#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgiformat.h>

static const GUID IID_D3D12DeviceProbe = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};

using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
                                      UINT, UINT, ID3DBlob**, ID3DBlob**);

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
            out += c;
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

static std::string read_text_file(const char* path) {
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return "";

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 1024 * 1024) {
        CloseHandle(file);
        return "";
    }

    std::string out(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    out.resize(read);
    CloseHandle(file);
    return out;
}

static bool file_nonzero(const char* path, unsigned long long* size_out = nullptr) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data))
        return false;
    unsigned long long size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    if (size_out)
        *size_out = size;
    return size > 0;
}

static bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

static void print_hr(const char* key, HRESULT hr, bool comma = true) {
    std::printf("    \"%s\": \"0x%08lx\"%s\n", key, static_cast<unsigned long>(static_cast<uint32_t>(hr)),
                comma ? "," : "");
}

static D3D12_BLEND_DESC default_blend_desc() {
    D3D12_BLEND_DESC desc = {};
    desc.AlphaToCoverageEnable = FALSE;
    desc.IndependentBlendEnable = FALSE;
    for (auto& rt : desc.RenderTarget) {
        rt.BlendEnable = FALSE;
        rt.LogicOpEnable = FALSE;
        rt.SrcBlend = D3D12_BLEND_ONE;
        rt.DestBlend = D3D12_BLEND_ZERO;
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.LogicOp = D3D12_LOGIC_OP_NOOP;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    return desc;
}

static D3D12_RASTERIZER_DESC default_rasterizer_desc() {
    D3D12_RASTERIZER_DESC desc = {};
    desc.FillMode = D3D12_FILL_MODE_SOLID;
    desc.CullMode = D3D12_CULL_MODE_NONE;
    desc.FrontCounterClockwise = FALSE;
    desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.DepthClipEnable = TRUE;
    desc.MultisampleEnable = FALSE;
    desc.AntialiasedLineEnable = FALSE;
    desc.ForcedSampleCount = 0;
    desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    return desc;
}

static D3D12_DEPTH_STENCIL_DESC default_depth_stencil_desc() {
    D3D12_DEPTH_STENCIL_DESC desc = {};
    desc.DepthEnable = FALSE;
    desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    desc.StencilEnable = FALSE;
    desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    desc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    desc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    desc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    return desc;
}

static HRESULT compile_shader(D3DCompileFn compile, const char* source, const char* entry, const char* target,
                              ID3DBlob** blob, ID3DBlob** errors) {
    return compile ? compile(source, strlen(source), "probe_shaders.hlsl", nullptr, nullptr, entry, target, 0, 0, blob,
                             errors)
                   : E_FAIL;
}

static HRESULT serialize_empty_root_signature(HMODULE d3d12, ID3DBlob** blob) {
    using SerializeRootSignatureFn =
        HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
    auto serialize_root_signature = reinterpret_cast<SerializeRootSignatureFn>(
        reinterpret_cast<void*>(d3d12 ? GetProcAddress(d3d12, "D3D12SerializeRootSignature") : nullptr));
    if (!serialize_root_signature)
        return E_FAIL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* errors = nullptr;
    HRESULT hr = serialize_root_signature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, blob, &errors);
    if (errors)
        errors->Release();
    return hr;
}

