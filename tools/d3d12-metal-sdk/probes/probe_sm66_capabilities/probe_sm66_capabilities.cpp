#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <d3d12.h>

static const GUID IID_D3D12DeviceProbe = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};

using D3D12CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using D3D12SerializeRootSignatureFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION,
                                                       ID3DBlob**, ID3DBlob**);

template <typename T> static void safe_release(T*& object) {
    if (object) {
        object->Release();
        object = nullptr;
    }
}

template <typename T> static T load_proc(HMODULE module, const char* name) {
    T fn = nullptr;
    FARPROC proc = module ? GetProcAddress(module, name) : nullptr;
    static_assert(sizeof(fn) == sizeof(proc), "function pointer size mismatch");
    std::memcpy(&fn, &proc, sizeof(fn));
    return fn;
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

static std::string hr_hex(HRESULT hr) {
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%08lx", static_cast<unsigned long>(static_cast<uint32_t>(hr)));
    return buffer;
}

static bool write_text_file(const char* path, const char* text) {
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    DWORD size = static_cast<DWORD>(std::strlen(text));
    bool ok = WriteFile(file, text, size, &written, nullptr) && written == size;
    CloseHandle(file);
    return ok;
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

static std::vector<uint8_t> read_binary_file(const char* path) {
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return {};

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(file);
        return {};
    }

    std::vector<uint8_t> out(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    out.resize(read);
    CloseHandle(file);
    return out;
}

static DWORD run_process_wait(std::string command_line) {
    STARTUPINFOA startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    std::vector<char> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back('\0');
    BOOL created = CreateProcessA(nullptr, mutable_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                                  nullptr, &startup, &process);
    if (!created)
        return 0xffffffffu;

    WaitForSingleObject(process.hProcess, 30000);
    DWORD exit_code = 0xffffffffu;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exit_code;
}

struct AuditCase {
    const char* name;
    const char* entry;
    const char* category;
    bool requires_runtime_proof;
};

struct CaseResult {
    std::string name;
    std::string category;
    bool compile_ok = false;
    bool dxil_blob = false;
    bool pso_created = false;
    bool runtime_executed = false;
    HRESULT pso_hr = E_FAIL;
    DWORD dxc_exit_code = 0xffffffffu;
    size_t dxil_size = 0;
    std::string detail;
};

static HRESULT create_root_signature(ID3D12Device* device, D3D12SerializeRootSignatureFn serialize,
                                     ID3D12RootSignature** root, std::string& errors) {
    D3D12_DESCRIPTOR_RANGE ranges[3] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[1].NumDescriptors = 3;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 0;
    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    ranges[2].NumDescriptors = 1;
    ranges[2].BaseShaderRegister = 0;
    ranges[2].OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[4] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &ranges[0];
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &ranges[1];
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &ranges[2];
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[3].Constants.Num32BitValues = 4;
    params[3].Constants.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 4;
    desc.pParameters = params;

    ID3DBlob* blob = nullptr;
    ID3DBlob* error_blob = nullptr;
    HRESULT hr = serialize(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &error_blob);
    if (error_blob) {
        errors.assign(static_cast<const char*>(error_blob->GetBufferPointer()), error_blob->GetBufferSize());
        error_blob->Release();
    }
    if (SUCCEEDED(hr))
        hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(root));
    safe_release(blob);
    return hr;
}

static CaseResult run_case(ID3D12Device* device, ID3D12RootSignature* root, const AuditCase& audit_case) {
    CaseResult result;
    result.name = audit_case.name;
    result.category = audit_case.category;

    const std::string base = std::string("Z:\\tmp\\dxmt_sm66_") + audit_case.entry;
    const std::string dxil_path = base + ".dxil";
    const std::string error_path = base + ".err";
    DeleteFileA(dxil_path.c_str());
    DeleteFileA(error_path.c_str());

    std::string command = "dxc.exe -nologo -T cs_6_6 -E ";
    command += audit_case.entry;
    command += " -HV 2021 -Od -Fo ";
    command += dxil_path;
    command += " -Fe ";
    command += error_path;
    command += " Z:\\tmp\\dxmt_sm66_capabilities.hlsl";

    result.dxc_exit_code = run_process_wait(command);
    std::vector<uint8_t> dxil = read_binary_file(dxil_path.c_str());
    result.dxil_blob = !dxil.empty();
    result.dxil_size = dxil.size();
    result.compile_ok = result.dxc_exit_code == 0 && result.dxil_blob;

    if (result.compile_ok && device && root) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = root;
        desc.CS.pShaderBytecode = dxil.data();
        desc.CS.BytecodeLength = dxil.size();
        ID3D12PipelineState* pso = nullptr;
        result.pso_hr = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso));
        result.pso_created = SUCCEEDED(result.pso_hr) && pso;
        safe_release(pso);
    }

    std::string dxc_errors = read_text_file(error_path.c_str());
    if (!result.compile_ok)
        result.detail = dxc_errors.empty() ? "DXC did not produce a DXIL blob" : dxc_errors;
    else if (!result.pso_created)
        result.detail = "compiled to DXIL, but no linked compute PSO was created";
    else if (audit_case.requires_runtime_proof)
        result.detail = "compiled and linked; runtime execution proof is still required before SM 6.6 can be reported";
    else
        result.detail = "negative capability case recorded";

    return result;
}

