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
    bool readback_ok = false;
    bool values_ok = false;
    HRESULT pso_hr = E_FAIL;
    HRESULT execute_hr = E_FAIL;
    DWORD dxc_exit_code = 0xffffffffu;
    size_t dxil_size = 0;
    std::vector<uint32_t> expected;
    std::vector<uint32_t> actual;
    std::string detail;
};

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

static D3D12_RESOURCE_DESC texture2d_desc(UINT width, UINT height, DXGI_FORMAT format,
                                          D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;
    return desc;
}

static D3D12_CPU_DESCRIPTOR_HANDLE offset_cpu(D3D12_CPU_DESCRIPTOR_HANDLE handle, UINT increment, UINT index) {
    handle.ptr += static_cast<SIZE_T>(increment) * index;
    return handle;
}

static D3D12_GPU_DESCRIPTOR_HANDLE offset_gpu(D3D12_GPU_DESCRIPTOR_HANDLE handle, UINT increment, UINT index) {
    handle.ptr += static_cast<UINT64>(increment) * index;
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

static bool fill_upload_buffer(ID3D12Resource* resource, const uint32_t* data, size_t word_count) {
    if (!resource || !data || word_count == 0)
        return false;
    void* mapped = nullptr;
    D3D12_RANGE no_read = {0, 0};
    if (FAILED(resource->Map(0, &no_read, &mapped)) || !mapped)
        return false;
    std::memcpy(mapped, data, word_count * sizeof(uint32_t));
    resource->Unmap(0, nullptr);
    return true;
}

static std::string json_u32_array(const std::vector<uint32_t>& values) {
    std::string out = "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i)
            out += ", ";
        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), "%u", values[i]);
        out += buffer;
    }
    out += "]";
    return out;
}

static bool expected_for_case(const char* name, std::vector<uint32_t>& expected) {
    if (std::strcmp(name, "root_constants_uav") == 0) {
        expected = {15u, 18u, 21u, 24u};
        return true;
    }
    if (std::strcmp(name, "descriptor_indexing") == 0) {
        expected = {15u, 25u, 35u, 45u};
        return true;
    }
    if (std::strcmp(name, "int64_arithmetic") == 0) {
        expected = {7u, 17u, 8u, 33u, 9u, 49u, 10u, 65u};
        return true;
    }
    if (std::strcmp(name, "atomics_barriers") == 0) {
        expected = {4u, 5u, 6u, 7u};
        return true;
    }
    if (std::strcmp(name, "atomic64_raw_uav") == 0) {
        expected = {15u, 0u, 5u, 0u};
        return true;
    }
    if (std::strcmp(name, "texture_sampler") == 0) {
        expected = {10u, 11u, 12u, 13u};
        return true;
    }
    return false;
}

