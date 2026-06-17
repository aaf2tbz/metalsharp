#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <d3d12.h>

extern "C" {
__declspec(dllexport) UINT D3D12SDKVersion = 619;
__declspec(dllexport) char D3D12SDKPath[260] = ".\\D3D12\\";
}

static const GUID IID_D3D12DeviceProbe = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};
static const GUID IID_IMetalSharpM12TranslationLayerInfo = {
    0x4d315232, 0x4d53, 0x4458, {0x8c, 0x3a, 0x31, 0x6f, 0xc7, 0x42, 0x7a, 0x11}};

static constexpr UINT MetalSharpM12TranslationLayerInfoAbiVersion = 1;
static constexpr UINT MetalSharpM12TranslationLayerVendorMetalSharp = 0x4d533132u; // MS12
static constexpr UINT MetalSharpM12TranslationLayerIdDxmtM12 = 0x44583132u;        // DX12
static constexpr UINT M12CORE_ABI_VERSION = 1;
static constexpr UINT M12CORE_BUILD_ID_LOW = 0x4d313243u; // M12C
static constexpr UINT M12CORE_BUILD_ID_HIGH = 0x00000010u;
static constexpr uint64_t FeatureD3D12 = 1ull << 0;
static constexpr uint64_t FeatureDXMT = 1ull << 1;
static constexpr uint64_t FeatureLibM12Core = 1ull << 2;
static constexpr uint64_t FeatureRootBindingPlans = 1ull << 3;
static constexpr uint64_t FeaturePrewarmPacks = 1ull << 4;
static constexpr uint64_t FeatureDrawPlanning = 1ull << 5;
static constexpr uint64_t FeaturePresentPlanning = 1ull << 6;
static constexpr uint64_t FeatureReplayPlanning = 1ull << 7;
static constexpr uint64_t FeatureCommandStreamDescriptors = 1ull << 8;
static constexpr UINT M12CORE_FEATURE_DRAW_PLANNING = 1u << 12;
static constexpr UINT M12CORE_FEATURE_PRESENT_PLANNING = 1u << 13;
static constexpr UINT M12CORE_FEATURE_REPLAY_PLANNING = 1u << 14;
static constexpr UINT M12CORE_FEATURE_COMMAND_STREAM_DESCRIPTORS = 1u << 15;

struct MetalSharpM12TranslationLayerInfo {
    UINT abi_version;
    UINT struct_size;
    UINT vendor_id;
    UINT layer_id;
    UINT64 feature_flags;
    UINT m12core_abi_version;
    UINT m12core_feature_flags;
    UINT m12core_build_id_low;
    UINT m12core_build_id_high;
    UINT reserved0;
    UINT64 reserved1[4];
    char layer_name[32];
    char backend_name[32];
    char build_string[64];
};

struct IMetalSharpM12TranslationLayerInfo : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMetalSharpM12TranslationLayerInfo(MetalSharpM12TranslationLayerInfo* info) = 0;
};

