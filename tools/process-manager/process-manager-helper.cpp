// MetalSharp Process Manager native telemetry helper
// C++17, intentionally dependency-light so it can be built during app packaging.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <ctime>
#include <cctype>
#include <algorithm>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <mach/host_info.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>
#include <unistd.h>
#endif

namespace {

std::string json_escape(const std::string &s) {
  std::ostringstream out;
  for (char c : s) {
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out << buf;
        } else {
          out << c;
        }
    }
  }
  return out.str();
}

#ifdef __APPLE__
uint64_t sysctl_u64(const char *name) {
  uint64_t value = 0;
  size_t size = sizeof(value);
  if (sysctlbyname(name, &value, &size, nullptr, 0) != 0) return 0;
  return value;
}

uint32_t sysctl_u32(const char *name) {
  uint32_t value = 0;
  size_t size = sizeof(value);
  if (sysctlbyname(name, &value, &size, nullptr, 0) != 0) return 0;
  return value;
}

std::string sysctl_string(const char *name) {
  size_t size = 0;
  if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) return "";
  std::vector<char> buf(size + 1, 0);
  if (sysctlbyname(name, buf.data(), &size, nullptr, 0) != 0) return "";
  return std::string(buf.data());
}

struct CpuTicks {
  uint64_t user = 0;
  uint64_t system = 0;
  uint64_t idle = 0;
  uint64_t nice = 0;
};

bool read_cpu_ticks(CpuTicks &ticks) {
  host_cpu_load_info_data_t info{};
  mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
  kern_return_t kr = host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&info), &count);
  if (kr != KERN_SUCCESS) return false;
  ticks.user = info.cpu_ticks[CPU_STATE_USER];
  ticks.system = info.cpu_ticks[CPU_STATE_SYSTEM];
  ticks.idle = info.cpu_ticks[CPU_STATE_IDLE];
  ticks.nice = info.cpu_ticks[CPU_STATE_NICE];
  return true;
}

double cpu_usage_sample() {
  CpuTicks a, b;
  if (!read_cpu_ticks(a)) return -1.0;
  std::this_thread::sleep_for(std::chrono::milliseconds(180));
  if (!read_cpu_ticks(b)) return -1.0;
  const uint64_t idle = b.idle - a.idle;
  const uint64_t total = (b.user - a.user) + (b.system - a.system) + (b.idle - a.idle) + (b.nice - a.nice);
  if (total == 0) return -1.0;
  return 100.0 * static_cast<double>(total - idle) / static_cast<double>(total);
}

void memory_info(uint64_t &used, uint64_t &total) {
  total = sysctl_u64("hw.memsize");
  used = 0;
  vm_statistics64_data_t vmstat{};
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  kern_return_t kr = host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vmstat), &count);
  if (kr != KERN_SUCCESS) return;
  const uint64_t page = static_cast<uint64_t>(getpagesize());
  const uint64_t free_bytes = static_cast<uint64_t>(vmstat.free_count + vmstat.inactive_count) * page;
  used = total > free_bytes ? total - free_bytes : 0;
}
#else
uint32_t sysctl_u32(const char *) { return static_cast<uint32_t>(std::thread::hardware_concurrency()); }
std::string sysctl_string(const char *) { return "unknown"; }
double cpu_usage_sample() { return -1.0; }
void memory_info(uint64_t &used, uint64_t &total) { used = 0; total = 0; }
#endif

struct ProcessInfo {
  int pid = 0;
  double cpu = 0.0;
  double mem = 0.0;
  std::string name;
  std::string command;
};

