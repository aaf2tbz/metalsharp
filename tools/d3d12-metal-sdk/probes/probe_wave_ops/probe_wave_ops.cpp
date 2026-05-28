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

struct WaveCase {
    const char* name;
    const char* entry;
    const char* target;
};

struct CaseResult {
    const char* name = "";
    DWORD dxc_exit_code = 0xffffffffu;
    size_t dxil_size = 0;
    HRESULT pso_hr = E_FAIL;
    bool compile_ok = false;
    bool dxil_blob = false;
    bool pso_created = false;
};

static HRESULT create_root_signature(ID3D12Device* device, D3D12SerializeRootSignatureFn serialize,
                                     ID3D12RootSignature** root, std::string& errors) {
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 1;
    param.DescriptorTable.pDescriptorRanges = &range;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &param;

    ID3DBlob* blob = nullptr;
    ID3DBlob* error_blob = nullptr;
    HRESULT hr = serialize ? serialize(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &error_blob) : E_FAIL;
    if (error_blob) {
        errors.assign(static_cast<const char*>(error_blob->GetBufferPointer()), error_blob->GetBufferSize());
        error_blob->Release();
    }
    if (SUCCEEDED(hr))
        hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(root));
    safe_release(blob);
    return hr;
}

static CaseResult run_case(ID3D12Device* device, ID3D12RootSignature* root, const WaveCase& wave_case) {
    CaseResult result;
    result.name = wave_case.name;

    char dxil_path[128] = {};
    std::snprintf(dxil_path, sizeof(dxil_path), "Z:\\tmp\\dxmt_wave_%s.dxil", wave_case.name);
    DeleteFileA(dxil_path);

    std::string command = "dxc.exe -nologo -HV 2021 -Od -E ";
    command += wave_case.entry;
    command += " -T ";
    command += wave_case.target;
    command += " -Fo ";
    command += dxil_path;
    command += " Z:\\tmp\\dxmt_wave_ops.hlsl";
    result.dxc_exit_code = run_process_wait(command);

    std::vector<uint8_t> dxil = read_binary_file(dxil_path);
    result.dxil_size = dxil.size();
    result.compile_ok = result.dxc_exit_code == 0;
    result.dxil_blob = !dxil.empty();

    if (device && root && !dxil.empty()) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = root;
        desc.CS = {dxil.data(), dxil.size()};
        ID3D12PipelineState* pso = nullptr;
        result.pso_hr = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso));
        result.pso_created = SUCCEEDED(result.pso_hr) && pso;
        safe_release(pso);
    }

    return result;
}