static bool execute_runtime_case(ID3D12Device* device, ID3D12RootSignature* root, ID3D12PipelineState* pso,
                                 const AuditCase& audit_case, CaseResult& result) {
    if (!expected_for_case(audit_case.name, result.expected)) {
        result.detail = "compiled and linked; runtime proof is still pending";
        return false;
    }
    const bool texture_case = std::strcmp(audit_case.name, "texture_sampler") == 0;

    constexpr UINT kInputWords = 4;
    constexpr UINT64 kInputBytes = 256;
    constexpr UINT64 kOutputBytes = 4096;
    const uint32_t input0[kInputWords] = {0x10u, 0x20u, 0x30u, 0x40u};
    const uint32_t input1[kInputWords] = {10u, 20u, 30u, 40u};
    const uint32_t input2[kInputWords] = {1u, 2u, 3u, 4u};

    ID3D12Resource* inputs[3] = {};
    ID3D12Resource* output = nullptr;
    ID3D12Resource* readback = nullptr;
    ID3D12Resource* texture = nullptr;
    ID3D12Resource* texture_upload = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    ID3D12DescriptorHeap* sampler_heap = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;

    D3D12_HEAP_PROPERTIES upload_heap = heap_props(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_HEAP_PROPERTIES default_heap = heap_props(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_HEAP_PROPERTIES readback_heap = heap_props(D3D12_HEAP_TYPE_READBACK);
    D3D12_RESOURCE_DESC input_desc = buffer_desc(kInputBytes);
    D3D12_RESOURCE_DESC output_desc = buffer_desc(kOutputBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    D3D12_RESOURCE_DESC readback_desc = buffer_desc(kOutputBytes);
    D3D12_RESOURCE_DESC texture_desc = texture2d_desc(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT texture_footprint = {};
    UINT texture_rows = 0;
    UINT64 texture_row_bytes = 0;
    UINT64 texture_upload_bytes = 0;
    if (texture_case)
        device->GetCopyableFootprints(&texture_desc, 0, 1, 0, &texture_footprint, &texture_rows, &texture_row_bytes,
                                      &texture_upload_bytes);
    D3D12_RESOURCE_DESC texture_upload_desc = buffer_desc(texture_upload_bytes ? texture_upload_bytes : 256);

    const uint32_t* input_data[3] = {input0, input1, input2};
    bool resources_ok = true;
    for (UINT i = 0; i < 3; ++i) {
        HRESULT hr =
            device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &input_desc,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&inputs[i]));
        resources_ok =
            resources_ok && SUCCEEDED(hr) && inputs[i] && fill_upload_buffer(inputs[i], input_data[i], kInputWords);
    }
    HRESULT output_hr =
        device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &output_desc,
                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output));
    HRESULT readback_hr =
        device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback));
    resources_ok = resources_ok && SUCCEEDED(output_hr) && output && SUCCEEDED(readback_hr) && readback;

    if (texture_case) {
        HRESULT texture_hr =
            device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture));
        HRESULT texture_upload_hr =
            device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &texture_upload_desc,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&texture_upload));
        resources_ok =
            resources_ok && SUCCEEDED(texture_hr) && texture && SUCCEEDED(texture_upload_hr) && texture_upload;
        if (texture_upload) {
            void* mapped = nullptr;
            D3D12_RANGE no_read = {0, 0};
            HRESULT map_hr = texture_upload->Map(0, &no_read, &mapped);
            if (SUCCEEDED(map_hr) && mapped) {
                std::memset(mapped, 0, static_cast<size_t>(texture_upload_bytes ? texture_upload_bytes : 256));
                uint8_t* bytes = static_cast<uint8_t*>(mapped) + texture_footprint.Offset;
                bytes[0] = 1;
                bytes[1] = 2;
                bytes[2] = 3;
                bytes[3] = 4;
                texture_upload->Unmap(0, nullptr);
            } else {
                resources_ok = false;
            }
        }
    }

    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = 4;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT heap_hr = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap));
    UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    bool descriptors_ok = SUCCEEDED(heap_hr) && heap && inc != 0;
    if (descriptors_ok) {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_R32_TYPELESS;
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav.Buffer.NumElements = static_cast<UINT>(kOutputBytes / 4);
        uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        device->CreateUnorderedAccessView(output, nullptr, &uav, offset_cpu(cpu, inc, 0));
        const UINT raw_srv_count = texture_case ? 2u : 3u;
        for (UINT i = 0; i < raw_srv_count; ++i) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
            srv.Format = DXGI_FORMAT_R32_TYPELESS;
            srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Buffer.NumElements = static_cast<UINT>(kInputBytes / 4);
            srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            device->CreateShaderResourceView(inputs[i], &srv, offset_cpu(cpu, inc, i + 1));
        }
        if (texture_case) {
            D3D12_SHADER_RESOURCE_VIEW_DESC texture_srv = {};
            texture_srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            texture_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            texture_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            texture_srv.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(texture, &texture_srv, offset_cpu(cpu, inc, 3));

            D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {};
            sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            sampler_heap_desc.NumDescriptors = 1;
            sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            HRESULT sampler_heap_hr = device->CreateDescriptorHeap(&sampler_heap_desc, IID_PPV_ARGS(&sampler_heap));
            descriptors_ok = descriptors_ok && SUCCEEDED(sampler_heap_hr) && sampler_heap;
            if (sampler_heap) {
                D3D12_SAMPLER_DESC sampler = {};
                sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
                sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                sampler.MinLOD = 0.0f;
                sampler.MaxLOD = D3D12_FLOAT32_MAX;
                device->CreateSampler(&sampler, sampler_heap->GetCPUDescriptorHandleForHeapStart());
            }
        }
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HRESULT queue_hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue));
    HRESULT allocator_hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    HRESULT list_hr =
        device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list));

    UINT64 completed_value = 0;
    if (resources_ok && descriptors_ok && SUCCEEDED(queue_hr) && SUCCEEDED(allocator_hr) && SUCCEEDED(list_hr) &&
        queue && list && heap) {
        ID3D12DescriptorHeap* heaps[] = {heap, sampler_heap};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
        const UINT constants[4] = {1u, 5u, 3u, 0u};
        list->SetDescriptorHeaps(texture_case ? 2u : 1u, heaps);
        if (texture_case) {
            D3D12_TEXTURE_COPY_LOCATION dst = {};
            dst.pResource = texture;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = 0;
            D3D12_TEXTURE_COPY_LOCATION src = {};
            src.pResource = texture_upload;
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = texture_footprint;
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            D3D12_RESOURCE_BARRIER texture_ready = transition_barrier(texture, D3D12_RESOURCE_STATE_COPY_DEST,
                                                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            list->ResourceBarrier(1, &texture_ready);
        }
        list->SetComputeRootSignature(root);
        list->SetPipelineState(pso);
        list->SetComputeRootDescriptorTable(0, offset_gpu(gpu, inc, 0));
        list->SetComputeRootDescriptorTable(1, offset_gpu(gpu, inc, 1));
        if (texture_case && sampler_heap)
            list->SetComputeRootDescriptorTable(2, sampler_heap->GetGPUDescriptorHandleForHeapStart());
        list->SetComputeRoot32BitConstants(3, 4, constants, 0);
        list->Dispatch(1, 1, 1);
        D3D12_RESOURCE_BARRIER barrier = uav_barrier(output);
        list->ResourceBarrier(1, &barrier);
        D3D12_RESOURCE_BARRIER to_copy =
            transition_barrier(output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &to_copy);
        list->CopyBufferRegion(readback, 0, output, 0, kOutputBytes);
        result.execute_hr = execute_and_wait(device, queue, list, 15000, completed_value);
    }

    if (readback && completed_value >= 1 && SUCCEEDED(result.execute_hr)) {
        std::vector<uint32_t> values(result.expected.size());
        void* mapped = nullptr;
        D3D12_RANGE range = {0, values.size() * sizeof(uint32_t)};
        HRESULT map_hr = readback->Map(0, &range, &mapped);
        if (SUCCEEDED(map_hr) && mapped) {
            std::memcpy(values.data(), mapped, values.size() * sizeof(uint32_t));
            D3D12_RANGE no_write = {0, 0};
            readback->Unmap(0, &no_write);
            result.actual = values;
            result.readback_ok = true;
            result.values_ok = result.actual == result.expected;
        }
    }

    for (auto*& input : inputs)
        safe_release(input);
    safe_release(output);
    safe_release(readback);
    safe_release(texture);
    safe_release(texture_upload);
    safe_release(heap);
    safe_release(sampler_heap);
    safe_release(list);
    safe_release(allocator);
    safe_release(queue);

    result.runtime_executed = SUCCEEDED(result.execute_hr) && completed_value >= 1 && result.readback_ok;
    result.detail =
        result.values_ok ? "validated expected UAV readback" : "runtime readback did not match expected values";
    return result.values_ok;
}

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