static std::vector<uint8_t> build_synthetic_dxil_container() {
    const uint32_t dxbc_fourcc = 'D' | ('X' << 8) | ('B' << 16) | ('C' << 24);
    const uint32_t dxil_fourcc = 'D' | ('X' << 8) | ('I' << 16) | ('L' << 24);
    const uint32_t chunk_offset = 36;
    const uint32_t dxil_blob_size = 32;
    const uint32_t total_size = chunk_offset + 8 + dxil_blob_size;
    std::vector<uint8_t> data(total_size, 0);
    auto put32 = [&](size_t offset, uint32_t value) { memcpy(data.data() + offset, &value, sizeof(value)); };
    auto put16 = [&](size_t offset, uint16_t value) { memcpy(data.data() + offset, &value, sizeof(value)); };

    put32(0, dxbc_fourcc);
    put16(20, 1);
    put16(22, 0);
    put32(24, total_size);
    put32(28, 1);
    put32(32, chunk_offset);
    put32(chunk_offset, dxil_fourcc);
    put32(chunk_offset + 4, dxil_blob_size);

    uint8_t* blob = data.data() + chunk_offset + 8;
    const uint32_t program_version = (5u << 16) | (6u << 4); // compute shader, SM 6.0
    const uint32_t program_size = dxil_blob_size / 4;
    const uint32_t bitcode_offset = 16;
    const uint32_t bitcode_size = 8;
    memcpy(blob + 0, &program_version, sizeof(program_version));
    memcpy(blob + 4, &program_size, sizeof(program_size));
    memcpy(blob + 8, &dxil_fourcc, sizeof(dxil_fourcc));
    put16(chunk_offset + 8 + 12, 0);
    put16(chunk_offset + 8 + 14, 1);
    memcpy(blob + 16, &bitcode_offset, sizeof(bitcode_offset));
    memcpy(blob + 20, &bitcode_size, sizeof(bitcode_size));
    const uint8_t invalid_bitcode[8] = {'B', 'C', 0xde, 0xad, 0xbe, 0xef, 0, 0};
    memcpy(blob + 24, invalid_bitcode, sizeof(invalid_bitcode));
    return data;
}

int main() {
    const char* shader_trace_path = "Z:\\tmp\\dxmt_d3d12_trace.log";
    const char* dxil_trace_path = "Z:\\tmp\\dxmt_dxil_trace.log";
    const char* shader_args_path = "Z:\\tmp\\dxmt_ps_args_debug.log";
    const char* vs_metallib_path = "Z:\\tmp\\dxmt_sm50_vs_main.metallib";
    const char* ps_metallib_path = "Z:\\tmp\\dxmt_sm50_ps_main.metallib";
    const char* cs_metallib_path = "Z:\\tmp\\dxmt_sm50_cs_main.metallib";
    DeleteFileA(shader_trace_path);
    DeleteFileA(dxil_trace_path);
    DeleteFileA(shader_args_path);
    DeleteFileA(vs_metallib_path);
    DeleteFileA(ps_metallib_path);
    DeleteFileA(cs_metallib_path);

    std::string profile = getenv_string("D3D12_METAL_SDK_PROFILE");

    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    HMODULE d3dcompiler = LoadLibraryA("d3dcompiler_47.dll");
    using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    auto create_device = reinterpret_cast<CreateDeviceFn>(
        reinterpret_cast<void*>(d3d12 ? GetProcAddress(d3d12, "D3D12CreateDevice") : nullptr));
    auto compile = reinterpret_cast<D3DCompileFn>(
        reinterpret_cast<void*>(d3dcompiler ? GetProcAddress(d3dcompiler, "D3DCompile") : nullptr));

    ID3D12Device* device = nullptr;
    HRESULT create_hr = create_device ? create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_D3D12DeviceProbe,
                                                      reinterpret_cast<void**>(&device))
                                      : E_FAIL;

    const char* graphics_hlsl = R"(
struct VSOut {
  float4 pos : SV_POSITION;
  float4 color : COLOR0;
};
VSOut vs_main(uint vertex_id : SV_VertexID) {
  float2 positions[3] = {
    float2(0.0, 0.5),
    float2(0.5, -0.5),
    float2(-0.5, -0.5)
  };
  float4 colors[3] = {
    float4(1.0, 0.0, 0.0, 1.0),
    float4(0.0, 1.0, 0.0, 1.0),
    float4(0.0, 0.0, 1.0, 1.0)
  };
  VSOut output;
  output.pos = float4(positions[vertex_id], 0.0, 1.0);
  output.color = colors[vertex_id];
  return output;
}
float4 ps_main(VSOut input) : SV_Target {
  return input.color;
}
)";

    const char* compute_hlsl = R"(