int main() {
    std::string profile = getenv_string("D3D12_METAL_SDK_PROFILE");

    const char* hlsl = R"(
RWByteAddressBuffer outbuf : register(u0);

[numthreads(32, 1, 1)]
void cs_lane_index_count(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  outbuf.Store(gi * 4, WaveGetLaneIndex() + WaveGetLaneCount());
}

[numthreads(32, 1, 1)]
void cs_active_ballot(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  uint4 ballot = WaveActiveBallot((gi & 1u) == 0u);
  outbuf.Store(gi * 4, ballot.x);
}

[numthreads(32, 1, 1)]
void cs_read_lane(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  uint first = WaveReadLaneFirst(gi + 7u);
  uint at = WaveReadLaneAt(gi + 11u, 0u);
  outbuf.Store(gi * 4, first + at);
}

[numthreads(32, 1, 1)]
void cs_active_any_all(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  bool any_set = WaveActiveAnyTrue(gi == 3u);
  bool all_small = WaveActiveAllTrue(gi < 64u);
  outbuf.Store(gi * 4, (any_set ? 1u : 0u) | (all_small ? 2u : 0u));
}

[numthreads(32, 1, 1)]
void cs_active_sum_min_max(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  uint sum = WaveActiveSum(1u);
  uint lo = WaveActiveMin(gi);
  uint hi = WaveActiveMax(gi);
  outbuf.Store(gi * 4, sum + lo + hi);
}

[numthreads(32, 1, 1)]
void cs_prefix(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  uint prefix = WavePrefixSum(1u);
  outbuf.Store(gi * 4, prefix);
}
)";

    bool hlsl_written = write_text_file("Z:\\tmp\\dxmt_wave_ops.hlsl", hlsl);

    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    HMODULE dxcompiler = LoadLibraryA("dxcompiler.dll");
    HMODULE dxil = LoadLibraryA("dxil.dll");
    auto create_device = load_proc<D3D12CreateDeviceFn>(d3d12, "D3D12CreateDevice");
    auto serialize = load_proc<D3D12SerializeRootSignatureFn>(d3d12, "D3D12SerializeRootSignature");

    ID3D12Device* device = nullptr;
    HRESULT create_hr = create_device ? create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_D3D12DeviceProbe,
                                                      reinterpret_cast<void**>(&device))
                                      : E_FAIL;

    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {};
    HRESULT options1_hr =
        device ? device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1)) : E_FAIL;
    bool waveops_reported = SUCCEEDED(options1_hr) && options1.WaveOps;

    ID3D12RootSignature* root = nullptr;
    std::string root_errors;
    HRESULT root_hr = (device && serialize) ? create_root_signature(device, serialize, &root, root_errors) : E_FAIL;

    const WaveCase cases[] = {
        {"lane_index_count", "cs_lane_index_count", "cs_6_0"},
        {"active_ballot", "cs_active_ballot", "cs_6_0"},
        {"read_lane", "cs_read_lane", "cs_6_0"},
        {"active_any_all", "cs_active_any_all", "cs_6_0"},
        {"active_sum_min_max", "cs_active_sum_min_max", "cs_6_0"},
        {"prefix", "cs_prefix", "cs_6_0"},
    };

    std::vector<CaseResult> results;
    if (hlsl_written) {
        for (const auto& wave_case : cases)
            results.push_back(run_case(device, root, wave_case));
    }

    bool compiler_acceptance_complete = results.size() == (sizeof(cases) / sizeof(cases[0]));
    bool pso_link_complete = compiler_acceptance_complete;
    for (const auto& result : results) {
        compiler_acceptance_complete = compiler_acceptance_complete && result.compile_ok && result.dxil_blob;
        pso_link_complete = pso_link_complete && result.pso_created;
    }

    bool runtime_correctness_complete = false;
    bool waveops_reportable = compiler_acceptance_complete && pso_link_complete && runtime_correctness_complete;
    bool pass = d3d12 && dxcompiler && dxil && create_device && serialize && hlsl_written && SUCCEEDED(create_hr) &&
                SUCCEEDED(options1_hr) && SUCCEEDED(root_hr) && compiler_acceptance_complete &&
                (!waveops_reported || waveops_reportable);

    std::printf("{\n");
    std::printf("  \"schema\": \"metalsharp.d3d12-metal.probe-wave-ops.v1\",\n");
    std::printf("  \"profile\": \"%s\",\n", json_escape(profile).c_str());
    std::printf("  \"pass\": %s,\n", pass ? "true" : "false");
    std::printf("  \"entrypoints\": {\n");
    std::printf("    \"d3d12_loaded\": %s,\n", d3d12 ? "true" : "false");
    std::printf("    \"dxcompiler_loaded\": %s,\n", dxcompiler ? "true" : "false");
    std::printf("    \"dxil_loaded\": %s,\n", dxil ? "true" : "false");
    std::printf("    \"D3D12CreateDevice\": %s,\n", create_device ? "true" : "false");
    std::printf("    \"D3D12SerializeRootSignature\": %s\n", serialize ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"device\": {\n");
    std::printf("    \"D3D12CreateDevice\": \"%s\",\n", hr_hex(create_hr).c_str());
    std::printf("    \"options1_hr\": \"%s\",\n", hr_hex(options1_hr).c_str());
    std::printf("    \"wave_ops\": %s,\n", waveops_reported ? "true" : "false");
    std::printf("    \"wave_lane_count_min\": %u,\n", options1.WaveLaneCountMin);
    std::printf("    \"wave_lane_count_max\": %u\n", options1.WaveLaneCountMax);
    std::printf("  },\n");
    std::printf("  \"root_signature\": {\n");
    std::printf("    \"create\": \"%s\",\n", hr_hex(root_hr).c_str());
    std::printf("    \"errors\": \"%s\"\n", json_escape(root_errors).c_str());
    std::printf("  },\n");
    std::printf("  \"cases\": [\n");
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        std::printf("    {\"name\": \"%s\", \"compile_ok\": %s, \"dxil_blob\": %s, \"dxil_size\": %zu, "
                    "\"pso_created\": %s, \"pso_hr\": \"%s\", \"dxc_exit_code\": %lu}%s\n",
                    result.name, result.compile_ok ? "true" : "false", result.dxil_blob ? "true" : "false",
                    result.dxil_size, result.pso_created ? "true" : "false", hr_hex(result.pso_hr).c_str(),
                    static_cast<unsigned long>(result.dxc_exit_code), i + 1 == results.size() ? "" : ",");
    }
    std::printf("  ],\n");
    std::printf("  \"summary\": {\n");
    std::printf("    \"compiler_acceptance_complete\": %s,\n", compiler_acceptance_complete ? "true" : "false");
    std::printf("    \"pso_link_complete\": %s,\n", pso_link_complete ? "true" : "false");
    std::printf("    \"runtime_correctness_complete\": %s,\n", runtime_correctness_complete ? "true" : "false");
    std::printf("    \"waveops_reported\": %s,\n", waveops_reported ? "true" : "false");
    std::printf("    \"waveops_reportable\": %s,\n", waveops_reportable ? "true" : "false");
    std::printf("    \"decision\": \"%s\"\n", waveops_reportable
                                                  ? "WaveOps may be reported"
                                                  : "WaveOps must not be reported until runtime cases execute");
    std::printf("  }\n");
    std::printf("}\n");

    std::fflush(stdout);
    // Wine/MinGW can assert during late CRT condition-variable teardown after
    // the DXMT worker stack has already produced the contract JSON.
    TerminateProcess(GetCurrentProcess(), pass ? 0u : 1u);
    safe_release(root);
    safe_release(device);
}