static CaseResult run_case(ID3D12Device* device, ID3D12RootSignature* root, const AuditCase& audit_case,
                           bool execute_runtime) {
    CaseResult result;
    result.name = audit_case.name;
    result.category = audit_case.category;

    const std::string base = std::string("dxmt_sm66_") + audit_case.entry;
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
    command += " dxmt_sm66_capabilities.hlsl";

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
        if (result.pso_created && execute_runtime)
            execute_runtime_case(device, root, pso, audit_case, result);
        safe_release(pso);
    }

    std::string dxc_errors = read_text_file(error_path.c_str());
    if (!result.compile_ok)
        result.detail = dxc_errors.empty() ? "DXC did not produce a DXIL blob" : dxc_errors;
    else if (!result.pso_created)
        result.detail = "compiled to DXIL, but no linked compute PSO was created";
    else if (audit_case.requires_runtime_proof && !result.runtime_executed)
        result.detail =
            result.detail.empty()
                ? "compiled and linked; runtime execution proof is still required before SM 6.6 can be reported"
                : result.detail;
    else if (!audit_case.requires_runtime_proof)
        result.detail = "negative capability case recorded";

    return result;
}

int main() {
    const bool warmup_only = getenv_string("D3D12_METAL_SDK_SM66_MODE") == "warmup";
    const char* hlsl_path = "dxmt_sm66_capabilities.hlsl";
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

[numthreads(1, 1, 1)]
void cs_atomic64_raw_uav(uint3 id : SV_DispatchThreadID) {
  outbuf.Store2(0, uint2(5u, 0u));
  uint64_t oldValue;
  outbuf.InterlockedAdd64(0, (uint64_t)10u, oldValue);
  outbuf.Store2(8, uint2((uint)(oldValue & 0xffffffffu), (uint)(oldValue >> 32)));
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
        {"atomic64_raw_uav", "cs_atomic64_raw_uav", "atomic64_raw_uav", true},
        {"texture_sampler", "cs_texture_sampler", "samplers_texture_paths", true},
    };

    std::vector<CaseResult> results;
    if (hlsl_written) {
        for (const auto& audit_case : audit_cases)
            results.push_back(run_case(device, root, audit_case, !warmup_only));
    }

    bool entrypoints_ok =
        d3d12 && dxcompiler && dxil && create_device && serialize && SUCCEEDED(create_hr) && SUCCEEDED(root_hr);
    bool compiler_acceptance_complete = hlsl_written && !results.empty();
    bool pso_link_complete = compiler_acceptance_complete;
    bool runtime_complete = compiler_acceptance_complete;
    for (const auto& result : results) {
        compiler_acceptance_complete = compiler_acceptance_complete && result.compile_ok;
        pso_link_complete = pso_link_complete && result.pso_created;
        runtime_complete = runtime_complete && result.runtime_executed && result.values_ok;
    }

    bool atomic64_conservative = (!SUCCEEDED(options9_hr) || (!options9.AtomicInt64OnTypedResourceSupported &&
                                                              !options9.AtomicInt64OnGroupSharedSupported)) &&
                                 (!SUCCEEDED(options11_hr) || !options11.AtomicInt64OnDescriptorHeapResourceSupported);
    bool int64_feature_reported = SUCCEEDED(options1_hr) && options1.Int64ShaderOps;
    bool sm66_reportable = compiler_acceptance_complete && pso_link_complete && runtime_complete &&
                           int64_feature_reported && !atomic64_conservative;
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
    const char* decision = sm66_reportable
                               ? "SM 6.6 may be reported"
                               : (runtime_complete ? "Runtime cases passed, but feature caps remain conservative; do "
                                                     "not report SM 6.6/int64/atomic64 yet"
                                                   : "SM 6.6 must not be reported until runtime cases execute");
    std::printf("    \"decision\": \"%s\"\n", decision);
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
        std::printf("      \"execute_hr\": \"%s\",\n", hr_hex(result.execute_hr).c_str());
        std::printf("      \"readback_ok\": %s,\n", result.readback_ok ? "true" : "false");
        std::printf("      \"values_ok\": %s,\n", result.values_ok ? "true" : "false");
        std::printf("      \"expected\": %s,\n", json_u32_array(result.expected).c_str());
        std::printf("      \"actual\": %s,\n", json_u32_array(result.actual).c_str());
        std::printf("      \"detail\": \"%s\"\n", json_escape(result.detail).c_str());
        std::printf("    }%s\n", i + 1 == results.size() ? "" : ",");
    }
    std::printf("  ]\n");
    std::printf("}\n");

    std::fflush(stdout);
    // Wine/MinGW can assert during late CRT condition-variable teardown after
    // the DXMT worker stack has already produced the contract JSON.
    TerminateProcess(GetCurrentProcess(), pass ? 0u : 1u);
    safe_release(root);
    safe_release(device);
    return 0;
}
