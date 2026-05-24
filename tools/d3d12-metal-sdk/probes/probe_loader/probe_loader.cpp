#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct ModuleProbe {
  const char *name;
  const char *required_symbol;
  int required_ordinal;
  HMODULE handle = nullptr;
  std::string path;
  bool loaded = false;
  bool has_symbol = false;
  bool has_ordinal = false;
};

static std::string json_escape(const std::string &input) {
  std::string out;
  out.reserve(input.size() + 8);
  for (char c : input) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
        out += buf;
      } else {
        out += c;
      }
      break;
    }
  }
  return out;
}

static std::string getenv_string(const char *key) {
  DWORD needed = GetEnvironmentVariableA(key, nullptr, 0);
  if (needed == 0)
    return "";
  std::string value(needed, '\0');
  DWORD written = GetEnvironmentVariableA(key, value.data(), needed);
  if (written == 0)
    return "";
  value.resize(written);
  return value;
}

static std::string module_path(HMODULE module) {
  char buffer[4096];
  DWORD written = GetModuleFileNameA(module, buffer, sizeof(buffer));
  if (written == 0)
    return "";
  if (written >= sizeof(buffer))
    written = sizeof(buffer) - 1;
  return std::string(buffer, written);
}

static uint64_t fnv1a_file_hash(const std::string &path) {
  HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return 0;

  uint64_t hash = 1469598103934665603ull;
  unsigned char buffer[64 * 1024];
  DWORD read = 0;
  while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
    for (DWORD i = 0; i < read; ++i) {
      hash ^= buffer[i];
      hash *= 1099511628211ull;
    }
  }
  CloseHandle(file);
  return hash;
}

static std::string lower_ascii(std::string value) {
  for (char &c : value) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  return value;
}

static bool contains_ascii_ci(const std::string &haystack, const std::string &needle) {
  if (needle.empty())
    return true;
  return lower_ascii(haystack).find(lower_ascii(needle)) != std::string::npos;
}

static void inspect_module(ModuleProbe &probe) {
  probe.handle = LoadLibraryA(probe.name);
  probe.loaded = probe.handle != nullptr;
  if (!probe.loaded)
    return;
  probe.path = module_path(probe.handle);
  if (probe.required_symbol)
    probe.has_symbol = GetProcAddress(probe.handle, probe.required_symbol) != nullptr;
  if (probe.required_ordinal > 0)
    probe.has_ordinal = GetProcAddress(probe.handle, MAKEINTRESOURCEA(probe.required_ordinal)) != nullptr;
}

static void print_module_json(const ModuleProbe &probe, const std::string &expected_path_substring,
                              bool last) {
  uint64_t hash = probe.loaded ? fnv1a_file_hash(probe.path) : 0;
  bool expected_path = probe.loaded && contains_ascii_ci(probe.path, expected_path_substring);
  std::printf("    \"%s\": {\n", probe.name);
  std::printf("      \"loaded\": %s,\n", probe.loaded ? "true" : "false");
  std::printf("      \"path\": \"%s\",\n", json_escape(probe.path).c_str());
  std::printf("      \"fnv1a64\": \"%016llx\",\n", static_cast<unsigned long long>(hash));
  std::printf("      \"expected_path_match\": %s,\n", expected_path ? "true" : "false");
  if (probe.required_symbol) {
    std::printf("      \"required_symbol\": \"%s\",\n", probe.required_symbol);
    std::printf("      \"has_required_symbol\": %s,\n", probe.has_symbol ? "true" : "false");
  } else {
    std::printf("      \"required_symbol\": null,\n");
    std::printf("      \"has_required_symbol\": null,\n");
  }
  if (probe.required_ordinal > 0) {
    std::printf("      \"required_ordinal\": %d,\n", probe.required_ordinal);
    std::printf("      \"has_required_ordinal\": %s\n", probe.has_ordinal ? "true" : "false");
  } else {
    std::printf("      \"required_ordinal\": null,\n");
    std::printf("      \"has_required_ordinal\": null\n");
  }
  std::printf("    }%s\n", last ? "" : ",");
}

int main() {
  std::vector<ModuleProbe> probes = {
      {"d3d12.dll", "D3D12CreateDevice", 101, nullptr, "", false, false, false},
      {"dxgi.dll", "CreateDXGIFactory2", 0, nullptr, "", false, false, false},
      {"d3d11.dll", "D3D11CreateDevice", 0, nullptr, "", false, false, false},
      {"d3d10core.dll", nullptr, 0, nullptr, "", false, false, false},
      {"winemetal.dll", nullptr, 0, nullptr, "", false, false, false},
  };

  for (auto &probe : probes)
    inspect_module(probe);

  std::string expected_windows = getenv_string("D3D12_METAL_SDK_EXPECT_WINDOWS_SUBSTR");
  std::string profile = getenv_string("D3D12_METAL_SDK_PROFILE");

  bool pass = true;
  for (const auto &probe : probes) {
    if (!probe.loaded)
      pass = false;
  }
  if (!probes[0].has_symbol || !probes[0].has_ordinal)
    pass = false;
  if (!probes[1].has_symbol)
    pass = false;
  if (!expected_windows.empty()) {
    for (const auto &probe : probes) {
      if (!contains_ascii_ci(probe.path, expected_windows))
        pass = false;
    }
  }

  std::printf("{\n");
  std::printf("  \"schema\": \"metalsharp.d3d12-metal.probe-loader.v1\",\n");
  std::printf("  \"profile\": \"%s\",\n", json_escape(profile).c_str());
  std::printf("  \"pass\": %s,\n", pass ? "true" : "false");
  std::printf("  \"environment\": {\n");
  std::printf("    \"WINEDLLOVERRIDES\": \"%s\",\n", json_escape(getenv_string("WINEDLLOVERRIDES")).c_str());
  std::printf("    \"WINEDLLPATH\": \"%s\",\n", json_escape(getenv_string("WINEDLLPATH")).c_str());
  std::printf("    \"DXMT_CONFIG\": \"%s\",\n", json_escape(getenv_string("DXMT_CONFIG")).c_str());
  std::printf("    \"DXMT_CONFIG_FILE\": \"%s\",\n", json_escape(getenv_string("DXMT_CONFIG_FILE")).c_str());
  std::printf("    \"D3D12_METAL_SDK_EXPECT_WINDOWS_SUBSTR\": \"%s\"\n",
              json_escape(expected_windows).c_str());
  std::printf("  },\n");
  std::printf("  \"modules\": {\n");
  for (size_t i = 0; i < probes.size(); ++i)
    print_module_json(probes[i], expected_windows, i + 1 == probes.size());
  std::printf("  }\n");
  std::printf("}\n");

  for (auto &probe : probes) {
    if (probe.handle)
      FreeLibrary(probe.handle);
  }
  return pass ? 0 : 1;
}
