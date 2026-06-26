#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <d3d12.h>
#include <dxgi1_6.h>

static const GUID IID_D3D12DeviceProbe = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};
static const GUID IID_IDXGIFactory2Probe = {
    0x50c83a1c, 0xe072, 0x4c48, {0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0}};

struct ExportProbe {
    const char* name;
    const char* symbol;
    int ordinal;
    FARPROC symbol_proc = nullptr;
    FARPROC ordinal_proc = nullptr;
};

struct ModuleProbe {
    const char* name;
    HMODULE handle = nullptr;
    std::string path;
    std::vector<ExportProbe> exports;
};

static std::string json_escape(const std::string& input) {
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

static std::string getenv_string(const char* key) {
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

static std::string wide_to_utf8(const WCHAR* text) {
    if (!text || !text[0])
        return "";
    int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1)
        return "";
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), needed, nullptr, nullptr);
    return out;
}

static const char* bool_json(bool value) {
    return value ? "true" : "false";
}

static std::string hr_hex(HRESULT hr) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08lx", static_cast<unsigned long>(static_cast<uint32_t>(hr)));
    return std::string(buf);
}

static uintptr_t rva(HMODULE module, FARPROC proc) {
    if (!module || !proc)
        return 0;
    return reinterpret_cast<uintptr_t>(proc) - reinterpret_cast<uintptr_t>(module);
}

static ModuleProbe inspect_module(const char* name, std::vector<ExportProbe> exports) {
    ModuleProbe probe{name, nullptr, "", exports};
    probe.handle = LoadLibraryA(name);
    if (!probe.handle)
        return probe;
    probe.path = module_path(probe.handle);
    for (auto& item : probe.exports) {
        if (item.symbol)
            item.symbol_proc = GetProcAddress(probe.handle, item.symbol);
        if (item.ordinal > 0)
            item.ordinal_proc = GetProcAddress(probe.handle, MAKEINTRESOURCEA(item.ordinal));
    }
    return probe;
}

static void print_export_json(const ModuleProbe& module, const ExportProbe& probe, bool last) {
    std::printf("        {\n");
    std::printf("          \"name\": \"%s\",\n", json_escape(probe.name).c_str());
    if (probe.symbol) {
        std::printf("          \"symbol\": \"%s\",\n", json_escape(probe.symbol).c_str());
        std::printf("          \"has_symbol\": %s,\n", bool_json(probe.symbol_proc != nullptr));
        std::printf("          \"symbol_rva\": \"0x%llx\",\n",
                    static_cast<unsigned long long>(rva(module.handle, probe.symbol_proc)));
    } else {
        std::printf("          \"symbol\": null,\n");
        std::printf("          \"has_symbol\": null,\n");
        std::printf("          \"symbol_rva\": null,\n");
    }
    if (probe.ordinal > 0) {
        std::printf("          \"ordinal\": %d,\n", probe.ordinal);
        std::printf("          \"has_ordinal\": %s,\n", bool_json(probe.ordinal_proc != nullptr));
        std::printf("          \"ordinal_rva\": \"0x%llx\"\n",
                    static_cast<unsigned long long>(rva(module.handle, probe.ordinal_proc)));
    } else {
        std::printf("          \"ordinal\": null,\n");
        std::printf("          \"has_ordinal\": null,\n");
        std::printf("          \"ordinal_rva\": null\n");
    }
    std::printf("        }%s\n", last ? "" : ",");
}

static void print_module_json(const ModuleProbe& module, bool last) {
    std::printf("    \"%s\": {\n", json_escape(module.name).c_str());
    std::printf("      \"loaded\": %s,\n", bool_json(module.handle != nullptr));
    std::printf("      \"path\": \"%s\",\n", json_escape(module.path).c_str());
    std::printf("      \"base\": \"0x%llx\",\n",
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(module.handle)));
    std::printf("      \"exports\": [\n");
    for (size_t i = 0; i < module.exports.size(); ++i)
        print_export_json(module, module.exports[i], i + 1 == module.exports.size());
    std::printf("      ]\n");
    std::printf("    }%s\n", last ? "" : ",");
}