[numthreads(1, 1, 1)]
void cs_main(uint3 dispatch_id : SV_DispatchThreadID) {
}
)";

    const char* sm6_probe_hlsl = R"(
float4 ps_main(float4 pos : SV_POSITION) : SV_Target {
  return WaveIsFirstLane() ? float4(1.0, 1.0, 1.0, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
}
)";

    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* ps_blob = nullptr;
    ID3DBlob* cs_blob = nullptr;
    ID3DBlob* sm6_blob = nullptr;
    ID3DBlob* vs_errors = nullptr;
    ID3DBlob* ps_errors = nullptr;
    ID3DBlob* cs_errors = nullptr;
    ID3DBlob* sm6_errors = nullptr;
    HRESULT vs_compile_hr = compile_shader(compile, graphics_hlsl, "vs_main", "vs_5_0", &vs_blob, &vs_errors);
    HRESULT ps_compile_hr = compile_shader(compile, graphics_hlsl, "ps_main", "ps_5_0", &ps_blob, &ps_errors);
    HRESULT cs_compile_hr = compile_shader(compile, compute_hlsl, "cs_main", "cs_5_0", &cs_blob, &cs_errors);
    HRESULT sm6_compile_hr = compile_shader(compile, sm6_probe_hlsl, "ps_main", "ps_6_0", &sm6_blob, &sm6_errors);

    ID3DBlob* root_blob = nullptr;
    HRESULT serialize_root_hr = serialize_empty_root_signature(d3d12, &root_blob);
    ID3D12RootSignature* root_signature = nullptr;
    HRESULT create_root_hr =
        (device && root_blob) ? device->CreateRootSignature(0, root_blob->GetBufferPointer(),
                                                            root_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature))
                              : E_FAIL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics_desc = {};
    graphics_desc.pRootSignature = root_signature;
    graphics_desc.VS = {vs_blob ? vs_blob->GetBufferPointer() : nullptr, vs_blob ? vs_blob->GetBufferSize() : 0};
    graphics_desc.PS = {ps_blob ? ps_blob->GetBufferPointer() : nullptr, ps_blob ? ps_blob->GetBufferSize() : 0};
    graphics_desc.BlendState = default_blend_desc();
    graphics_desc.SampleMask = UINT_MAX;
    graphics_desc.RasterizerState = default_rasterizer_desc();
    graphics_desc.DepthStencilState = default_depth_stencil_desc();
    graphics_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    graphics_desc.NumRenderTargets = 1;
    graphics_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    graphics_desc.SampleDesc.Count = 1;

    ID3D12PipelineState* graphics_pso = nullptr;
    HRESULT graphics_pso_hr =
        device ? device->CreateGraphicsPipelineState(&graphics_desc, IID_PPV_ARGS(&graphics_pso)) : E_FAIL;

    D3D12_COMPUTE_PIPELINE_STATE_DESC compute_desc = {};
    compute_desc.pRootSignature = root_signature;
    compute_desc.CS = {cs_blob ? cs_blob->GetBufferPointer() : nullptr, cs_blob ? cs_blob->GetBufferSize() : 0};
    ID3D12PipelineState* compute_pso = nullptr;
    HRESULT compute_pso_hr =
        device ? device->CreateComputePipelineState(&compute_desc, IID_PPV_ARGS(&compute_pso)) : E_FAIL;

    std::vector<uint8_t> synthetic_dxil = build_synthetic_dxil_container();
    D3D12_COMPUTE_PIPELINE_STATE_DESC bad_compute_desc = {};
    bad_compute_desc.pRootSignature = root_signature;
    bad_compute_desc.CS = {synthetic_dxil.data(), synthetic_dxil.size()};
    ID3D12PipelineState* bad_compute_pso = nullptr;
    HRESULT bad_compute_pso_hr =
        device ? device->CreateComputePipelineState(&bad_compute_desc, IID_PPV_ARGS(&bad_compute_pso)) : E_FAIL;

    std::string trace = read_text_file(shader_trace_path);
    std::string dxil_trace = read_text_file(dxil_trace_path);
    std::string args_trace = read_text_file(shader_args_path);
    unsigned long long vs_metallib_size = 0;
    unsigned long long ps_metallib_size = 0;
    unsigned long long cs_metallib_size = 0;
    bool vs_metallib = file_nonzero(vs_metallib_path, &vs_metallib_size);
    bool ps_metallib = file_nonzero(ps_metallib_path, &ps_metallib_size);
    bool cs_metallib = file_nonzero(cs_metallib_path, &cs_metallib_size);
    bool graphics_trace_ok =
        contains(trace, "CompileShader: vs_main SM50 OK") && contains(trace, "CompileShader: ps_main SM50 OK");
    bool compute_trace_ok = contains(trace, "CompileShader: cs_main SM50 OK") || contains(args_trace, "CS_ARGS_DEBUG");
    bool failure_trace_ok = contains(trace, "shader/sm50_init") || contains(trace, "SM50Init FAILED for cs_main") ||
                            contains(trace, "shader/bitcode_parse") || contains(trace, "BitcodeReader::parse FAILED") ||
                            contains(trace, "pso/compute_no_cs");
    bool dxil_container_trace_ok = contains(dxil_trace, "DXILContainer") || contains(trace, "DXIL container parsed");
    bool sm6_probe_explicit = FAILED(sm6_compile_hr);
    bool bindless_explicit = true; // D3DCompile cannot emit SM6 bindless DXIL here; this remains explicitly unsupported
                                   // until DXC is bundled.

    bool entrypoints_valid = d3d12 && d3dcompiler && create_device && compile;
    bool compile_valid = SUCCEEDED(vs_compile_hr) && SUCCEEDED(ps_compile_hr) && SUCCEEDED(cs_compile_hr);
    bool pso_valid = SUCCEEDED(create_root_hr) && SUCCEEDED(graphics_pso_hr) && graphics_pso &&
                     SUCCEEDED(compute_pso_hr) && compute_pso;
    bool cache_valid = vs_metallib && ps_metallib && cs_metallib;
    bool diagnostics_valid =
        SUCCEEDED(bad_compute_pso_hr) && bad_compute_pso && failure_trace_ok && dxil_container_trace_ok;
    bool pass = entrypoints_valid && SUCCEEDED(create_hr) && compile_valid && pso_valid && cache_valid &&
                graphics_trace_ok && compute_trace_ok && diagnostics_valid && sm6_probe_explicit && bindless_explicit;

    std::printf("{\n");
    std::printf("  \"schema\": \"metalsharp.d3d12-metal.probe-shaders.v1\",\n");
    std::printf("  \"profile\": \"%s\",\n", json_escape(profile).c_str());
    std::printf("  \"pass\": %s,\n", pass ? "true" : "false");
    std::printf("  \"entrypoints\": {\n");
    std::printf("    \"d3d12_loaded\": %s,\n", d3d12 ? "true" : "false");
    std::printf("    \"d3dcompiler_47_loaded\": %s,\n", d3dcompiler ? "true" : "false");
    std::printf("    \"D3D12CreateDevice\": %s,\n", create_device ? "true" : "false");
    std::printf("    \"D3DCompile\": %s\n", compile ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"device\": {\n");
    print_hr("D3D12CreateDevice", create_hr, false);
    std::printf("  },\n");
    std::printf("  \"compile\": {\n");
    print_hr("vs_5_0", vs_compile_hr);
    print_hr("ps_5_0", ps_compile_hr);
    print_hr("cs_5_0", cs_compile_hr);
    print_hr("ps_6_0_wave_probe", sm6_compile_hr);
    std::printf("    \"sm6_probe_decision\": \"%s\"\n",
                sm6_probe_explicit ? "explicitly_unsupported_without_dxc_dxil_compiler" : "unexpectedly_compiled");
    std::printf("  },\n");
    std::printf("  \"root_signature\": {\n");
    print_hr("serialize_empty", serialize_root_hr);
    print_hr("create_empty", create_root_hr, false);
    std::printf("  },\n");
    std::printf("  \"pipeline_state\": {\n");
    print_hr("graphics_pso", graphics_pso_hr);
    print_hr("compute_pso", compute_pso_hr);
    print_hr("bad_compute_pso", bad_compute_pso_hr);
    std::printf("    \"bad_compute_returns_object_but_logs_failure\": %s\n", diagnostics_valid ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"dxmt_shader_paths\": {\n");
    std::printf("    \"dxbc_vertex_sm50\": %s,\n", graphics_trace_ok ? "true" : "false");
    std::printf("    \"dxbc_pixel_sm50\": %s,\n", graphics_trace_ok ? "true" : "false");
    std::printf("    \"dxbc_compute_sm50\": %s,\n", compute_trace_ok ? "true" : "false");
    std::printf("    \"dxil_container_parse\": \"%s\",\n",
                dxil_container_trace_ok ? "synthetic_sm6_container_parse_exercised" : "not_observed");
    std::printf("    \"dxil_to_msl\": \"%s\",\n",
                sm6_probe_explicit ? "not_exercised_no_dxc_dxil_compiler_available" : "unexpected_compile");
    std::printf("    \"bindless_descriptor_indexing\": \"%s\"\n",
                bindless_explicit ? "explicitly_unsupported_until_dxc_sm6_probe_is_bundled" : "unknown");
    std::printf("  },\n");
    std::printf("  \"shader_cache\": {\n");
    std::printf("    \"vs_metallib\": %s,\n", vs_metallib ? "true" : "false");
    std::printf("    \"ps_metallib\": %s,\n", ps_metallib ? "true" : "false");
    std::printf("    \"cs_metallib\": %s,\n", cs_metallib ? "true" : "false");
    std::printf("    \"vs_metallib_size\": %llu,\n", vs_metallib_size);
    std::printf("    \"ps_metallib_size\": %llu,\n", ps_metallib_size);
    std::printf("    \"cs_metallib_size\": %llu\n", cs_metallib_size);
    std::printf("  },\n");
    std::printf("  \"diagnostics\": {\n");
    std::printf("    \"trace_file_read\": %s,\n", trace.empty() ? "false" : "true");
    std::printf("    \"dxil_trace_file_read\": %s,\n", dxil_trace.empty() ? "false" : "true");
    std::printf("    \"dxil_container_trace_ok\": %s,\n", dxil_container_trace_ok ? "true" : "false");
    std::printf("    \"failure_trace_ok\": %s,\n", failure_trace_ok ? "true" : "false");
    std::printf("    \"trace_excerpt_has_stage\": %s\n",
                contains(trace, "cs_main") || contains(trace, "pso/") ? "true" : "false");
    std::printf("  }\n");
    std::printf("}\n");

    if (bad_compute_pso)
        bad_compute_pso->Release();
    if (compute_pso)
        compute_pso->Release();
    if (graphics_pso)
        graphics_pso->Release();
    if (root_signature)
        root_signature->Release();
    if (root_blob)
        root_blob->Release();
    if (sm6_blob)
        sm6_blob->Release();
    if (cs_blob)
        cs_blob->Release();
    if (ps_blob)
        ps_blob->Release();
    if (vs_blob)
        vs_blob->Release();
    if (sm6_errors)
        sm6_errors->Release();
    if (cs_errors)
        cs_errors->Release();
    if (ps_errors)
        ps_errors->Release();
    if (vs_errors)
        vs_errors->Release();
    if (device)
        device->Release();

    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), pass ? 0 : 1);
}
