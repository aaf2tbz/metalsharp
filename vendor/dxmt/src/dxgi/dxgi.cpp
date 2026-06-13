#include "dxgi_interfaces.h"
#include "dxgi_trace.hpp"
#include "log/log.hpp"
#include "util_env.hpp"
#include "winemetal.h"
#include <d3d11.h>
#include <mutex>
#include <string>

namespace dxmt {
Logger Logger::s_instance("dxgi.log");

VendorExtension g_extension_enabled = VendorExtension::None;

#ifdef _WIN32
std::once_flag nvext_init;

static void InitializeVendorExtensionNV() {
#ifdef __i386__
  return;
#endif
  HKEY key1 = nullptr, key2 = nullptr, key3 = nullptr;
  auto name1 = L"{41FCC608-8496-4DEF-B43E-7D9BD675A6FF}";
  auto name2 = L"FullPath";
  WCHAR value3[] = L"C:\\Windows\\System32";
  const auto exe_name = env::getExeName();
  const bool enable_nvext = env::getEnvVar("DXMT_ENABLE_NVEXT") == "1";
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\NVIDIA Corporation\\Global", 0, nullptr, 0,
                      KEY_ALL_ACCESS, nullptr, &key1, nullptr) ||
      RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\ControlSet001\\Services\\nvlddmkm", 0, nullptr,
                      0, KEY_ALL_ACCESS, nullptr, &key2, nullptr) ||
      RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", 0,
                      nullptr, 0, KEY_ALL_ACCESS, nullptr, &key3, nullptr)) {
    goto cleanup;
  }
  if (!enable_nvext) {
    RegDeleteValueW(key1, name1);
    RegDeleteValueW(key2, name1);
    RegDeleteValueW(key3, name2);
  } else {
    DWORD _1 = 1;
    RegSetValueExW(key1, name1, 0, REG_DWORD, (const BYTE *)&_1, sizeof(_1));
    RegSetValueExW(key2, name1, 0, REG_DWORD, (const BYTE *)&_1, sizeof(_1));
    RegSetValueExW(key3, name2, 0, REG_SZ, (const BYTE *)value3, sizeof(value3));
    Logger::info("Vendor extension enabled: NVEXT");
    DXMTDXGITrace("DXGI", "NVEXT enabled exe=%s env=%s", exe_name.c_str(),
                  env::getEnvVar("DXMT_ENABLE_NVEXT").c_str());
    g_extension_enabled = VendorExtension::Nvidia;
  }
  cleanup:
  if (key1) RegCloseKey(key1);
  if (key2) RegCloseKey(key2);
  if (key3) RegCloseKey(key3);
};

static void InitializeMetalCachePath() {
  if (env::getEnvVar("DXMT_USE_DEFAULT_METAL_CACHE") == "1")
    return;
  // This is the framework cache (mainly for PSOs), not managed by DXMT
  auto metal_cache_path = str::format("dxmt/", env::getExeName(), "/com.apple.metal");
  if (!WMTSetMetalShaderCachePath(metal_cache_path.c_str())) {
    Logger::info("Failed to set Metal cache path, fallback to system default");
  }
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason,
                               LPVOID reserved) {
  if (reason != DLL_PROCESS_ATTACH)
    return TRUE;

  DisableThreadLibraryCalls(instance);

  FILE *f = dxmt::openDiagnosticLog("dxmt-dxgi-trace.log");
  if (f) {
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    fprintf(f, "=== dxgi.dll DllMain PROCESS_ATTACH pid=%lu exe=[%s] ===\n",
            GetCurrentProcessId(), exe);
    fclose(f);
  }

  InitializeMetalCachePath();
  std::call_once(nvext_init, InitializeVendorExtensionNV);

  return TRUE;
}

#endif

extern "C" HRESULT __stdcall DXGIGetDebugInterface1(UINT Flags, REFIID riid,
                                                    void **ppDebug) {
  DXMTDXGITrace("DXGI", "DXGIGetDebugInterface1 Flags=0x%x riid=%s out=%p",
                Flags, str::format(riid).c_str(), ppDebug);
#ifdef _WIN32
  // it's a DXMT implementation detail
  if (riid == DXMT_NVEXT_GUID) {
    std::call_once(nvext_init, InitializeVendorExtensionNV);
    HRESULT hr = g_extension_enabled == VendorExtension::Nvidia ? S_OK : E_NOINTERFACE;
    DXMTDXGITrace("DXGI", "DXGIGetDebugInterface1 NVEXT -> 0x%lx", hr);
    return hr;
  }
#endif

  return E_NOINTERFACE;
}

extern "C" HRESULT __stdcall DXGIGetDebugInterface(REFIID riid,
                                                   void **ppDebug) {
  DXMTDXGITrace("DXGI", "DXGIGetDebugInterface riid=%s out=%p",
                str::format(riid).c_str(), ppDebug);
  return DXGIGetDebugInterface1(0, riid, ppDebug);
}

using PFN_D3D11CoreCreateDevice =
    HRESULT(WINAPI *)(IDXGIFactory *, IDXGIAdapter *, UINT,
                      const D3D_FEATURE_LEVEL *, UINT, ID3D11Device **);

extern "C" HRESULT WINAPI
DXGID3D10CreateDevice(HMODULE d3d11_module, IDXGIFactory *factory,
                      IDXGIAdapter *adapter, UINT flags,
                      const D3D_FEATURE_LEVEL *feature_levels,
                      UINT feature_level_count, void **device) {
  DXMTDXGITrace("DXGI",
                "DXGID3D10CreateDevice module=%p factory=%p adapter=%p "
                "flags=0x%x levels=%u out=%p",
                d3d11_module, factory, adapter, flags, feature_level_count,
                device);

  if (!device)
    return E_INVALIDARG;
  *device = nullptr;

  auto create_device = reinterpret_cast<PFN_D3D11CoreCreateDevice>(
      GetProcAddress(d3d11_module, "D3D11CoreCreateDevice"));
  if (!create_device) {
    Logger::err("DXGID3D10CreateDevice: D3D11CoreCreateDevice is missing");
    return E_NOINTERFACE;
  }

  ID3D11Device *d3d11_device = nullptr;
  HRESULT hr = create_device(factory, adapter, flags, feature_levels,
                             feature_level_count, &d3d11_device);
  if (FAILED(hr)) {
    DXMTDXGITrace("DXGI", "DXGID3D10CreateDevice -> 0x%lx", hr);
    return hr;
  }

  *device = d3d11_device;
  DXMTDXGITrace("DXGI", "DXGID3D10CreateDevice -> %p", d3d11_device);
  return S_OK;
}

extern "C" HRESULT WINAPI DXGID3D10RegisterLayers() {
  DXMTDXGITrace("DXGI", "DXGID3D10RegisterLayers");
  return S_OK;
}

} // namespace dxmt