int main() {
    std::string profile = getenv_string("D3D12_METAL_SDK_PROFILE");

    auto d3d12 = inspect_module("d3d12.dll", {{"D3D12CreateDevice", "D3D12CreateDevice", 101},
                                              {"D3D12GetInterface", "D3D12GetInterface", 0},
                                              {"D3D12SerializeRootSignature", "D3D12SerializeRootSignature", 0}});
    auto dxgi = inspect_module("dxgi.dll", {{"CreateDXGIFactory", "CreateDXGIFactory", 9},
                                            {"CreateDXGIFactory1", "CreateDXGIFactory1", 10},
                                            {"CreateDXGIFactory2", "CreateDXGIFactory2", 11}});
    auto dxgi_dxmt = inspect_module("dxgi_dxmt.dll", {{"CreateDXGIFactory", "CreateDXGIFactory", 9},
                                                      {"CreateDXGIFactory1", "CreateDXGIFactory1", 10},
                                                      {"CreateDXGIFactory2", "CreateDXGIFactory2", 11}});
    auto winemetal = inspect_module(
        "winemetal.dll", {{"WMTBootstrapRegister", "WMTBootstrapRegister", 0},
                          {"WMTBootstrapLookUp", "WMTBootstrapLookUp", 0},
                          {"WMTSetMetalShaderCachePath", "WMTSetMetalShaderCachePath", 0},
                          {"MTLLibrary_newFunctionWithDescriptor", "MTLLibrary_newFunctionWithDescriptor", 0}});

    using D3D12CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    using CreateDXGIFactory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);

    auto create_device = reinterpret_cast<D3D12CreateDeviceFn>(reinterpret_cast<void*>(d3d12.exports[0].symbol_proc));
    auto create_factory2 = reinterpret_cast<CreateDXGIFactory2Fn>(reinterpret_cast<void*>(dxgi.exports[2].symbol_proc));

    IDXGIFactory2* factory = nullptr;
    IDXGIAdapter1* adapter = nullptr;
    DXGI_ADAPTER_DESC1 adapter_desc = {};
    HRESULT create_factory_hr =
        create_factory2 ? create_factory2(0, IID_IDXGIFactory2Probe, reinterpret_cast<void**>(&factory)) : E_FAIL;
    HRESULT enum_adapter_hr = factory ? factory->EnumAdapters1(0, &adapter) : E_FAIL;
    HRESULT adapter_desc_hr = adapter ? adapter->GetDesc1(&adapter_desc) : E_FAIL;

    ID3D12Device* device = nullptr;
    HRESULT create_device_hr = create_device ? create_device(adapter, D3D_FEATURE_LEVEL_11_0, IID_D3D12DeviceProbe,
                                                             reinterpret_cast<void**>(&device))
                                             : E_FAIL;
    if (FAILED(create_device_hr) && create_device) {
        create_device_hr =
            create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_D3D12DeviceProbe, reinterpret_cast<void**>(&device));
    }

    D3D12_FEATURE_DATA_FEATURE_LEVELS levels = {};
    D3D_FEATURE_LEVEL requested_levels[] = {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
                                            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    levels.NumFeatureLevels = static_cast<UINT>(sizeof(requested_levels) / sizeof(requested_levels[0]));
    levels.pFeatureLevelsRequested = requested_levels;
    levels.MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    HRESULT feature_levels_hr =
        device ? device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels)) : E_FAIL;

    bool module_exports_ok = d3d12.handle && dxgi.handle && dxgi_dxmt.handle && winemetal.handle &&
                             d3d12.exports[0].symbol_proc && d3d12.exports[0].ordinal_proc &&
                             dxgi.exports[2].symbol_proc && dxgi_dxmt.exports[2].symbol_proc &&
                             winemetal.exports[0].symbol_proc && winemetal.exports[1].symbol_proc;
    bool adapter_ok = SUCCEEDED(create_factory_hr) && SUCCEEDED(enum_adapter_hr) && SUCCEEDED(adapter_desc_hr) &&
                      adapter_desc.VendorId != 0;
    bool device_ok = SUCCEEDED(create_device_hr) && device != nullptr && SUCCEEDED(feature_levels_hr);
    bool pass = module_exports_ok && adapter_ok && device_ok;

    std::printf("{\n");
    std::printf("  \"schema\": \"metalsharp.m12.fresh.runtime-identity.v1\",\n");
    std::printf("  \"profile\": \"%s\",\n", json_escape(profile).c_str());
    std::printf("  \"pass\": %s,\n", bool_json(pass));
    std::printf("  \"environment\": {\n");
    std::printf("    \"WINEDLLOVERRIDES\": \"%s\",\n", json_escape(getenv_string("WINEDLLOVERRIDES")).c_str());
    std::printf("    \"WINEDLLPATH\": \"%s\",\n", json_escape(getenv_string("WINEDLLPATH")).c_str());
    std::printf("    \"DXMT_WINEMETAL_UNIXLIB\": \"%s\"\n",
                json_escape(getenv_string("DXMT_WINEMETAL_UNIXLIB")).c_str());
    std::printf("  },\n");
    std::printf("  \"modules\": {\n");
    print_module_json(d3d12, false);
    print_module_json(dxgi, false);
    print_module_json(dxgi_dxmt, false);
    print_module_json(winemetal, true);
    std::printf("  },\n");
    std::printf("  \"dxgi\": {\n");
    std::printf("    \"CreateDXGIFactory2\": \"%s\",\n", hr_hex(create_factory_hr).c_str());
    std::printf("    \"EnumAdapters1\": \"%s\",\n", hr_hex(enum_adapter_hr).c_str());
    std::printf("    \"GetDesc1\": \"%s\",\n", hr_hex(adapter_desc_hr).c_str());
    std::printf("    \"adapter_description\": \"%s\",\n", json_escape(wide_to_utf8(adapter_desc.Description)).c_str());
    std::printf("    \"vendor_id\": %u,\n", adapter_desc.VendorId);
    std::printf("    \"device_id\": %u,\n", adapter_desc.DeviceId);
    std::printf("    \"dedicated_video_memory\": %llu,\n",
                static_cast<unsigned long long>(adapter_desc.DedicatedVideoMemory));
    std::printf("    \"shared_system_memory\": %llu\n",
                static_cast<unsigned long long>(adapter_desc.SharedSystemMemory));
    std::printf("  },\n");
    std::printf("  \"d3d12\": {\n");
    std::printf("    \"D3D12CreateDevice\": \"%s\",\n", hr_hex(create_device_hr).c_str());
    std::printf("    \"CheckFeatureSupport_FEATURE_LEVELS\": \"%s\",\n", hr_hex(feature_levels_hr).c_str());
    std::printf("    \"max_feature_level\": \"0x%x\"\n", static_cast<unsigned>(levels.MaxSupportedFeatureLevel));
    std::printf("  }\n");
    std::printf("}\n");

    if (device)
        device->Release();
    if (adapter)
        adapter->Release();
    if (factory)
        factory->Release();
    // This gate proves runtime identity and bootstrap, not process teardown.
    // Avoid CRT/static destruction after the JSON is flushed because the M12
    // runtime owns process-global worker state whose orderly shutdown is covered
    // by later lifecycle gates.
    std::fflush(stdout);
    std::fflush(stderr);
    TerminateProcess(GetCurrentProcess(), pass ? 0u : 1u);
    return pass ? 0 : 1;
}
