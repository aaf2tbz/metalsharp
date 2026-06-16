#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include <d3d12.h>

static const GUID IID_D3D12DeviceProbe = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};

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

static D3D12_RESOURCE_BARRIER aliasing_barrier(ID3D12Resource* before, ID3D12Resource* after) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Aliasing.pResourceBefore = before;
    barrier.Aliasing.pResourceAfter = after;
    return barrier;
}

static void print_hr_field(const char* key, HRESULT hr, bool comma = true) {
    std::printf("    \"%s\": \"0x%08lx\"%s\n", key, static_cast<unsigned long>(static_cast<uint32_t>(hr)),
                comma ? "," : "");
}

int main() {
    std::string profile = getenv_string("D3D12_METAL_SDK_PROFILE");

    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    auto create_device = reinterpret_cast<CreateDeviceFn>(
        reinterpret_cast<void*>(d3d12 ? GetProcAddress(d3d12, "D3D12CreateDevice") : nullptr));

    ID3D12Device* device = nullptr;
    HRESULT create_hr = create_device ? create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_D3D12DeviceProbe,
                                                      reinterpret_cast<void**>(&device))
                                      : E_FAIL;

    const UINT64 bytes = 4096;
    D3D12_RESOURCE_DESC desc = buffer_desc(bytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    D3D12_RESOURCE_ALLOCATION_INFO alloc_info = {};
    if (device)
        alloc_info = device->GetResourceAllocationInfo(0, 1, &desc);

    D3D12_HEAP_DESC heap_desc = {};
    heap_desc.SizeInBytes = alloc_info.SizeInBytes ? alloc_info.SizeInBytes : 65536;
    heap_desc.Properties = heap_props(D3D12_HEAP_TYPE_DEFAULT);
    heap_desc.Alignment = alloc_info.Alignment;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

    ID3D12Heap* heap = nullptr;
    HRESULT create_heap_hr = device ? device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap)) : E_FAIL;

    ID3D12Resource* placed_a = nullptr;
    ID3D12Resource* placed_b = nullptr;
    HRESULT placed_a_hr = heap ? device->CreatePlacedResource(heap, 0, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                              IID_PPV_ARGS(&placed_a))
                               : E_FAIL;
    HRESULT placed_b_hr = heap ? device->CreatePlacedResource(heap, 0, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                              IID_PPV_ARGS(&placed_b))
                               : E_FAIL;

    D3D12_RESOURCE_DESC staging_desc = buffer_desc(bytes);
    D3D12_HEAP_PROPERTIES upload_props = heap_props(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_HEAP_PROPERTIES readback_props = heap_props(D3D12_HEAP_TYPE_READBACK);
    ID3D12Resource* upload = nullptr;
    ID3D12Resource* readback = nullptr;
    HRESULT upload_hr =
        device ? device->CreateCommittedResource(&upload_props, D3D12_HEAP_FLAG_NONE, &staging_desc,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))
               : E_FAIL;
    HRESULT readback_hr =
        device ? device->CreateCommittedResource(&readback_props, D3D12_HEAP_FLAG_NONE, &staging_desc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))
               : E_FAIL;

    uint8_t* upload_ptr = nullptr;
    HRESULT map_upload_hr = upload ? upload->Map(0, nullptr, reinterpret_cast<void**>(&upload_ptr)) : E_FAIL;
    if (SUCCEEDED(map_upload_hr) && upload_ptr) {
        for (UINT64 i = 0; i < bytes; ++i)
            upload_ptr[i] = static_cast<uint8_t>((i * 13u + 7u) & 0xffu);
        upload->Unmap(0, nullptr);
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    HRESULT queue_hr = device ? device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)) : E_FAIL;
    HRESULT allocator_hr =
        device ? device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)) : E_FAIL;
    HRESULT list_hr =
        device ? device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list))
               : E_FAIL;
    HRESULT fence_hr = device ? device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)) : E_FAIL;

    bool recorded_aliasing_barrier = false;
    bool recorded_copy_after_alias = false;
    HRESULT close_hr = E_FAIL;
    HRESULT execute_signal_hr = E_FAIL;
    HRESULT wait_hr = E_FAIL;
    UINT64 completed_value = 0;
    if (list && upload && readback && placed_a && placed_b) {
        list->CopyBufferRegion(placed_a, 0, upload, 0, bytes);
        D3D12_RESOURCE_BARRIER to_copy_source =
            transition_barrier(placed_a, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &to_copy_source);
        list->CopyBufferRegion(readback, 0, placed_a, 0, bytes);

        D3D12_RESOURCE_BARRIER alias = aliasing_barrier(placed_a, placed_b);
        list->ResourceBarrier(1, &alias);
        recorded_aliasing_barrier = true;

        list->CopyBufferRegion(placed_b, 0, upload, 0, bytes);
        D3D12_RESOURCE_BARRIER b_to_copy_source =
            transition_barrier(placed_b, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &b_to_copy_source);
        list->CopyBufferRegion(readback, 0, placed_b, 0, bytes);
        recorded_copy_after_alias = true;
    }
    if (list)
        close_hr = list->Close();
    if (queue && list && fence && SUCCEEDED(close_hr)) {
        ID3D12CommandList* lists[] = {list};
        queue->ExecuteCommandLists(1, lists);
        execute_signal_hr = queue->Signal(fence, 1);
        if (SUCCEEDED(execute_signal_hr)) {
            HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
            if (event) {
                if (fence->GetCompletedValue() < 1)
                    fence->SetEventOnCompletion(1, event);
                DWORD wait_result = WaitForSingleObject(event, 5000);
                wait_hr = wait_result == WAIT_OBJECT_0 ? S_OK : HRESULT_FROM_WIN32(wait_result);
                CloseHandle(event);
            } else {
                wait_hr = HRESULT_FROM_WIN32(GetLastError());
            }
        }
        completed_value = fence->GetCompletedValue();
        if (completed_value >= 1)
            wait_hr = S_OK;
    }

    bool pass = SUCCEEDED(create_hr) && SUCCEEDED(create_heap_hr) && SUCCEEDED(placed_a_hr) && SUCCEEDED(placed_b_hr) &&
                SUCCEEDED(upload_hr) && SUCCEEDED(readback_hr) && SUCCEEDED(map_upload_hr) && SUCCEEDED(queue_hr) &&
                SUCCEEDED(allocator_hr) && SUCCEEDED(list_hr) && SUCCEEDED(fence_hr) && recorded_aliasing_barrier &&
                recorded_copy_after_alias && SUCCEEDED(close_hr) && SUCCEEDED(execute_signal_hr) &&
                SUCCEEDED(wait_hr) && completed_value >= 1;

    std::printf("{\n");
    std::printf("  \"schema\": \"metalsharp.d3d12-metal.probe-heap-aliasing.v1\",\n");
    std::printf("  \"profile\": \"%s\",\n", json_escape(profile).c_str());
    std::printf("  \"pass\": %s,\n", pass ? "true" : "false");
    std::printf("  \"heap\": {\n");
    std::printf("    \"allocation_size\": %llu,\n", static_cast<unsigned long long>(alloc_info.SizeInBytes));
    std::printf("    \"allocation_alignment\": %llu,\n", static_cast<unsigned long long>(alloc_info.Alignment));
    print_hr_field("create_heap_hr", create_heap_hr, false);
    std::printf("  },\n");
    std::printf("  \"resources\": {\n");
    print_hr_field("create_placed_a_hr", placed_a_hr);
    print_hr_field("create_placed_b_same_offset_hr", placed_b_hr);
    print_hr_field("create_upload_hr", upload_hr);
    print_hr_field("create_readback_hr", readback_hr);
    print_hr_field("map_upload_hr", map_upload_hr, false);
    std::printf("  },\n");
    std::printf("  \"commands\": {\n");
    std::printf("    \"recorded_aliasing_barrier\": %s,\n", recorded_aliasing_barrier ? "true" : "false");
    std::printf("    \"recorded_copy_after_alias\": %s,\n", recorded_copy_after_alias ? "true" : "false");
    print_hr_field("close_hr", close_hr);
    print_hr_field("execute_signal_hr", execute_signal_hr);
    print_hr_field("wait_hr", wait_hr);
    std::printf("    \"completed_value\": %llu\n", static_cast<unsigned long long>(completed_value));
    std::printf("  },\n");
    std::printf("  \"apple_phase35_mapping\": {\n");
    std::printf("    \"heap_aliasing\": true,\n");
    std::printf("    \"explicit_aliasing_barrier\": %s,\n", recorded_aliasing_barrier ? "true" : "false");
    std::printf("    \"queue_fence_completion\": %s\n", completed_value >= 1 ? "true" : "false");
    std::printf("  }\n");
    std::printf("}\n");
    std::fflush(stdout);

    if (fence)
        fence->Release();
    if (list)
        list->Release();
    if (allocator)
        allocator->Release();
    if (queue)
        queue->Release();
    if (readback)
        readback->Release();
    if (upload)
        upload->Release();
    if (placed_b)
        placed_b->Release();
    if (placed_a)
        placed_a->Release();
    if (heap)
        heap->Release();
    if (device)
        device->Release();

    TerminateProcess(GetCurrentProcess(), pass ? 0 : 1);
}
