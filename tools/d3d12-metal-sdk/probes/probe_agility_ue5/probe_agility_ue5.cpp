#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <d3d12.h>

extern "C" __declspec(dllexport) const UINT D3D12SDKVersion = 619;
extern "C" __declspec(dllexport) const char D3D12SDKPath[] = ".\\D3D12\\";

struct ModuleInfo {
    const char* name;
    HMODULE handle = nullptr;
    std::string path;
    bool loaded = false;
    bool has_required_symbol = false;
};

struct InterfaceProbe {
    const char* name;
    GUID iid;
    HRESULT hr = E_FAIL;
    bool supported = false;
    bool requires_contract_review = false;
};

static const GUID IID_D3D12Device0Probe = {
    0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};
static const GUID IID_D3D12Device1Probe = {
    0x77acce80, 0x638e, 0x4e65, {0x88, 0x95, 0xc1, 0xf2, 0x33, 0x86, 0x86, 0x3e}};
static const GUID IID_D3D12Device2Probe = {
    0x30baa41e, 0xb15b, 0x475c, {0xa0, 0xbb, 0x1a, 0xf5, 0xc5, 0xb6, 0x43, 0x28}};
static const GUID IID_D3D12Device5Probe = {
    0x8b4f173b, 0x2fea, 0x4b80, {0x8f, 0x58, 0x43, 0x07, 0x19, 0x1a, 0xb9, 0x5d}};
static const GUID IID_D3D12Device10Probe = {
    0x517f8718, 0xaa66, 0x49f9, {0xb0, 0x2b, 0xa7, 0xab, 0x89, 0xc0, 0x60, 0x31}};
static const GUID IID_D3D12Device11Probe = {
    0x5405c344, 0xd457, 0x444e, {0xb4, 0xdd, 0x23, 0x66, 0xe4, 0x5a, 0xee, 0x39}};
static const GUID IID_D3D12Device12Probe = {
    0x5af5c532, 0x4c91, 0x4cd0, {0xb5, 0x41, 0x15, 0xa4, 0x05, 0x39, 0x5f, 0xc5}};

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