int main() {
    const bool warmup_only = getenv_string("D3D12_METAL_SDK_SM66_MODE") == "warmup";
    const char* hlsl_path = "Z:\\tmp\\dxmt_sm66_capabilities.hlsl";
    const char* hlsl = R"(
RWByteAddressBuffer outbuf : register(u0);
ByteAddressBuffer inputs[2] : register(t0);
Texture2D<float4> tex : register(t2);
SamplerState smp : register(s0);

cbuffer RootConstants : register(b0) {
  uint selector;
  uint addend;
  uint multiplier;
  uint pad0;
};

groupshared uint group_counter;

[numthreads(4, 1, 1)]
void cs_root_constants(uint3 id : SV_DispatchThreadID) {
  outbuf.Store(id.x * 4, (id.x + addend) * multiplier);
}

[numthreads(4, 1, 1)]
void cs_descriptor_indexing(uint3 id : SV_DispatchThreadID) {
  uint descriptor_index = selector & 1u;
  outbuf.Store(id.x * 4, inputs[descriptor_index].Load(id.x * 4) + addend);
}

[numthreads(4, 1, 1)]
void cs_int64_arithmetic(uint3 id : SV_DispatchThreadID) {
  uint64_t wide = ((uint64_t)inputs[0].Load(id.x * 4) << 32) | (uint64_t)(id.x + addend);
  wide += 0x100000002ull;
  outbuf.Store(id.x * 8, (uint)(wide & 0xffffffffull));
  outbuf.Store(id.x * 8 + 4, (uint)(wide >> 32));
}

[numthreads(4, 1, 1)]
void cs_atomics_barriers(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  if (gi == 0)
    group_counter = 0;
  GroupMemoryBarrierWithGroupSync();
  uint original = 0;
  InterlockedAdd(group_counter, 1, original);
  GroupMemoryBarrierWithGroupSync();
  outbuf.Store(id.x * 4, group_counter + original);
}

[numthreads(4, 1, 1)]
void cs_texture_sampler(uint3 id : SV_DispatchThreadID) {
  float4 sample = tex.SampleLevel(smp, float2(0.5, 0.5), 0.0);
  uint total = (uint)(sample.r * 255.0 + 0.5) + (uint)(sample.g * 255.0 + 0.5) +
               (uint)(sample.b * 255.0 + 0.5) + (uint)(sample.a * 255.0 + 0.5);
  outbuf.Store(id.x * 4, total + id.x);
}
)";

    bool hlsl_written = write_text_file(hlsl_path, hlsl);

    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    HMODULE dxcompiler = LoadLibraryA("dxcompiler.dll");
    HMODULE dxil = LoadLibraryA("dxil.dll");
    auto create_device = load_proc<D3D12CreateDeviceFn>(d3d12, "D3D12CreateDevice");
    auto serialize = load_proc<D3D12SerializeRootSignatureFn>(d3d12, "D3D12SerializeRootSignature");

    ID3D12Device* device = nullptr;
    HRESULT create_hr = create_device ? create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_D3D12DeviceProbe,
                                                      reinterpret_cast<void**>(&device))
                                      : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);

    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS9 options9 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS11 options11 = {};
    HRESULT options1_hr =
        device ? device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1)) : E_FAIL;
    HRESULT options9_hr =
        device ? device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS9, &options9, sizeof(options9)) : E_FAIL;
    HRESULT options11_hr =
        device ? device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS11, &options11, sizeof(options11)) : E_FAIL;

    ID3D12RootSignature* root = nullptr;
    std::string root_errors;
    HRESULT root_hr = (device && serialize) ? create_root_signature(device, serialize, &root, root_errors)
                                            : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);

    const AuditCase audit_cases[] = {
        {"root_constants_uav", "cs_root_constants", "root_constants_cbv_srv_uav_tables", true},
        {"descriptor_indexing", "cs_descriptor_indexing", "descriptor_indexing", true},
        {"int64_arithmetic", "cs_int64_arithmetic", "64_bit_shader_arithmetic", true},
        {"atomics_barriers", "cs_atomics_barriers", "atomics_barriers", true},
        {"texture_sampler", "cs_texture_sampler", "samplers_texture_paths", true},
    };

    std::vector<CaseResult> results;
    if (hlsl_written) {
        for (const auto& audit_case : audit_cases)
            results.push_back(run_case(device, root, audit_case));
    }

    bool entrypoints_ok =
        d3d12 && dxcompiler && dxil && create_device && serialize && SUCCEEDED(create_hr) && SUCCEEDED(root_hr);
    bool compiler_acceptance_complete = hlsl_written && !results.empty();
    bool pso_link_complete = compiler_acceptance_complete;
    bool runtime_complete = false;
    for (const auto& result : results) {
        compiler_acceptance_complete = compiler_acceptance_complete && result.compile_ok;
        pso_link_complete = pso_link_complete && result.pso_created;
    }

    bool atomic64_conservative = (!SUCCEEDED(options9_hr) || (!options9.AtomicInt64OnTypedResourceSupported &&
                                                              !options9.AtomicInt64OnGroupSharedSupported)) &&
                                 (!SUCCEEDED(options11_hr) || !options11.AtomicInt64OnDescriptorHeapResourceSupported);
    bool sm66_reportable = compiler_acceptance_complete && pso_link_complete && runtime_complete;
    bool pass = entrypoints_ok && compiler_acceptance_complete && pso_link_complete && atomic64_conservative;

    std::printf("{\n");
    std::printf("  \"schema\": \"metalsharp.d3d12-metal.sm66-capabilities.v1\",\n");
    std::printf("  \"profile\": \"%s\",\n", json_escape(getenv_string("D3D12_METAL_SDK_PROFILE")).c_str());
    std::printf("  \"mode\": \"%s\",\n", warmup_only ? "warmup" : "audit");
    std::printf("  \"pass\": %s,\n", pass ? "true" : "false");
    std::printf("  \"entrypoints\": {\n");
    std::printf("    \"d3d12_loaded\": %s,\n", d3d12 ? "true" : "false");
    std::printf("    \"dxcompiler_loaded\": %s,\n", dxcompiler ? "true" : "false");
    std::printf("    \"dxil_loaded\": %s,\n", dxil ? "true" : "false");
    std::printf("    \"D3D12CreateDevice\": %s,\n", create_device ? "true" : "false");
    std::printf("    \"D3D12SerializeRootSignature\": %s,\n", serialize ? "true" : "false");
    std::printf("    \"complete\": %s\n", entrypoints_ok ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"device\": {\n");
    std::printf("    \"create_hr\": \"%s\",\n", hr_hex(create_hr).c_str());
    std::printf("    \"root_signature_hr\": \"%s\"\n", hr_hex(root_hr).c_str());
    std::printf("  },\n");
    std::printf("  \"feature_negatives\": {\n");
    std::printf("    \"options1_hr\": \"%s\",\n", hr_hex(options1_hr).c_str());
    std::printf("    \"int64_shader_ops_reported\": %s,\n", options1.Int64ShaderOps ? "true" : "false");
    std::printf("    \"options9_hr\": \"%s\",\n", hr_hex(options9_hr).c_str());
    std::printf("    \"atomic64_typed_resource_reported\": %s,\n",
                options9.AtomicInt64OnTypedResourceSupported ? "true" : "false");
    std::printf("    \"atomic64_group_shared_reported\": %s,\n",
                options9.AtomicInt64OnGroupSharedSupported ? "true" : "false");
    std::printf("    \"options11_hr\": \"%s\",\n", hr_hex(options11_hr).c_str());
    std::printf("    \"atomic64_descriptor_heap_reported\": %s,\n",
                options11.AtomicInt64OnDescriptorHeapResourceSupported ? "true" : "false");
    std::printf("    \"atomic64_conservative\": %s\n", atomic64_conservative ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"summary\": {\n");
    std::printf("    \"compiler_acceptance_complete\": %s,\n", compiler_acceptance_complete ? "true" : "false");
    std::printf("    \"pso_link_complete\": %s,\n", pso_link_complete ? "true" : "false");
    std::printf("    \"runtime_correctness_complete\": %s,\n", runtime_complete ? "true" : "false");
    std::printf("    \"sm66_reportable\": %s,\n", sm66_reportable ? "true" : "false");
    std::printf("    \"decision\": \"%s\"\n",
                sm66_reportable ? "SM 6.6 may be reported" : "SM 6.6 must not be reported until runtime cases execute");
    std::printf("  },\n");
    std::printf("  \"cases\": [\n");
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        std::printf("    {\n");
        std::printf("      \"name\": \"%s\",\n", json_escape(result.name).c_str());
        std::printf("      \"category\": \"%s\",\n", json_escape(result.category).c_str());
        std::printf("      \"compile_ok\": %s,\n", result.compile_ok ? "true" : "false");
        std::printf("      \"dxil_blob\": %s,\n", result.dxil_blob ? "true" : "false");
        std::printf("      \"dxil_size\": %zu,\n", result.dxil_size);
        std::printf("      \"dxc_exit_code\": %lu,\n", static_cast<unsigned long>(result.dxc_exit_code));
        std::printf("      \"pso_created\": %s,\n", result.pso_created ? "true" : "false");
        std::printf("      \"pso_hr\": \"%s\",\n", hr_hex(result.pso_hr).c_str());
        std::printf("      \"runtime_executed\": %s,\n", result.runtime_executed ? "true" : "false");
        std::printf("      \"detail\": \"%s\"\n", json_escape(result.detail).c_str());
        std::printf("    }%s\n", i + 1 == results.size() ? "" : ",");
    }
    std::printf("  ]\n");
    std::printf("}\n");

    safe_release(root);
    safe_release(device);
    std::fflush(stdout);
    return pass ? 0 : 1;
}
