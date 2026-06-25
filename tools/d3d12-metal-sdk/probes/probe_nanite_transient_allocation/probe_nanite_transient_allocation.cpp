#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgiformat.h>

static const GUID IID_D3D12DeviceProbe = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};

using D3D12CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using D3D12SerializeRootSignatureFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION,
                                                       ID3DBlob**, ID3DBlob**);
using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
                                      UINT, UINT, ID3DBlob**, ID3DBlob**);

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

static D3D12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES props = {};
    props.Type = type;
    props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    props.CreationNodeMask = 1;
    props.VisibleNodeMask = 1;
    return props;
}

static D3D12_RESOURCE_DESC buffer_desc(UINT64 bytes, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return desc;
}

static D3D12_RESOURCE_BARRIER transition_barrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                                 D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

static D3D12_RESOURCE_BARRIER uav_barrier(ID3D12Resource* resource) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.UAV.pResource = resource;
    return barrier;
}

static D3D12_CPU_DESCRIPTOR_HANDLE offset_cpu(D3D12_CPU_DESCRIPTOR_HANDLE handle, UINT increment, UINT index) {
    handle.ptr += static_cast<SIZE_T>(increment) * index;
    return handle;
}

static HRESULT execute_and_wait(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* list,
                                UINT timeout_ms, UINT64& completed_value) {
    HRESULT hr = list->Close();
    if (FAILED(hr))
        return hr;
    ID3D12CommandList* lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    ID3D12Fence* fence = nullptr;
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr))
        return hr;
    hr = queue->Signal(fence, 1);
    if (SUCCEEDED(hr)) {
        HANDLE event_handle = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        if (!event_handle)
            hr = HRESULT_FROM_WIN32(GetLastError());
        if (SUCCEEDED(hr))
            hr = fence->SetEventOnCompletion(1, event_handle);
        if (SUCCEEDED(hr)) {
            DWORD wait_result = WaitForSingleObject(event_handle, timeout_ms);
            if (wait_result != WAIT_OBJECT_0)
                hr = HRESULT_FROM_WIN32(wait_result);
        }
        if (event_handle)
            CloseHandle(event_handle);
    }
    completed_value = fence->GetCompletedValue();
    if (completed_value >= 1 && FAILED(hr))
        hr = S_OK;
    fence->Release();
    return hr;
}

int main() {
    constexpr UINT64 kCandidateBytes = 134217728ull;
    constexpr UINT64 kCandidateAlignment = 65536ull;
    constexpr UINT64 kCandidateRecordBytes = 12ull;
    constexpr UINT64 kCandidateRecordCount = kCandidateBytes / kCandidateRecordBytes;
    constexpr UINT64 kLastRecordOffset = (kCandidateRecordCount - 1ull) * kCandidateRecordBytes;
    constexpr UINT64 kOutputBytes = 4096ull;
    constexpr UINT64 kInputBytes = 4096ull;
    constexpr UINT64 kCandidateFirstReadbackOffset = 256ull;
    constexpr UINT64 kCandidateTailReadbackOffset = 512ull;
    constexpr UINT64 kCandidateTailCopyOffset = kLastRecordOffset - 36ull;

    const std::string profile = getenv_string("D3D12_METAL_SDK_PROFILE");
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    HMODULE d3dcompiler = LoadLibraryA("d3dcompiler_47.dll");
    auto create_device = load_proc<D3D12CreateDeviceFn>(d3d12, "D3D12CreateDevice");
    auto serialize = load_proc<D3D12SerializeRootSignatureFn>(d3d12, "D3D12SerializeRootSignature");
    auto compile = load_proc<D3DCompileFn>(d3dcompiler, "D3DCompile");

    ID3D12Device* device = nullptr;
    HRESULT create_hr = create_device ? create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_D3D12DeviceProbe,
                                                      reinterpret_cast<void**>(&device))
                                      : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);

    D3D12_RESOURCE_DESC candidate_desc = buffer_desc(kCandidateBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    D3D12_RESOURCE_ALLOCATION_INFO candidate_alloc = {};
    if (device)
        candidate_alloc = device->GetResourceAllocationInfo(0, 1, &candidate_desc);

    ID3D12Resource* candidate = nullptr;
    ID3D12Resource* output = nullptr;
    ID3D12Resource* input = nullptr;
    ID3D12Resource* input_upload = nullptr;
    ID3D12Resource* readback = nullptr;
    D3D12_HEAP_PROPERTIES default_heap = heap_props(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_HEAP_PROPERTIES readback_heap = heap_props(D3D12_HEAP_TYPE_READBACK);
    HRESULT candidate_hr =
        device
            ? device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &candidate_desc,
                                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&candidate))
            : E_FAIL;
    D3D12_RESOURCE_DESC output_desc = buffer_desc(kOutputBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    HRESULT output_hr =
        device ? device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &output_desc,
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output))
               : E_FAIL;
    D3D12_RESOURCE_DESC input_desc = buffer_desc(kInputBytes);
    HRESULT input_hr =
        device ? device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &input_desc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&input))
               : E_FAIL;
    D3D12_HEAP_PROPERTIES upload_heap = heap_props(D3D12_HEAP_TYPE_UPLOAD);
    HRESULT input_upload_hr = device ? device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &input_desc,
                                                                       D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                       IID_PPV_ARGS(&input_upload))
                                     : E_FAIL;
    if (input_upload) {
        uint32_t sentinels[24] = {};
        for (uint32_t gi = 0; gi < 4; ++gi) {
            uint32_t* forward = sentinels + gi * 3;
            forward[0] = 0x10000000u + gi;
            forward[1] = 0xabc00000u | gi;
            forward[2] = 0x12340000u | gi;
            uint32_t* reverse = sentinels + 12 + gi * 3;
            reverse[0] = 0xfeed0000u | gi;
            reverse[1] = 0xbeef0000u | gi;
            reverse[2] = 0x10000000u + static_cast<uint32_t>(kCandidateRecordCount) - gi;
        }
        void* mapped = nullptr;
        D3D12_RANGE no_read = {0, 0};
        if (SUCCEEDED(input_upload->Map(0, &no_read, &mapped)) && mapped) {
            std::memcpy(mapped, sentinels, sizeof(sentinels));
            input_upload->Unmap(0, nullptr);
        }
    }
    D3D12_RESOURCE_DESC readback_desc = buffer_desc(kOutputBytes);
    HRESULT readback_hr =
        device ? device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))
               : E_FAIL;

    const char* hlsl = R"(