static std::string lower_ascii(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

static bool contains_ascii_ci(const std::string& haystack, const std::string& needle) {
    if (needle.empty())
        return true;
    return lower_ascii(haystack).find(lower_ascii(needle)) != std::string::npos;
}

static uint64_t fnv1a_file_hash(const std::string& path) {
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

static void inspect_module(ModuleInfo& module, const char* required_symbol = nullptr) {
    module.handle = LoadLibraryA(module.name);
    module.loaded = module.handle != nullptr;
    if (!module.loaded)
        return;

    module.path = module_path(module.handle);
    module.has_required_symbol =
        required_symbol == nullptr || GetProcAddress(module.handle, required_symbol) != nullptr;
}

static void print_module_json(const ModuleInfo& module, bool last) {
    uint64_t hash = module.loaded ? fnv1a_file_hash(module.path) : 0;
    std::printf("    \"%s\": {\n", json_escape(module.name).c_str());
    std::printf("      \"loaded\": %s,\n", module.loaded ? "true" : "false");
    std::printf("      \"path\": \"%s\",\n", json_escape(module.path).c_str());
    std::printf("      \"fnv1a64\": \"%016llx\",\n", static_cast<unsigned long long>(hash));
    std::printf("      \"has_required_symbol\": %s\n", module.has_required_symbol ? "true" : "false");
    std::printf("    }%s\n", last ? "" : ",");
}

static void print_interface_json(const InterfaceProbe& probe, bool last) {
    std::printf("    \"%s\": {\n", probe.name);
    std::printf("      \"hr\": \"0x%08lx\",\n", static_cast<unsigned long>(static_cast<uint32_t>(probe.hr)));
    std::printf("      \"supported\": %s,\n", probe.supported ? "true" : "false");
    std::printf("      \"classification\": \"%s\",\n", probe.supported ? "supported" : "safely_rejected");
    std::printf("      \"requires_contract_review\": %s\n", probe.requires_contract_review ? "true" : "false");
    std::printf("    }%s\n", last ? "" : ",");
}

int main() {
    std::string profile = getenv_string("D3D12_METAL_SDK_PROFILE");
    std::string expected_windows = getenv_string("D3D12_METAL_SDK_EXPECT_WINDOWS_SUBSTR");

    std::vector<ModuleInfo> modules = {
        {"D3D12\\D3D12Core.dll", nullptr, "", false, false},
        {"D3D12\\d3d12SDKLayers.dll", nullptr, "", false, false},
        {"D3D12\\D3D12StateObjectCompiler.dll", nullptr, "", false, false},
        {"d3d12.dll", nullptr, "", false, false},
    };

    inspect_module(modules[0]);
    inspect_module(modules[1]);
    inspect_module(modules[2]);
    inspect_module(modules[3], "D3D12CreateDevice");

    using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    FARPROC create_device_proc = modules[3].loaded ? GetProcAddress(modules[3].handle, "D3D12CreateDevice") : nullptr;
    auto create_device = reinterpret_cast<CreateDeviceFn>(reinterpret_cast<void*>(create_device_proc));

    IUnknown* device = nullptr;
    HRESULT create_hr = E_FAIL;
    if (create_device)
        create_hr =
            create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_D3D12Device0Probe, reinterpret_cast<void**>(&device));

    std::vector<InterfaceProbe> interfaces = {
        {"ID3D12Device", IID_D3D12Device0Probe, E_FAIL, false, false},
        {"ID3D12Device1", IID_D3D12Device1Probe, E_FAIL, false, false},
        {"ID3D12Device2", IID_D3D12Device2Probe, E_FAIL, false, false},
        {"ID3D12Device5", IID_D3D12Device5Probe, E_FAIL, false, false},
        {"ID3D12Device10", IID_D3D12Device10Probe, E_FAIL, false, false},
        {"ID3D12Device11", IID_D3D12Device11Probe, E_FAIL, false, false},
        {"ID3D12Device12", IID_D3D12Device12Probe, E_FAIL, false, false},
    };

    if (device) {
        for (auto& probe : interfaces) {
            void* queried = nullptr;
            probe.hr = device->QueryInterface(probe.iid, &queried);
            probe.supported = SUCCEEDED(probe.hr) && queried != nullptr;
            probe.requires_contract_review = probe.supported && std::strcmp(probe.name, "ID3D12Device") != 0 &&
                                             std::strcmp(probe.name, "ID3D12Device1") != 0 &&
                                             std::strcmp(probe.name, "ID3D12Device2") != 0;
        }
    }

    bool d3d12_expected_path = expected_windows.empty() || contains_ascii_ci(modules[3].path, expected_windows);
    bool pass = modules[0].loaded && modules[1].loaded && modules[3].loaded && modules[3].has_required_symbol &&
                d3d12_expected_path && SUCCEEDED(create_hr) && device != nullptr && interfaces[0].supported;

    std::printf("{\n");
    std::printf("  \"schema\": \"metalsharp.d3d12-metal.probe-agility-ue5.v1\",\n");
    std::printf("  \"profile\": \"%s\",\n", json_escape(profile).c_str());
    std::printf("  \"pass\": %s,\n", pass ? "true" : "false");
    std::printf("  \"sdk\": {\n");
    std::printf("    \"D3D12SDKVersion\": %u,\n", D3D12SDKVersion);
    std::printf("    \"D3D12SDKPath\": \"%s\"\n", json_escape(D3D12SDKPath).c_str());
    std::printf("  },\n");
    std::printf("  \"device_create\": {\n");
    std::printf("    \"minimum_feature_level\": \"11_0\",\n");
    std::printf("    \"hr\": \"0x%08lx\",\n", static_cast<unsigned long>(static_cast<uint32_t>(create_hr)));
    std::printf("    \"succeeded\": %s\n", SUCCEEDED(create_hr) ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"routing\": {\n");
    std::printf("    \"expected_d3d12_path_substring\": \"%s\",\n", json_escape(expected_windows).c_str());
    std::printf("    \"d3d12_expected_path_match\": %s\n", d3d12_expected_path ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"modules\": {\n");
    for (size_t i = 0; i < modules.size(); ++i)
        print_module_json(modules[i], i + 1 == modules.size());
    std::printf("  },\n");
    std::printf("  \"device_interfaces\": {\n");
    for (size_t i = 0; i < interfaces.size(); ++i)
        print_interface_json(interfaces[i], i + 1 == interfaces.size());
    std::printf("  }\n");
    std::printf("}\n");

    // DXMT may keep Wine-hosted worker synchronization objects alive at process
    // teardown. This probe is diagnostic, so flush JSON and terminate hard
    // before COM/module cleanup can mask successful evidence with CRT asserts.
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), pass ? 0 : 1);
}
