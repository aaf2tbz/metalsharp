#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::string json_escape(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char ch : value) {
    switch (ch) {
    case '\\': out += "\\\\"; break;
    case '"': out += "\\\""; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (static_cast<unsigned char>(ch) < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(ch));
        out += buf;
      } else {
        out += ch;
      }
      break;
    }
  }
  return out;
}

static std::string module_path(HMODULE module) {
  char path[MAX_PATH * 4] = {};
  DWORD len = GetModuleFileNameA(module, path, sizeof(path));
  if (!len || len >= sizeof(path))
    return "";
  return path;
}

struct ModuleCheck {
  const char *name;
  const char *required_export;
  HMODULE module = nullptr;
  DWORD load_error = 0;
  FARPROC export_ptr = nullptr;
  std::string path;
};

static void print_module_json(const ModuleCheck &check, bool last) {
  std::printf(
      "    {\"name\":\"%s\",\"loaded\":%s,\"load_error\":%lu,"
      "\"path\":\"%s\",\"required_export\":\"%s\",\"has_required_export\":%s}%s\n",
      check.name,
      check.module ? "true" : "false",
      static_cast<unsigned long>(check.load_error),
      json_escape(check.path).c_str(),
      check.required_export ? check.required_export : "",
      check.export_ptr ? "true" : "false",
      last ? "" : ",");
}

int main() {
  std::vector<ModuleCheck> modules = {
      {"d3d12.dll", "D3D12CreateDevice", nullptr, 0, nullptr, ""},
      {"dxgi.dll", "CreateDXGIFactory2", nullptr, 0, nullptr, ""},
      {"dxgi_dxmt.dll", "CreateDXGIFactory2", nullptr, 0, nullptr, ""},
      {"winemetal.dll", "WMTBootstrapRegister", nullptr, 0, nullptr, ""},
  };

  bool modules_ok = true;
  for (auto &check : modules) {
    SetLastError(0);
    check.module = LoadLibraryA(check.name);
    check.load_error = check.module ? 0 : GetLastError();
    if (check.module) {
      check.path = module_path(check.module);
      check.export_ptr = GetProcAddress(check.module, check.required_export);
    }
    modules_ok = modules_ok && check.module && check.export_ptr;
  }

  HRESULT create_device_hr = E_FAIL;
  bool d3d12_device_created = false;
  if (modules[0].export_ptr) {
    PFN_D3D12_CREATE_DEVICE create_device = nullptr;
    std::memcpy(&create_device, &modules[0].export_ptr, sizeof(create_device));
    ID3D12Device *device = nullptr;
    create_device_hr = create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_ID3D12Device,
                                     reinterpret_cast<void **>(&device));
    d3d12_device_created = SUCCEEDED(create_device_hr) && device != nullptr;
    if (device)
      device->Release();
  }

  HRESULT factory_hr = E_FAIL;
  bool dxgi_factory_created = false;
  if (modules[1].export_ptr) {
    using PFN_CREATE_DXGI_FACTORY2 = HRESULT(WINAPI *)(UINT, REFIID, void **);
    PFN_CREATE_DXGI_FACTORY2 create_factory = nullptr;
    std::memcpy(&create_factory, &modules[1].export_ptr, sizeof(create_factory));
    IDXGIFactory2 *factory = nullptr;
    factory_hr = create_factory(0, IID_IDXGIFactory2, reinterpret_cast<void **>(&factory));
    dxgi_factory_created = SUCCEEDED(factory_hr) && factory != nullptr;
    if (factory)
      factory->Release();
  }

  bool pass = modules_ok && d3d12_device_created && dxgi_factory_created;
  std::printf("{\n");
  std::printf("  \"schema\": \"metalsharp.m12-runtime-alignment.probe.v1\",\n");
  std::printf("  \"pass\": %s,\n", pass ? "true" : "false");
  std::printf("  \"modules_ok\": %s,\n", modules_ok ? "true" : "false");
  std::printf("  \"d3d12_device\": {\"hr\": \"0x%08lx\", \"created\": %s},\n",
              static_cast<unsigned long>(create_device_hr), d3d12_device_created ? "true" : "false");
  std::printf("  \"dxgi_factory\": {\"hr\": \"0x%08lx\", \"created\": %s},\n",
              static_cast<unsigned long>(factory_hr), dxgi_factory_created ? "true" : "false");
  std::printf("  \"modules\": [\n");
  for (size_t i = 0; i < modules.size(); ++i)
    print_module_json(modules[i], i + 1 == modules.size());
  std::printf("  ]\n");
  std::printf("}\n");
  return pass ? 0 : 1;
}