ByteAddressBuffer InputSentinels : register(t0);
RWByteAddressBuffer CandidateClusters : register(u0);
RWByteAddressBuffer Output : register(u1);
static const uint LastRecordOffsetLiteral = 134217708u;
[numthreads(4, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  uint forwardOffset = gi * 12u;
  uint reverseOffset = LastRecordOffsetLiteral - gi * 12u;
  uint reverseSourceOffset = 48u + gi * 12u;
  uint3 loadedForward = InputSentinels.Load3(forwardOffset);
  uint3 loadedReverse = InputSentinels.Load3(reverseSourceOffset);
  CandidateClusters.Store3(forwardOffset, loadedForward);
  CandidateClusters.Store3(reverseOffset, loadedReverse);
  Output.Store3(gi * 24u, loadedForward);
  Output.Store3(gi * 24u + 12u, loadedReverse);
}
)";

    ID3DBlob* shader = nullptr;
    ID3DBlob* compile_errors = nullptr;
    HRESULT compile_hr = compile ? compile(hlsl, std::strlen(hlsl), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0,
                                           &shader, &compile_errors)
                                 : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    std::string compile_error_text;
    if (compile_errors) {
        compile_error_text.assign(static_cast<const char*>(compile_errors->GetBufferPointer()),
                                  compile_errors->GetBufferSize());
        compile_errors->Release();
    }

    ID3D12RootSignature* root = nullptr;
    ID3DBlob* root_blob = nullptr;
    ID3DBlob* root_errors = nullptr;
    HRESULT root_hr = E_FAIL;
    std::string root_error_text;
    if (serialize && device) {
        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1;
        ranges[0].BaseShaderRegister = 0;
        ranges[0].RegisterSpace = 0;
        ranges[0].OffsetInDescriptorsFromTableStart = 0;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 2;
        ranges[1].BaseShaderRegister = 0;
        ranges[1].RegisterSpace = 0;
        ranges[1].OffsetInDescriptorsFromTableStart = 1;

        D3D12_ROOT_PARAMETER params[2] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 2;
        params[0].DescriptorTable.pDescriptorRanges = ranges;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants.Num32BitValues = 4;
        params[1].Constants.ShaderRegister = 0;
        params[1].Constants.RegisterSpace = 0;

        D3D12_ROOT_SIGNATURE_DESC root_desc = {};
        root_desc.NumParameters = 2;
        root_desc.pParameters = params;
        root_hr = serialize(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &root_blob, &root_errors);
        if (root_errors) {
            root_error_text.assign(static_cast<const char*>(root_errors->GetBufferPointer()),
                                   root_errors->GetBufferSize());
            root_errors->Release();
        }
        if (SUCCEEDED(root_hr))
            root_hr = device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                                  IID_PPV_ARGS(&root));
    }

    ID3D12PipelineState* pso = nullptr;
    HRESULT pso_hr = E_FAIL;
    if (device && root && shader) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = shader->GetBufferPointer();
        pso_desc.CS.BytecodeLength = shader->GetBufferSize();
        pso_hr = device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso));
    }

    ID3D12DescriptorHeap* heap = nullptr;
    HRESULT heap_hr = E_FAIL;
    UINT descriptor_increment = 0;
    if (device) {
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap_desc.NumDescriptors = 3;
        heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        heap_hr = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap));
        descriptor_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    if (device && heap && candidate && output && input) {
        D3D12_SHADER_RESOURCE_VIEW_DESC input_srv = {};
        input_srv.Format = DXGI_FORMAT_R32_TYPELESS;
        input_srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        input_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        input_srv.Buffer.FirstElement = 0;
        input_srv.Buffer.NumElements = static_cast<UINT>(kInputBytes / 4ull);
        input_srv.Buffer.StructureByteStride = 0;
        input_srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        device->CreateShaderResourceView(input, &input_srv, heap->GetCPUDescriptorHandleForHeapStart());

        D3D12_UNORDERED_ACCESS_VIEW_DESC candidate_uav = {};
        candidate_uav.Format = DXGI_FORMAT_R32_TYPELESS;
        candidate_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        candidate_uav.Buffer.FirstElement = 0;
        candidate_uav.Buffer.NumElements = static_cast<UINT>(kCandidateBytes / 4ull);
        candidate_uav.Buffer.StructureByteStride = 0;
        candidate_uav.Buffer.CounterOffsetInBytes = 0;
        candidate_uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        device->CreateUnorderedAccessView(
            candidate, nullptr, &candidate_uav,
            offset_cpu(heap->GetCPUDescriptorHandleForHeapStart(), descriptor_increment, 1));

        D3D12_UNORDERED_ACCESS_VIEW_DESC output_uav = candidate_uav;
        output_uav.Buffer.NumElements = static_cast<UINT>(kOutputBytes / 4ull);
        device->CreateUnorderedAccessView(
            output, nullptr, &output_uav,
            offset_cpu(heap->GetCPUDescriptorHandleForHeapStart(), descriptor_increment, 2));
    }

    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HRESULT queue_hr = device ? device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)) : E_FAIL;
    HRESULT allocator_hr =
        device ? device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)) : E_FAIL;
    HRESULT list_hr =
        device ? device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list))
               : E_FAIL;

    bool dispatch_recorded = false;
    bool copy_recorded = false;
    UINT64 completed_value = 0;
    HRESULT execute_hr = E_FAIL;
    if (queue && list && heap && root && pso && candidate && output && input && input_upload && readback) {
        list->CopyBufferRegion(input, 0, input_upload, 0, 96);
        D3D12_RESOURCE_BARRIER input_to_srv =
            transition_barrier(input, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &input_to_srv);
        ID3D12DescriptorHeap* heaps[] = {heap};
        list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootSignature(root);
        list->SetPipelineState(pso);
        list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
        const UINT constants[4] = {static_cast<UINT>(kCandidateRecordCount), static_cast<UINT>(kLastRecordOffset),
                                   0x10000000u, 0u};
        list->SetComputeRoot32BitConstants(1, 4, constants, 0);
        list->Dispatch(1, 1, 1);
        dispatch_recorded = true;
        D3D12_RESOURCE_BARRIER barriers[2] = {uav_barrier(candidate), uav_barrier(output)};
        list->ResourceBarrier(2, barriers);
        D3D12_RESOURCE_BARRIER output_to_copy =
            transition_barrier(output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &output_to_copy);
        list->CopyBufferRegion(readback, 0, output, 0, 128);
        D3D12_RESOURCE_BARRIER candidate_to_copy =
            transition_barrier(candidate, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &candidate_to_copy);
        list->CopyBufferRegion(readback, kCandidateFirstReadbackOffset, candidate, 0, 48);
        list->CopyBufferRegion(readback, kCandidateTailReadbackOffset, candidate, kCandidateTailCopyOffset, 48);
        copy_recorded = true;
        execute_hr = execute_and_wait(device, queue, list, 15000, completed_value);
    }

    uint32_t values[kOutputBytes / sizeof(uint32_t)] = {};
    bool readback_ok = false;
    if (readback) {
        uint8_t* mapped = nullptr;
        D3D12_RANGE range = {0, sizeof(values)};
        HRESULT map_hr = readback->Map(0, &range, reinterpret_cast<void**>(&mapped));
        if (SUCCEEDED(map_hr) && mapped) {
            std::memcpy(values, mapped, sizeof(values));
            readback_ok = true;
            D3D12_RANGE write_range = {0, 0};
            readback->Unmap(0, &write_range);
        }
    }

    bool sentinels_ok = readback_ok;
    for (uint32_t gi = 0; gi < 4 && sentinels_ok; ++gi) {
        uint32_t* slot = values + gi * 6;
        sentinels_ok = slot[0] == (0x10000000u + gi) && slot[1] == (0xabc00000u | gi) &&
                       slot[2] == (0x12340000u | gi) && slot[3] == (0xfeed0000u | gi) &&
                       slot[4] == (0xbeef0000u | gi) &&
                       slot[5] == (0x10000000u + static_cast<uint32_t>(kCandidateRecordCount) - gi);
    }
    uint32_t* candidate_first = values + (kCandidateFirstReadbackOffset / sizeof(uint32_t));
    uint32_t* candidate_tail = values + (kCandidateTailReadbackOffset / sizeof(uint32_t));
    bool candidate_store_ok = readback_ok;
    for (uint32_t gi = 0; gi < 4 && candidate_store_ok; ++gi) {
        uint32_t* slot = candidate_first + gi * 3;
        candidate_store_ok =
            slot[0] == (0x10000000u + gi) && slot[1] == (0xabc00000u | gi) && slot[2] == (0x12340000u | gi);
    }
    for (uint32_t gi = 0; gi < 4 && candidate_store_ok; ++gi) {
        uint32_t expected_gi = 3u - gi;
        uint32_t* slot = candidate_tail + gi * 3;
        candidate_store_ok = slot[0] == (0xfeed0000u | expected_gi) && slot[1] == (0xbeef0000u | expected_gi) &&
                             slot[2] == (0x10000000u + static_cast<uint32_t>(kCandidateRecordCount) - expected_gi);
    }
    sentinels_ok = sentinels_ok && candidate_store_ok;

    bool allocation_alignment_ok = candidate_alloc.Alignment == 0 || candidate_alloc.Alignment == kCandidateAlignment;
    bool pass = d3d12 && d3dcompiler && create_device && serialize && compile && SUCCEEDED(create_hr) &&
                SUCCEEDED(candidate_hr) && candidate && candidate_alloc.SizeInBytes >= kCandidateBytes &&
                allocation_alignment_ok && SUCCEEDED(output_hr) && SUCCEEDED(input_hr) && SUCCEEDED(input_upload_hr) &&
                SUCCEEDED(readback_hr) && SUCCEEDED(compile_hr) && SUCCEEDED(root_hr) && SUCCEEDED(pso_hr) &&
                SUCCEEDED(heap_hr) && SUCCEEDED(queue_hr) && SUCCEEDED(allocator_hr) && SUCCEEDED(list_hr) &&
                dispatch_recorded && copy_recorded && SUCCEEDED(execute_hr) && completed_value >= 1 && readback_ok &&
                sentinels_ok;

    std::printf("{\n");
    std::printf("  \"schema\": \"metalsharp.d3d12-metal.probe-nanite-transient-allocation.v1\",\n");
    std::printf("  \"profile\": \"%s\",\n", json_escape(profile).c_str());
    std::printf("  \"pass\": %s,\n", pass ? "true" : "false");
    std::printf("  \"target\": {\n");
    std::printf("    \"debug_name\": \"Nanite.MainAndPostCandidateClustersBuffer\",\n");
    std::printf("    \"candidate_bytes\": %llu,\n", static_cast<unsigned long long>(kCandidateBytes));
    std::printf("    \"required_alignment\": %llu,\n", static_cast<unsigned long long>(kCandidateAlignment));
    std::printf("    \"record_bytes\": %llu,\n", static_cast<unsigned long long>(kCandidateRecordBytes));
    std::printf("    \"record_count\": %llu,\n", static_cast<unsigned long long>(kCandidateRecordCount));
    std::printf("    \"last_record_offset\": %llu\n", static_cast<unsigned long long>(kLastRecordOffset));
    std::printf("  },\n");
    std::printf("  \"entrypoints\": {\n");
    std::printf("    \"d3d12_loaded\": %s,\n", d3d12 ? "true" : "false");
    std::printf("    \"d3dcompiler_loaded\": %s,\n", d3dcompiler ? "true" : "false");
    std::printf("    \"D3D12CreateDevice\": %s,\n", create_device ? "true" : "false");
    std::printf("    \"D3D12SerializeRootSignature\": %s,\n", serialize ? "true" : "false");
    std::printf("    \"D3DCompile\": %s\n", compile ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"resource_allocation\": {\n");
    std::printf("    \"create_device_hr\": \"%s\",\n", hr_hex(create_hr).c_str());
    std::printf("    \"candidate_create_hr\": \"%s\",\n", hr_hex(candidate_hr).c_str());
    std::printf("    \"candidate_allocation_size\": %llu,\n",
                static_cast<unsigned long long>(candidate_alloc.SizeInBytes));
    std::printf("    \"candidate_allocation_alignment\": %llu,\n",
                static_cast<unsigned long long>(candidate_alloc.Alignment));
    std::printf("    \"allocation_alignment_ok\": %s,\n", allocation_alignment_ok ? "true" : "false");
    std::printf("    \"output_create_hr\": \"%s\",\n", hr_hex(output_hr).c_str());
    std::printf("    \"input_create_hr\": \"%s\",\n", hr_hex(input_hr).c_str());
    std::printf("    \"input_upload_create_hr\": \"%s\",\n", hr_hex(input_upload_hr).c_str());
    std::printf("    \"readback_create_hr\": \"%s\"\n", hr_hex(readback_hr).c_str());
    std::printf("  },\n");
    std::printf("  \"compute\": {\n");
    std::printf("    \"compile_hr\": \"%s\",\n", hr_hex(compile_hr).c_str());
    std::printf("    \"compile_errors\": \"%s\",\n", json_escape(compile_error_text).c_str());
    std::printf("    \"root_signature_hr\": \"%s\",\n", hr_hex(root_hr).c_str());
    std::printf("    \"root_errors\": \"%s\",\n", json_escape(root_error_text).c_str());
    std::printf("    \"pso_hr\": \"%s\",\n", hr_hex(pso_hr).c_str());
    std::printf("    \"descriptor_heap_hr\": \"%s\",\n", hr_hex(heap_hr).c_str());
    std::printf("    \"dispatch_recorded\": %s,\n", dispatch_recorded ? "true" : "false");
    std::printf("    \"copy_recorded\": %s,\n", copy_recorded ? "true" : "false");
    std::printf("    \"execute_hr\": \"%s\",\n", hr_hex(execute_hr).c_str());
    std::printf("    \"completed_value\": %llu\n", static_cast<unsigned long long>(completed_value));
    std::printf("  },\n");
    std::printf("  \"readback\": {\n");
    std::printf("    \"readback_ok\": %s,\n", readback_ok ? "true" : "false");
    std::printf("    \"sentinels_ok\": %s,\n", sentinels_ok ? "true" : "false");
    std::printf("    \"candidate_store_ok\": %s,\n", candidate_store_ok ? "true" : "false");
    std::printf("    \"first_record\": [%u, %u, %u],\n", values[0], values[1], values[2]);
    std::printf("    \"first_reverse_record\": [%u, %u, %u],\n", values[3], values[4], values[5]);
    std::printf("    \"candidate_first_record\": [%u, %u, %u],\n", candidate_first[0], candidate_first[1],
                candidate_first[2]);
    std::printf("    \"candidate_tail_lowest_copied_record\": [%u, %u, %u]\n", candidate_tail[0], candidate_tail[1],
                candidate_tail[2]);
    std::printf("  },\n");
    std::printf("  \"decision\": \"%s\"\n", pass
                                                ? "Nanite candidate buffer allocation and raw UAV readback probe passed"
                                                : "Nanite candidate buffer path is not proven; keep live launches "
                                                  "paused and deny Nanite-adjacent readiness");
    std::printf("}\n");
    std::fflush(stdout);

    TerminateProcess(GetCurrentProcess(), pass ? 0 : 1);

    safe_release(list);
    safe_release(allocator);
    safe_release(queue);
    safe_release(heap);
    safe_release(pso);
    safe_release(root);
    safe_release(root_blob);
    safe_release(shader);
    safe_release(readback);
    safe_release(input_upload);
    safe_release(input);
    safe_release(output);
    safe_release(candidate);
    safe_release(device);
    return pass ? 0 : 1;
}
