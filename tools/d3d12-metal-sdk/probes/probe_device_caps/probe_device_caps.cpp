#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <d3d12.h>

extern "C" __declspec(dllexport) const UINT D3D12SDKVersion = 619;
extern "C" __declspec(dllexport) const char D3D12SDKPath[] = ".\\D3D12\\";

static const GUID IID_D3D12DeviceProbe = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};

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

static std::string feature_level_name(D3D_FEATURE_LEVEL level) {
    switch (level) {
    case D3D_FEATURE_LEVEL_12_2:
        return "12_2";
    case D3D_FEATURE_LEVEL_12_1:
        return "12_1";
    case D3D_FEATURE_LEVEL_12_0:
        return "12_0";
    case D3D_FEATURE_LEVEL_11_1:
        return "11_1";
    case D3D_FEATURE_LEVEL_11_0:
        return "11_0";
    default:
        return "unknown";
    }
}

static std::string shader_model_name(D3D_SHADER_MODEL model) {
    switch (model) {
    case D3D_SHADER_MODEL_6_7:
        return "6_7";
    case D3D_SHADER_MODEL_6_6:
        return "6_6";
    case D3D_SHADER_MODEL_6_5:
        return "6_5";
    case D3D_SHADER_MODEL_6_4:
        return "6_4";
    case D3D_SHADER_MODEL_6_3:
        return "6_3";
    case D3D_SHADER_MODEL_6_2:
        return "6_2";
    case D3D_SHADER_MODEL_6_1:
        return "6_1";
    case D3D_SHADER_MODEL_6_0:
        return "6_0";
    case D3D_SHADER_MODEL_5_1:
        return "5_1";
    default:
        return "unknown";
    }
}

static void print_hr(const char* key, HRESULT hr) {
    std::printf("    \"%s_hr\": \"0x%08lx\",\n", key, static_cast<unsigned long>(static_cast<uint32_t>(hr)));
}