static std::string json_escape(const char* input) {
    std::string out;
    if (!input)
        return out;
    for (const char* p = input; *p; ++p) {
        switch (*p) {
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
            if (static_cast<unsigned char>(*p) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(*p));
                out += buf;
            } else {
                out += *p;
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

static void configure_exported_sdk() {
    std::string version_text = getenv_string("D3D12_METAL_SDK_AGILITY_VERSION");
    if (!version_text.empty())
        D3D12SDKVersion = static_cast<UINT>(std::strtoul(version_text.c_str(), nullptr, 10));

    std::string sdk_path = getenv_string("D3D12_METAL_SDK_AGILITY_PATH");
    if (!sdk_path.empty())
        std::snprintf(D3D12SDKPath, sizeof(D3D12SDKPath), "%s", sdk_path.c_str());
}

static void print_hr(const char* key, HRESULT hr) {
    std::printf("    \"%s\": \"0x%08lx\"", key, static_cast<unsigned long>(static_cast<uint32_t>(hr)));
}

int main() {
    configure_exported_sdk();
    std::string profile = getenv_string("D3D12_METAL_SDK_PROFILE");

    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    auto create_device = reinterpret_cast<CreateDeviceFn>(
        reinterpret_cast<void*>(d3d12 ? GetProcAddress(d3d12, "D3D12CreateDevice") : nullptr));

    ID3D12Device* device = nullptr;
    HRESULT create_hr = E_FAIL;
    HRESULT qi_hr = E_NOINTERFACE;
    HRESULT info_hr = E_FAIL;
    MetalSharpM12TranslationLayerInfo info = {};

    if (create_device)
        create_hr =
            create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_D3D12DeviceProbe, reinterpret_cast<void**>(&device));

    IMetalSharpM12TranslationLayerInfo* detection = nullptr;
    if (device) {
        qi_hr = device->QueryInterface(IID_IMetalSharpM12TranslationLayerInfo, reinterpret_cast<void**>(&detection));
        if (detection) {
            info_hr = detection->GetMetalSharpM12TranslationLayerInfo(&info);
            // This diagnostic exits immediately after printing JSON. Avoid
            // tearing down the runtime's worker primitives on Wine, where
            // device destruction can trip unrelated C++ runtime assertions.
        }
    }

    bool has_required_layer_features =
        (info.feature_flags & FeatureD3D12) && (info.feature_flags & FeatureDXMT) &&
        (info.feature_flags & FeatureLibM12Core) && (info.feature_flags & FeatureRootBindingPlans) &&
        (info.feature_flags & FeaturePrewarmPacks) && (info.feature_flags & FeatureDrawPlanning) &&
        (info.feature_flags & FeaturePresentPlanning) && (info.feature_flags & FeatureReplayPlanning) &&
        (info.feature_flags & FeatureCommandStreamDescriptors);
    bool pass =
        SUCCEEDED(create_hr) && SUCCEEDED(qi_hr) && SUCCEEDED(info_hr) &&
        info.abi_version == MetalSharpM12TranslationLayerInfoAbiVersion &&
        info.struct_size == sizeof(MetalSharpM12TranslationLayerInfo) &&
        info.vendor_id == MetalSharpM12TranslationLayerVendorMetalSharp &&
        info.layer_id == MetalSharpM12TranslationLayerIdDxmtM12 && info.m12core_abi_version == M12CORE_ABI_VERSION &&
        info.m12core_build_id_low == M12CORE_BUILD_ID_LOW && info.m12core_build_id_high == M12CORE_BUILD_ID_HIGH &&
        (info.m12core_feature_flags & M12CORE_FEATURE_DRAW_PLANNING) &&
        (info.m12core_feature_flags & M12CORE_FEATURE_PRESENT_PLANNING) &&
        (info.m12core_feature_flags & M12CORE_FEATURE_REPLAY_PLANNING) &&
        (info.m12core_feature_flags & M12CORE_FEATURE_COMMAND_STREAM_DESCRIPTORS) && has_required_layer_features;

    std::printf("{\n");
    std::printf("  \"schema\": \"metalsharp.d3d12-metal.probe-m12-detection.v1\",\n");
    std::printf("  \"profile\": \"%s\",\n", json_escape(profile.c_str()).c_str());
    std::printf("  \"pass\": %s,\n", pass ? "true" : "false");
    std::printf("  \"device_create\": {\n");
    print_hr("hr", create_hr);
    std::printf(",\n    \"succeeded\": %s\n  },\n", SUCCEEDED(create_hr) ? "true" : "false");
    std::printf("  \"query_interface\": {\n");
    print_hr("hr", qi_hr);
    std::printf(",\n    \"succeeded\": %s\n  },\n", SUCCEEDED(qi_hr) ? "true" : "false");
    std::printf("  \"info_call\": {\n");
    print_hr("hr", info_hr);
    std::printf(",\n    \"succeeded\": %s\n  },\n", SUCCEEDED(info_hr) ? "true" : "false");
    std::printf("  \"info\": {\n");
    std::printf("    \"abi_version\": %u,\n", info.abi_version);
    std::printf("    \"struct_size\": %u,\n", info.struct_size);
    std::printf("    \"expected_struct_size\": %zu,\n", sizeof(MetalSharpM12TranslationLayerInfo));
    std::printf("    \"vendor_id\": \"0x%08x\",\n", info.vendor_id);
    std::printf("    \"layer_id\": \"0x%08x\",\n", info.layer_id);
    std::printf("    \"feature_flags\": \"0x%016llx\",\n", static_cast<unsigned long long>(info.feature_flags));
    std::printf("    \"m12core_abi_version\": %u,\n", info.m12core_abi_version);
    std::printf("    \"m12core_feature_flags\": \"0x%08x\",\n", info.m12core_feature_flags);
    std::printf("    \"m12core_build_id_low\": \"0x%08x\",\n", info.m12core_build_id_low);
    std::printf("    \"m12core_build_id_high\": \"0x%08x\",\n", info.m12core_build_id_high);
    std::printf("    \"layer_name\": \"%s\",\n", json_escape(info.layer_name).c_str());
    std::printf("    \"backend_name\": \"%s\",\n", json_escape(info.backend_name).c_str());
    std::printf("    \"build_string\": \"%s\"\n", json_escape(info.build_string).c_str());
    std::printf("  }\n");
    std::printf("}\n");
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), pass ? 0 : 1);
}
