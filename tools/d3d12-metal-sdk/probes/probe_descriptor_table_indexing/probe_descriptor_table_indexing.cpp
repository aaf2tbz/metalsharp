#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <d3d12.h>
#include <dxgiformat.h>

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
    DWORD wait_result = WaitForSingleObject(process.hProcess, 30000);
    DWORD exit_code = 0xffffffffu;
    if (wait_result == WAIT_OBJECT_0)
        GetExitCodeProcess(process.hProcess, &exit_code);
    else
        TerminateProcess(process.hProcess, 0xffffffffu);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exit_code;
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

static D3D12_CPU_DESCRIPTOR_HANDLE offset_cpu(D3D12_CPU_DESCRIPTOR_HANDLE handle, UINT increment, UINT index) {
    handle.ptr += static_cast<SIZE_T>(increment) * index;
    return handle;
}

static D3D12_RESOURCE_BARRIER transition_barrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                                 D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

static D3D12_RESOURCE_BARRIER uav_barrier(ID3D12Resource* resource) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    return barrier;
}

static HRESULT execute_and_wait(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* list,
                                UINT timeout_ms, UINT64& completed_value) {
    if (!device || !queue || !list)
        return E_INVALIDARG;
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
    constexpr UINT kDescriptorCount = 4;
    constexpr UINT kThreadCount = 8;
    constexpr UINT64 kBufferBytes = 256;
    constexpr UINT64 kOutputBytes = 4096;
    const uint32_t sentinels[kDescriptorCount] = {0x11110000u, 0x22220000u, 0x33330000u, 0x44440000u};

    const char* hlsl = R"(
ByteAddressBuffer Inputs[4] : register(t0);
RWByteAddressBuffer Output : register(u0);
cbuffer Params : register(b0) {
  uint BaseIndex;
  uint Addend;
  uint Pad0;
  uint Pad1;
};
[numthreads(8, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  uint descriptorIndex = (BaseIndex + gi) & 3u;
  uint value = Inputs[descriptorIndex].Load(0);
  Output.Store(gi * 4u, value + Addend + descriptorIndex);
}
)";

    bool hlsl_written = write_text_file("dxmt_descriptor_table_indexing.hlsl", hlsl);
    DeleteFileA("dxmt_descriptor_table_indexing.dxil");
    DeleteFileA("dxmt_descriptor_table_indexing.err");
    DWORD dxc_exit =
        hlsl_written
            ? run_process_wait("dxc.exe -nologo -T cs_5_1 -E main -HV 2021 -Od -Fo dxmt_descriptor_table_indexing.dxil "
                               "-Fe dxmt_descriptor_table_indexing.err dxmt_descriptor_table_indexing.hlsl")
            : 0xffffffffu;
    std::vector<uint8_t> dxil = read_binary_file("dxmt_descriptor_table_indexing.dxil");
    std::string dxc_errors = read_text_file("dxmt_descriptor_table_indexing.err");
    bool compile_ok = hlsl_written && dxc_exit == 0 && !dxil.empty();

    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    auto create_device = load_proc<D3D12CreateDeviceFn>(d3d12, "D3D12CreateDevice");
    auto serialize = load_proc<D3D12SerializeRootSignatureFn>(d3d12, "D3D12SerializeRootSignature");

    ID3D12Device* device = nullptr;
    HRESULT create_hr = create_device ? create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_D3D12DeviceProbe,
                                                      reinterpret_cast<void**>(&device))
                                      : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);

    ID3D12RootSignature* root = nullptr;
    ID3DBlob* root_blob = nullptr;
    ID3DBlob* root_error_blob = nullptr;
    HRESULT root_hr = E_FAIL;
    std::string root_errors;
    if (device && serialize) {
        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = kDescriptorCount;
        ranges[0].BaseShaderRegister = 0;
        ranges[0].RegisterSpace = 0;
        ranges[0].OffsetInDescriptorsFromTableStart = 0;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].BaseShaderRegister = 0;
        ranges[1].RegisterSpace = 0;
        ranges[1].OffsetInDescriptorsFromTableStart = kDescriptorCount;

        D3D12_ROOT_PARAMETER params[2] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 2;
        params[0].DescriptorTable.pDescriptorRanges = ranges;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants.Num32BitValues = 4;
        params[1].Constants.ShaderRegister = 0;

        D3D12_ROOT_SIGNATURE_DESC root_desc = {};
        root_desc.NumParameters = 2;
        root_desc.pParameters = params;
        root_hr = serialize(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &root_blob, &root_error_blob);
        if (root_error_blob) {
            root_errors.assign(static_cast<const char*>(root_error_blob->GetBufferPointer()),
                               root_error_blob->GetBufferSize());
            root_error_blob->Release();
        }
        if (SUCCEEDED(root_hr))
            root_hr = device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                                  IID_PPV_ARGS(&root));
    }

    ID3D12PipelineState* pso = nullptr;
    HRESULT pso_hr = E_FAIL;
    if (device && root && compile_ok) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = root;
        desc.CS.pShaderBytecode = dxil.data();
        desc.CS.BytecodeLength = dxil.size();
        pso_hr = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso));
    }

    ID3D12Resource* inputs[kDescriptorCount] = {};
    ID3D12Resource* output = nullptr;
    ID3D12Resource* readback = nullptr;
    D3D12_HEAP_PROPERTIES upload_heap = heap_props(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_HEAP_PROPERTIES default_heap = heap_props(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_HEAP_PROPERTIES readback_heap = heap_props(D3D12_HEAP_TYPE_READBACK);
    D3D12_RESOURCE_DESC input_desc = buffer_desc(kBufferBytes);
    D3D12_RESOURCE_DESC output_desc = buffer_desc(kOutputBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    D3D12_RESOURCE_DESC readback_desc = buffer_desc(kOutputBytes);

    HRESULT input_hrs[kDescriptorCount] = {};
    for (UINT i = 0; i < kDescriptorCount; ++i) {
        input_hrs[i] = device ? device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &input_desc,
                                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                IID_PPV_ARGS(&inputs[i]))
                              : E_FAIL;
        if (inputs[i]) {
            void* mapped = nullptr;
            D3D12_RANGE no_read = {0, 0};
            if (SUCCEEDED(inputs[i]->Map(0, &no_read, &mapped)) && mapped) {
                std::memcpy(mapped, &sentinels[i], sizeof(uint32_t));
                inputs[i]->Unmap(0, nullptr);
            }
        }
    }
    HRESULT output_hr =
        device ? device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &output_desc,
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output))
               : E_FAIL;
    HRESULT readback_hr =
        device ? device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))
               : E_FAIL;

    ID3D12DescriptorHeap* heap = nullptr;
    HRESULT heap_hr = E_FAIL;
    UINT inc = 0;
    if (device) {
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap_desc.NumDescriptors = kDescriptorCount + 1;
        heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        heap_hr = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap));
        inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    bool descriptors_created = false;
    if (device && heap && inc != 0 && output) {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < kDescriptorCount; ++i) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
            srv.Format = DXGI_FORMAT_R32_TYPELESS;
            srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Buffer.FirstElement = 0;
            srv.Buffer.NumElements = static_cast<UINT>(kBufferBytes / 4);
            srv.Buffer.StructureByteStride = 0;
            srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            device->CreateShaderResourceView(inputs[i], &srv, offset_cpu(cpu, inc, i));
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_R32_TYPELESS;
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav.Buffer.FirstElement = 0;
        uav.Buffer.NumElements = static_cast<UINT>(kOutputBytes / 4);
        uav.Buffer.StructureByteStride = 0;
        uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        device->CreateUnorderedAccessView(output, nullptr, &uav, offset_cpu(cpu, inc, kDescriptorCount));
        descriptors_created = true;
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
    if (queue && list && heap && root && pso && output && readback) {
        ID3D12DescriptorHeap* heaps[] = {heap};
        list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootSignature(root);
        list->SetPipelineState(pso);
        list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
        const UINT constants[4] = {1u, 0x100u, 0u, 0u};
        list->SetComputeRoot32BitConstants(1, 4, constants, 0);
        list->Dispatch(1, 1, 1);
        dispatch_recorded = true;
        D3D12_RESOURCE_BARRIER barrier = uav_barrier(output);
        list->ResourceBarrier(1, &barrier);
        D3D12_RESOURCE_BARRIER to_copy =
            transition_barrier(output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &to_copy);
        list->CopyBufferRegion(readback, 0, output, 0, kOutputBytes);
        copy_recorded = true;
        execute_hr = execute_and_wait(device, queue, list, 15000, completed_value);
    }

    uint32_t values[kThreadCount] = {};
    bool readback_ok = false;
    if (readback) {
        void* mapped = nullptr;
        D3D12_RANGE range = {0, sizeof(values)};
        HRESULT map_hr = readback->Map(0, &range, &mapped);
        if (SUCCEEDED(map_hr) && mapped) {
            std::memcpy(values, mapped, sizeof(values));
            D3D12_RANGE no_write = {0, 0};
            readback->Unmap(0, &no_write);
            readback_ok = true;
        }
    }

    bool values_ok = readback_ok;
    for (UINT gi = 0; gi < kThreadCount && values_ok; ++gi) {
        UINT descriptor_index = (1u + gi) & 3u;
        uint32_t expected = sentinels[descriptor_index] + 0x100u + descriptor_index;
        values_ok = values[gi] == expected;
    }

    bool inputs_ok = true;
    for (UINT i = 0; i < kDescriptorCount; ++i)
        inputs_ok = inputs_ok && SUCCEEDED(input_hrs[i]);

    bool pass = d3d12 && create_device && serialize && hlsl_written && compile_ok && SUCCEEDED(create_hr) &&
                SUCCEEDED(root_hr) && SUCCEEDED(pso_hr) && inputs_ok && SUCCEEDED(output_hr) &&
                SUCCEEDED(readback_hr) && SUCCEEDED(heap_hr) && descriptors_created && SUCCEEDED(queue_hr) &&
                SUCCEEDED(allocator_hr) && SUCCEEDED(list_hr) && dispatch_recorded && copy_recorded &&
                SUCCEEDED(execute_hr) && completed_value >= 1 && values_ok;

    std::printf("{\n");
    std::printf("  \"schema\": \"metalsharp.d3d12-metal.probe-descriptor-table-indexing.v1\",\n");
    std::printf("  \"profile\": \"%s\",\n", json_escape(getenv_string("D3D12_METAL_SDK_PROFILE")).c_str());
    std::printf("  \"pass\": %s,\n", pass ? "true" : "false");
    std::printf("  \"compile\": {\n");
    std::printf("    \"hlsl_written\": %s,\n", hlsl_written ? "true" : "false");
    std::printf("    \"dxc_exit_code\": %lu,\n", static_cast<unsigned long>(dxc_exit));
    std::printf("    \"dxil_size\": %zu,\n", dxil.size());
    std::printf("    \"compile_ok\": %s,\n", compile_ok ? "true" : "false");
    std::printf("    \"errors\": \"%s\"\n", json_escape(dxc_errors).c_str());
    std::printf("  },\n");
    std::printf("  \"d3d12\": {\n");
    std::printf("    \"d3d12_loaded\": %s,\n", d3d12 ? "true" : "false");
    std::printf("    \"create_device_hr\": \"%s\",\n", hr_hex(create_hr).c_str());
    std::printf("    \"root_signature_hr\": \"%s\",\n", hr_hex(root_hr).c_str());
    std::printf("    \"root_errors\": \"%s\",\n", json_escape(root_errors).c_str());
    std::printf("    \"pso_hr\": \"%s\",\n", hr_hex(pso_hr).c_str());
    std::printf("    \"heap_hr\": \"%s\",\n", hr_hex(heap_hr).c_str());
    std::printf("    \"descriptors_created\": %s\n", descriptors_created ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"execution\": {\n");
    std::printf("    \"queue_hr\": \"%s\",\n", hr_hex(queue_hr).c_str());
    std::printf("    \"allocator_hr\": \"%s\",\n", hr_hex(allocator_hr).c_str());
    std::printf("    \"list_hr\": \"%s\",\n", hr_hex(list_hr).c_str());
    std::printf("    \"dispatch_recorded\": %s,\n", dispatch_recorded ? "true" : "false");
    std::printf("    \"copy_recorded\": %s,\n", copy_recorded ? "true" : "false");
    std::printf("    \"execute_hr\": \"%s\",\n", hr_hex(execute_hr).c_str());
    std::printf("    \"completed_value\": %llu\n", static_cast<unsigned long long>(completed_value));
    std::printf("  },\n");
    std::printf("  \"readback\": {\n");
    std::printf("    \"readback_ok\": %s,\n", readback_ok ? "true" : "false");
    std::printf("    \"values_ok\": %s,\n", values_ok ? "true" : "false");
    std::printf("    \"values\": [%u, %u, %u, %u, %u, %u, %u, %u]\n", values[0], values[1], values[2], values[3],
                values[4], values[5], values[6], values[7]);
    std::printf("  },\n");
    std::printf("  \"decision\": \"%s\"\n", pass ? "Dynamic descriptor table indexing passed with runtime readback"
                                                 : "Dynamic descriptor table indexing is not proven");
    std::printf("}\n");
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), pass ? 0u : 1u);

    safe_release(list);
    safe_release(allocator);
    safe_release(queue);
    safe_release(heap);
    safe_release(readback);
    safe_release(output);
    for (UINT i = 0; i < kDescriptorCount; ++i)
        safe_release(inputs[i]);
    safe_release(pso);
    safe_release(root_blob);
    safe_release(root);
    safe_release(device);
    return pass ? 0 : 1;
}