int main() {
    std::string profile = getenv_string("D3D12_METAL_SDK_PROFILE");

    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    FARPROC create_device_proc = d3d12 ? GetProcAddress(d3d12, "D3D12CreateDevice") : nullptr;
    auto create_device = reinterpret_cast<CreateDeviceFn>(reinterpret_cast<void*>(create_device_proc));

    ID3D12Device* device = nullptr;
    HRESULT create_hr = E_FAIL;
    if (create_device)
        create_hr =
            create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_D3D12DeviceProbe, reinterpret_cast<void**>(&device));

    D3D_FEATURE_LEVEL requested_levels[] = {
        D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
    };
    D3D12_FEATURE_DATA_FEATURE_LEVELS feature_levels = {
        static_cast<UINT>(sizeof(requested_levels) / sizeof(requested_levels[0])), requested_levels,
        D3D_FEATURE_LEVEL_11_0};
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model = {D3D_SHADER_MODEL_6_7};
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS2 options2 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS9 options9 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS11 options11 = {};

    HRESULT fl_hr = E_FAIL;
    HRESULT sm_hr = E_FAIL;
    HRESULT options_hr = E_FAIL;
    HRESULT options1_hr = E_FAIL;
    HRESULT options2_hr = E_FAIL;
    HRESULT options3_hr = E_FAIL;
    HRESULT options5_hr = E_FAIL;
    HRESULT options7_hr = E_FAIL;
    HRESULT options9_hr = E_FAIL;
    HRESULT options11_hr = E_FAIL;

    if (device) {
        fl_hr = device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &feature_levels, sizeof(feature_levels));
        sm_hr = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model));
        options_hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
        options1_hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1));
        options2_hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2, &options2, sizeof(options2));
        options3_hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &options3, sizeof(options3));
        options5_hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5));
        options7_hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));
        options9_hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS9, &options9, sizeof(options9));
        options11_hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS11, &options11, sizeof(options11));
    }

    bool feature_level_ok = SUCCEEDED(fl_hr) && feature_levels.MaxSupportedFeatureLevel >= D3D_FEATURE_LEVEL_12_0;
    bool shader_model_ok = SUCCEEDED(sm_hr) && shader_model.HighestShaderModel >= D3D_SHADER_MODEL_6_6;
    bool binding_tier_ok = SUCCEEDED(options_hr) && options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3;
    bool wave_ops_ok = SUCCEEDED(options1_hr) && options1.WaveOps;
    bool atomic64_conservative = (!SUCCEEDED(options9_hr) || (!options9.AtomicInt64OnTypedResourceSupported &&
                                                              !options9.AtomicInt64OnGroupSharedSupported)) &&
                                 (!SUCCEEDED(options11_hr) || !options11.AtomicInt64OnDescriptorHeapResourceSupported);
    bool advanced_conservative =
        (!SUCCEEDED(options5_hr) || options5.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) &&
        (!SUCCEEDED(options7_hr) || (options7.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED &&
                                     options7.SamplerFeedbackTier == D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED)) &&
        (!SUCCEEDED(options9_hr) || options9.WaveMMATier == D3D12_WAVE_MMA_TIER_NOT_SUPPORTED);
    bool pass = SUCCEEDED(create_hr) && feature_level_ok && shader_model_ok && binding_tier_ok && wave_ops_ok &&
                atomic64_conservative && advanced_conservative;

    std::printf("{\n");
    std::printf("  \"schema\": \"metalsharp.d3d12-metal.probe-device-caps.v1\",\n");
    std::printf("  \"profile\": \"%s\",\n", json_escape(profile).c_str());
    std::printf("  \"pass\": %s,\n", pass ? "true" : "false");
    std::printf("  \"sdk\": {\"D3D12SDKVersion\": %u, \"D3D12SDKPath\": \"%s\"},\n", D3D12SDKVersion,
                json_escape(D3D12SDKPath).c_str());
    std::printf("  \"device_create\": {\n");
    std::printf("    \"hr\": \"0x%08lx\",\n", static_cast<unsigned long>(static_cast<uint32_t>(create_hr)));
    std::printf("    \"succeeded\": %s\n", SUCCEEDED(create_hr) ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"feature_levels\": {\n");
    print_hr("check", fl_hr);
    std::printf("    \"max_supported\": \"%s\",\n",
                feature_level_name(feature_levels.MaxSupportedFeatureLevel).c_str());
    std::printf("    \"meets_12_0\": %s\n", feature_level_ok ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"shader_model\": {\n");
    print_hr("check", sm_hr);
    std::printf("    \"highest\": \"%s\",\n", shader_model_name(shader_model.HighestShaderModel).c_str());
    std::printf("    \"meets_6_6\": %s\n", shader_model_ok ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"options\": {\n");
    print_hr("check", options_hr);
    std::printf("    \"resource_binding_tier\": %u,\n", static_cast<unsigned>(options.ResourceBindingTier));
    std::printf("    \"resource_heap_tier\": %u,\n", static_cast<unsigned>(options.ResourceHeapTier));
    std::printf("    \"rovs_supported\": %s,\n", options.ROVsSupported ? "true" : "false");
    std::printf("    \"conservative_rasterization_tier\": %u\n",
                static_cast<unsigned>(options.ConservativeRasterizationTier));
    std::printf("  },\n");
    std::printf("  \"options1\": {\n");
    print_hr("check", options1_hr);
    std::printf("    \"wave_ops\": %s,\n", options1.WaveOps ? "true" : "false");
    std::printf("    \"wave_lane_count_min\": %u,\n", options1.WaveLaneCountMin);
    std::printf("    \"wave_lane_count_max\": %u,\n", options1.WaveLaneCountMax);
    std::printf("    \"int64_shader_ops\": %s\n", options1.Int64ShaderOps ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"advanced_features\": {\n");
    print_hr("options2", options2_hr);
    print_hr("options3", options3_hr);
    print_hr("options5", options5_hr);
    std::printf("    \"raytracing_tier\": %u,\n", static_cast<unsigned>(options5.RaytracingTier));
    print_hr("options7", options7_hr);
    std::printf("    \"mesh_shader_tier\": %u,\n", static_cast<unsigned>(options7.MeshShaderTier));
    std::printf("    \"sampler_feedback_tier\": %u,\n", static_cast<unsigned>(options7.SamplerFeedbackTier));
    print_hr("options9", options9_hr);
    std::printf("    \"atomic64_typed_resource\": %s,\n",
                options9.AtomicInt64OnTypedResourceSupported ? "true" : "false");
    std::printf("    \"atomic64_group_shared\": %s,\n", options9.AtomicInt64OnGroupSharedSupported ? "true" : "false");
    std::printf("    \"wave_mma_tier\": %u,\n", static_cast<unsigned>(options9.WaveMMATier));
    print_hr("options11", options11_hr);
    std::printf("    \"atomic64_descriptor_heap_resource\": %s\n",
                options11.AtomicInt64OnDescriptorHeapResourceSupported ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"requirements\": {\n");
    std::printf("    \"feature_level_12_0_or_better\": %s,\n", feature_level_ok ? "true" : "false");
    std::printf("    \"shader_model_6_6_or_better\": %s,\n", shader_model_ok ? "true" : "false");
    std::printf("    \"binding_tier_3\": %s,\n", binding_tier_ok ? "true" : "false");
    std::printf("    \"wave_ops\": %s,\n", wave_ops_ok ? "true" : "false");
    std::printf("    \"atomic64_conservative\": %s,\n", atomic64_conservative ? "true" : "false");
    std::printf("    \"advanced_features_conservative\": %s\n", advanced_conservative ? "true" : "false");
    std::printf("  }\n");
    std::printf("}\n");

    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), pass ? 0 : 1);
}