std::string lower_copy(std::string s) {
  for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

double parse_gpu_value(const std::string &text, const std::string &key) {
  const std::string needle = "\"" + key + "\"=";
  const auto pos = text.find(needle);
  if (pos == std::string::npos) return -1.0;
  const auto start = pos + needle.size();
  char *end = nullptr;
  const double value = std::strtod(text.c_str() + start, &end);
  if (end == text.c_str() + start) return -1.0;
  return value;
}

std::string cf_string_to_std(CFTypeRef value) {
#ifdef __APPLE__
  if (!value || CFGetTypeID(value) != CFStringGetTypeID()) return "";
  char buffer[512];
  if (CFStringGetCString(static_cast<CFStringRef>(value), buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
    return std::string(buffer);
  }
#endif
  return "";
}

double hid_pmu_cpu_temp_c(std::string &source) {
#ifdef __APPLE__
  void *iokit = dlopen("/System/Library/Frameworks/IOKit.framework/IOKit", RTLD_LAZY | RTLD_LOCAL);
  if (!iokit) return -1.0;

  using ClientCreateFn = void *(*)(CFAllocatorRef);
  using CopyServicesFn = CFArrayRef (*)(void *);
  using CopyEventFn = void *(*)(void *, int64_t, int32_t, int64_t);
  using GetFloatValueFn = double (*)(void *, int32_t);
  using CopyPropertyFn = CFTypeRef (*)(void *, CFStringRef);

  auto client_create = reinterpret_cast<ClientCreateFn>(dlsym(iokit, "IOHIDEventSystemClientCreate"));
  auto copy_services = reinterpret_cast<CopyServicesFn>(dlsym(iokit, "IOHIDEventSystemClientCopyServices"));
  auto copy_event = reinterpret_cast<CopyEventFn>(dlsym(iokit, "IOHIDServiceClientCopyEvent"));
  auto get_float = reinterpret_cast<GetFloatValueFn>(dlsym(iokit, "IOHIDEventGetFloatValue"));
  auto copy_property = reinterpret_cast<CopyPropertyFn>(dlsym(iokit, "IOHIDServiceClientCopyProperty"));
  if (!client_create || !copy_services || !copy_event || !get_float) {
    dlclose(iokit);
    return -1.0;
  }

  void *client = client_create(kCFAllocatorDefault);
  if (!client) {
    dlclose(iokit);
    return -1.0;
  }

  CFArrayRef services = copy_services(client);
  if (!services) {
    CFRelease(client);
    dlclose(iokit);
    return -1.0;
  }

  constexpr int64_t kIOHIDEventTypeTemperature = 15;
  constexpr int32_t kIOHIDEventFieldTemperatureLevel = 15 << 16;
  CFStringRef product_key = CFSTR("Product");

  double best = -1.0;
  std::string best_product;
  const CFIndex count = CFArrayGetCount(services);
  for (CFIndex i = 0; i < count; ++i) {
    void *service = const_cast<void *>(CFArrayGetValueAtIndex(services, i));
    if (!service) continue;

    std::string product;
    if (copy_property) {
      CFTypeRef product_value = copy_property(service, product_key);
      product = cf_string_to_std(product_value);
      if (product_value) CFRelease(product_value);
    }
    const std::string lowered = lower_copy(product);
    const bool looks_like_pmu_temp = lowered.find("pmu") != std::string::npos || lowered.find("temp") != std::string::npos;
    if (!looks_like_pmu_temp) continue;

    void *event = copy_event(service, kIOHIDEventTypeTemperature, 0, 0);
    if (!event) continue;
    const double celsius = get_float(event, kIOHIDEventFieldTemperatureLevel);
    CFRelease(event);

    // Apple Silicon PMU temperature services normally report Celsius. Filter out
    // uninitialized/obviously bogus values, then use the hottest PMU channel as
    // the most useful package-like in-game thermal indicator.
    if (celsius > 0.0 && celsius < 130.0 && celsius > best) {
      best = celsius;
      best_product = product.empty() ? "PMU temperature" : product;
    }
  }

  CFRelease(services);
  CFRelease(client);
  dlclose(iokit);

  if (best > 0.0) {
    source = "IOHID private PMU sensor: " + best_product;
    return best;
  }
#endif
  return -1.0;
}

double cpu_temperature_c(std::string &source) {
  const double hid = hid_pmu_cpu_temp_c(source);
  if (hid > 0.0) return hid;
  source = "temperature sensor unavailable";
  return -1.0;
}

double gpu_usage_percent() {
#ifdef __APPLE__
  FILE *pipe = popen("/usr/sbin/ioreg -r -c AGXAccelerator -d 1 2>/dev/null", "r");
  if (!pipe) return -1.0;
  std::string output;
  char line[4096];
  while (std::fgets(line, sizeof(line), pipe)) output += line;
  pclose(pipe);

  const double device = parse_gpu_value(output, "Device Utilization %");
  const double renderer = parse_gpu_value(output, "Renderer Utilization %");
  const double tiler = parse_gpu_value(output, "Tiler Utilization %");
  double best = device >= 0.0 ? device : std::max(renderer, tiler);
  if (best < 0.0) return -1.0;
  if (best > 100.0) best = 100.0;
  return best;
#else
  return -1.0;
#endif
}

std::vector<ProcessInfo> session_processes() {
  std::vector<ProcessInfo> rows;
  FILE *pipe = popen("/bin/ps -axo pid=,pcpu=,pmem=,comm=,command=", "r");
  if (!pipe) return rows;
  char line[4096];
  while (std::fgets(line, sizeof(line), pipe) && rows.size() < 14) {
    std::istringstream in(line);
    ProcessInfo p;
    if (!(in >> p.pid >> p.cpu >> p.mem >> p.name)) continue;
    std::getline(in, p.command);
    if (!p.command.empty() && p.command[0] == ' ') p.command.erase(0, 1);
    const std::string haystack = lower_copy(p.name + " " + p.command);
    if (haystack.find("wine") == std::string::npos &&
        haystack.find("steam") == std::string::npos &&
        haystack.find("metalsharp") == std::string::npos &&
        haystack.find("gameoverlayui") == std::string::npos) {
      continue;
    }
    rows.push_back(p);
  }
  pclose(pipe);
  return rows;
}

} // namespace

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  uint64_t mem_used = 0;
  uint64_t mem_total = 0;
  memory_info(mem_used, mem_total);
  const uint32_t cores = sysctl_u32("hw.logicalcpu");
  const double cpu = cpu_usage_sample();
  const std::string chip = sysctl_string("machdep.cpu.brand_string").empty()
    ? sysctl_string("hw.model")
    : sysctl_string("machdep.cpu.brand_string");

  // CPU temperature, process-presented frame rate, and GPU utilization need privileged/private
  // sensors or a future in-process render hook. Emit nulls now so the overlay can show honest
  // placeholders while the visual surface is built.
  const auto processes = session_processes();
  const double gpu = gpu_usage_percent();
  std::string temp_source;
  const double cpu_temp = cpu_temperature_c(temp_source);

  std::cout << "{"
            << "\"ok\":true,"
            << "\"source\":\"metalsharp-process-helper-cpp\","
            << "\"timestamp\":" << static_cast<unsigned long long>(std::time(nullptr)) << ","
            << "\"fps\":null,"
            << "\"cpu_percent\":" << (cpu >= 0.0 ? cpu : 0.0) << ","
            << "\"cpu_temp_c\":" << (cpu_temp > 0.0 ? std::to_string(cpu_temp) : "null") << ","
            << "\"cpu_temp_source\":\"" << json_escape(temp_source) << "\","
            << "\"cores_used\":" << (cpu >= 0.0 ? (cpu / 100.0) * static_cast<double>(cores) : 0.0) << ","
            << "\"cores_total\":" << cores << ","
            << "\"ram_used_bytes\":" << static_cast<unsigned long long>(mem_used) << ","
            << "\"ram_total_bytes\":" << static_cast<unsigned long long>(mem_total) << ","
            << "\"gpu_percent\":" << (gpu >= 0.0 ? std::to_string(gpu) : "null") << ","
            << "\"gpu_label\":\"AGX system Device Utilization % via ioreg\","
            << "\"chip\":\"" << json_escape(chip) << "\",";
  std::cout << "\"processes\":[";
  for (size_t i = 0; i < processes.size(); ++i) {
    const auto &p = processes[i];
    if (i) std::cout << ",";
    std::cout << "{"
              << "\"pid\":" << p.pid << ","
              << "\"cpu_percent\":" << p.cpu << ","
              << "\"mem_percent\":" << p.mem << ","
              << "\"name\":\"" << json_escape(p.name) << "\","
              << "\"command\":\"" << json_escape(p.command) << "\""
              << "}";
  }
  std::cout << "]}" << std::endl;
  return 0;
}
