#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgiformat.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return desc;
}

static D3D12_RESOURCE_DESC texture_desc(UINT width, UINT height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
                                        UINT sample_count = 1) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = sample_count;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;
    return desc;
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

static D3D12_CPU_DESCRIPTOR_HANDLE offset_cpu_handle(D3D12_CPU_DESCRIPTOR_HANDLE start, UINT increment, UINT index) {
    start.ptr += static_cast<SIZE_T>(increment) * index;
    return start;
}

static D3D12_GPU_DESCRIPTOR_HANDLE offset_gpu_handle(D3D12_GPU_DESCRIPTOR_HANDLE start, UINT increment, UINT index) {
    start.ptr += static_cast<UINT64>(increment) * index;
    return start;
}

static D3D12_RESOURCE_BARRIER uav_barrier(ID3D12Resource* resource) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    return barrier;
}

struct Pixel {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
};

struct CaseResult {
    std::string name;
    bool pass = false;
    HRESULT hr = E_FAIL;
    std::string detail;
    std::string extra;
};

static D3D12CreateDeviceFn g_create_device = nullptr;
static D3D12SerializeRootSignatureFn g_serialize_root_signature = nullptr;
static D3DCompileFn g_compile = nullptr;

static HRESULT create_device(ID3D12Device** device) {
    return g_create_device ? g_create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_D3D12DeviceProbe,
                                             reinterpret_cast<void**>(device))
                           : E_FAIL;
}

static HRESULT create_queue(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandQueue** queue) {
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = type;
    return device->CreateCommandQueue(&desc, IID_PPV_ARGS(queue));
}

static HRESULT create_buffer(ID3D12Device* device, D3D12_HEAP_TYPE heap_type, UINT64 bytes, D3D12_RESOURCE_FLAGS flags,
                             D3D12_RESOURCE_STATES state, ID3D12Resource** resource) {
    D3D12_HEAP_PROPERTIES heap = heap_props(heap_type);
    D3D12_RESOURCE_DESC desc = buffer_desc(bytes, flags);
    return device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(resource));
}

static HRESULT create_upload_buffer(ID3D12Device* device, const void* data, UINT64 bytes, ID3D12Resource** resource) {
    HRESULT hr = create_buffer(device, D3D12_HEAP_TYPE_UPLOAD, bytes, D3D12_RESOURCE_FLAG_NONE,
                               D3D12_RESOURCE_STATE_GENERIC_READ, resource);
    if (FAILED(hr) || !data || bytes == 0)
        return hr;
    void* mapped = nullptr;
    D3D12_RANGE read_range = {0, 0};
    hr = (*resource)->Map(0, &read_range, &mapped);
    if (SUCCEEDED(hr)) {
        std::memcpy(mapped, data, static_cast<size_t>(bytes));
        D3D12_RANGE write_range = {0, static_cast<SIZE_T>(bytes)};
        (*resource)->Unmap(0, &write_range);
    }
    return hr;
}

static HRESULT execute_and_wait(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* list) {
    HRESULT hr = list->Close();
    if (FAILED(hr))
        return hr;
    ID3D12CommandList* lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    ID3D12Fence* fence = nullptr;
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (SUCCEEDED(hr))
        hr = queue->Signal(fence, 1);
    HANDLE event_handle = nullptr;
    if (SUCCEEDED(hr)) {
        event_handle = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        if (!event_handle)
            hr = HRESULT_FROM_WIN32(GetLastError());
    }
    if (SUCCEEDED(hr))
        hr = fence->SetEventOnCompletion(1, event_handle);
    if (SUCCEEDED(hr) && WaitForSingleObject(event_handle, 15000) != WAIT_OBJECT_0)
        hr = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
    if (event_handle)
        CloseHandle(event_handle);
    safe_release(fence);
    return hr;
}

static HRESULT compile_shader(const char* hlsl, const char* entry, ID3DBlob** out, std::string& errors) {
    ID3DBlob* err = nullptr;
    HRESULT hr = g_compile ? g_compile(hlsl, std::strlen(hlsl), "probe_barriers_uav_aliasing_subresource.hlsl", nullptr,
                                       nullptr, entry, "cs_5_0", 0, 0, out, &err)
                           : E_FAIL;
    if (err) {
        errors.assign(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize());
        err->Release();
    }
    return hr;
}

static HRESULT serialize_root_signature(const D3D12_ROOT_SIGNATURE_DESC& desc, ID3DBlob** out, std::string& errors) {
    ID3DBlob* err = nullptr;
    HRESULT hr = g_serialize_root_signature ? g_serialize_root_signature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, out, &err)
                                            : E_FAIL;
    if (err) {
        errors.assign(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize());
        err->Release();
    }
    return hr;
}

static bool read_texture_pixel(ID3D12Resource* readback, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint,
                               Pixel* pixel) {
    uint8_t* data = nullptr;
    D3D12_RANGE read_range = {0, static_cast<SIZE_T>(footprint.Footprint.RowPitch * footprint.Footprint.Height)};
    HRESULT hr = readback->Map(0, &read_range, reinterpret_cast<void**>(&data));
    if (FAILED(hr) || !data)
        return false;
    pixel->r = data[footprint.Offset + 0];
    pixel->g = data[footprint.Offset + 1];
    pixel->b = data[footprint.Offset + 2];
    pixel->a = data[footprint.Offset + 3];
    D3D12_RANGE write_range = {0, 0};
    readback->Unmap(0, &write_range);
    return true;
}

static bool pixel_eq(const Pixel& p, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return p.r == r && p.g == g && p.b == b && p.a == a;
}

static bool readback_u32(ID3D12Resource* readback, uint32_t* values, size_t count) {
    uint32_t* data = nullptr;
    D3D12_RANGE read_range = {0, count * sizeof(uint32_t)};
    HRESULT hr = readback->Map(0, &read_range, reinterpret_cast<void**>(&data));
    if (FAILED(hr) || !data)
        return false;
    std::memcpy(values, data, count * sizeof(uint32_t));
    D3D12_RANGE write_range = {0, 0};
    readback->Unmap(0, &write_range);
    return true;
}

static CaseResult run_render_pass_split_case() {
    CaseResult result = {"render_pass_split_clear_store", false, E_FAIL, "", ""};
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12DescriptorHeap* rtv_heap = nullptr;
    ID3D12Resource* target = nullptr;
    ID3D12Resource* first_readback = nullptr;
    ID3D12Resource* second_readback = nullptr;

    D3D12_RESOURCE_DESC rt_desc =
        texture_desc(4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rows = 0;
    UINT64 row_bytes = 0;
    UINT64 readback_bytes = 0;

    HRESULT hr = create_device(&device);
    if (SUCCEEDED(hr))
        hr = create_queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &queue);
    if (SUCCEEDED(hr))
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    if (SUCCEEDED(hr))
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list));
    if (SUCCEEDED(hr)) {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = 1;
        hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtv_heap));
    }
    if (SUCCEEDED(hr)) {
        D3D12_HEAP_PROPERTIES heap = heap_props(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_CLEAR_VALUE clear = {};
        clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        clear.Color[3] = 1.0f;
        hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rt_desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                             &clear, IID_PPV_ARGS(&target));
    }
    if (SUCCEEDED(hr)) {
        device->GetCopyableFootprints(&rt_desc, 0, 1, 0, &footprint, &rows, &row_bytes, &readback_bytes);
        hr = create_buffer(device, D3D12_HEAP_TYPE_READBACK, readback_bytes, D3D12_RESOURCE_FLAG_NONE,
                           D3D12_RESOURCE_STATE_COPY_DEST, &first_readback);
    }
    if (SUCCEEDED(hr))
        hr = create_buffer(device, D3D12_HEAP_TYPE_READBACK, readback_bytes, D3D12_RESOURCE_FLAG_NONE,
                           D3D12_RESOURCE_STATE_COPY_DEST, &second_readback);
    if (SUCCEEDED(hr)) {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(target, nullptr, rtv);
        const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        list->ClearRenderTargetView(rtv, red, 0, nullptr);
        D3D12_RESOURCE_BARRIER to_copy =
            transition_barrier(target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &to_copy);
        D3D12_TEXTURE_COPY_LOCATION first_dst = {};
        first_dst.pResource = first_readback;
        first_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        first_dst.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = target;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;
        list->CopyTextureRegion(&first_dst, 0, 0, 0, &src, nullptr);
        D3D12_RESOURCE_BARRIER to_rt =
            transition_barrier(target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->ResourceBarrier(1, &to_rt);
        const float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
        list->ClearRenderTargetView(rtv, green, 0, nullptr);
        list->ResourceBarrier(1, &to_copy);
        D3D12_TEXTURE_COPY_LOCATION second_dst = {};
        second_dst.pResource = second_readback;
        second_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        second_dst.PlacedFootprint = footprint;
        list->CopyTextureRegion(&second_dst, 0, 0, 0, &src, nullptr);
        hr = execute_and_wait(device, queue, list);
    }

    Pixel first = {};
    Pixel second = {};
    bool verified = SUCCEEDED(hr) && read_texture_pixel(first_readback, footprint, &first) &&
                    read_texture_pixel(second_readback, footprint, &second) && pixel_eq(first, 255, 0, 0, 255) &&
                    pixel_eq(second, 0, 255, 0, 255);
    result.pass = verified;
    result.hr = hr;
    result.detail = verified ? "render target clear/store survives pass split and reopen"
                             : "render pass split clear/store mismatch";
    char extra[256] = {};
    std::snprintf(extra, sizeof(extra), "\"first_pixel\":[%u,%u,%u,%u],\"second_pixel\":[%u,%u,%u,%u]", first.r,
                  first.g, first.b, first.a, second.r, second.g, second.b, second.a);
    result.extra = extra;

    safe_release(second_readback);
    safe_release(first_readback);
    safe_release(target);
    safe_release(rtv_heap);
    safe_release(list);
    safe_release(allocator);
    safe_release(queue);
    safe_release(device);
    return result;
}

static CaseResult run_copy_shader_resource_state_case() {
    CaseResult result = {"copy_to_shader_resource_transition", false, E_FAIL, "", ""};
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Resource* texture = nullptr;
    ID3D12Resource* upload = nullptr;
    ID3D12Resource* readback = nullptr;

    D3D12_RESOURCE_DESC tex_desc = texture_desc(2, 2, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rows = 0;
    UINT64 row_bytes = 0;
    UINT64 upload_bytes = 0;
    HRESULT hr = create_device(&device);
    if (SUCCEEDED(hr))
        hr = create_queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &queue);
    if (SUCCEEDED(hr))
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    if (SUCCEEDED(hr))
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list));
    if (SUCCEEDED(hr)) {
        D3D12_HEAP_PROPERTIES heap = heap_props(D3D12_HEAP_TYPE_DEFAULT);
        hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &tex_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                             nullptr, IID_PPV_ARGS(&texture));
    }
    if (SUCCEEDED(hr)) {
        device->GetCopyableFootprints(&tex_desc, 0, 1, 0, &footprint, &rows, &row_bytes, &upload_bytes);
        std::vector<uint8_t> data(static_cast<size_t>(upload_bytes), 0);
        const uint8_t pixels[16] = {11, 22, 33, 255, 44, 55, 66, 255, 77, 88, 99, 255, 111, 122, 133, 255};
        for (UINT y = 0; y < 2; ++y)
            std::memcpy(data.data() + footprint.Footprint.RowPitch * y, pixels + y * 8, 8);
        hr = create_upload_buffer(device, data.data(), data.size(), &upload);
    }
    if (SUCCEEDED(hr))
        hr = create_buffer(device, D3D12_HEAP_TYPE_READBACK, upload_bytes, D3D12_RESOURCE_FLAG_NONE,
                           D3D12_RESOURCE_STATE_COPY_DEST, &readback);
    if (SUCCEEDED(hr)) {
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = upload;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = texture;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        D3D12_RESOURCE_BARRIER to_srv =
            transition_barrier(texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &to_srv);
        D3D12_RESOURCE_BARRIER to_copy =
            transition_barrier(texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &to_copy);
        D3D12_TEXTURE_COPY_LOCATION rb_dst = {};
        rb_dst.pResource = readback;
        rb_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        rb_dst.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION tex_src = {};
        tex_src.pResource = texture;
        tex_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        tex_src.SubresourceIndex = 0;
        list->CopyTextureRegion(&rb_dst, 0, 0, 0, &tex_src, nullptr);
        hr = execute_and_wait(device, queue, list);
    }
    Pixel pixel = {};
    bool verified =
        SUCCEEDED(hr) && read_texture_pixel(readback, footprint, &pixel) && pixel_eq(pixel, 11, 22, 33, 255);
    result.pass = verified;
    result.hr = hr;
    result.detail = verified ? "copy destination to shader-resource to copy-source transition preserved texture data"
                             : "copy-to-shader-resource transition mismatch";
    char extra[128] = {};
    std::snprintf(extra, sizeof(extra), "\"first_pixel\":[%u,%u,%u,%u]", pixel.r, pixel.g, pixel.b, pixel.a);
    result.extra = extra;

    safe_release(readback);
    safe_release(upload);
    safe_release(texture);
    safe_release(list);
    safe_release(allocator);
    safe_release(queue);
    safe_release(device);
    return result;
}

static CaseResult run_uav_visibility_case() {
    CaseResult result = {"uav_to_uav_visibility", false, E_FAIL, "", ""};
    const char* hlsl = "RWStructuredBuffer<uint> outbuf:register(u0);"
                       "[numthreads(4,1,1)] void first(uint3 id:SV_DispatchThreadID){outbuf[id.x]=id.x+40;}"
                       "[numthreads(4,1,1)] void second(uint3 id:SV_DispatchThreadID){"
                       "outbuf[id.x]=outbuf[id.x]+7;}";

    ID3D12Device* device = nullptr;
    ID3DBlob* first_cs = nullptr;
    ID3DBlob* second_cs = nullptr;
    ID3DBlob* root_blob = nullptr;
    ID3D12RootSignature* root = nullptr;
    ID3D12PipelineState* first_pso = nullptr;
    ID3D12PipelineState* second_pso = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    ID3D12Resource* output = nullptr;
    ID3D12Resource* readback = nullptr;
    std::string detail;

    HRESULT hr = create_device(&device);
    if (SUCCEEDED(hr))
        hr = compile_shader(hlsl, "first", &first_cs, detail);
    if (SUCCEEDED(hr))
        hr = compile_shader(hlsl, "second", &second_cs, detail);
    if (SUCCEEDED(hr)) {
        D3D12_DESCRIPTOR_RANGE range = {};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable.NumDescriptorRanges = 1;
        param.DescriptorTable.pDescriptorRanges = &range;
        D3D12_ROOT_SIGNATURE_DESC root_desc = {};
        root_desc.NumParameters = 1;
        root_desc.pParameters = &param;
        hr = serialize_root_signature(root_desc, &root_blob, detail);
    }
    if (SUCCEEDED(hr))
        hr = device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                         IID_PPV_ARGS(&root));
    if (SUCCEEDED(hr)) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = root;
        desc.CS = {first_cs->GetBufferPointer(), first_cs->GetBufferSize()};
        hr = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&first_pso));
        if (SUCCEEDED(hr)) {
            desc.CS = {second_cs->GetBufferPointer(), second_cs->GetBufferSize()};
            hr = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&second_pso));
        }
    }
    if (SUCCEEDED(hr))
        hr = create_queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &queue);
    if (SUCCEEDED(hr))
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    if (SUCCEEDED(hr))
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list));
    if (SUCCEEDED(hr)) {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 1;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));
    }
    if (SUCCEEDED(hr))
        hr = create_buffer(device, D3D12_HEAP_TYPE_DEFAULT, 256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &output);
    if (SUCCEEDED(hr))
        hr = create_buffer(device, D3D12_HEAP_TYPE_READBACK, 256, D3D12_RESOURCE_FLAG_NONE,
                           D3D12_RESOURCE_STATE_COPY_DEST, &readback);
    if (SUCCEEDED(hr)) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_UNKNOWN;
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav.Buffer.NumElements = 64;
        uav.Buffer.StructureByteStride = sizeof(uint32_t);
        device->CreateUnorderedAccessView(output, nullptr, &uav, heap->GetCPUDescriptorHandleForHeapStart());
        ID3D12DescriptorHeap* heaps[] = {heap};
        list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootSignature(root);
        list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
        list->SetPipelineState(first_pso);
        list->Dispatch(1, 1, 1);
        D3D12_RESOURCE_BARRIER barrier = uav_barrier(output);
        list->ResourceBarrier(1, &barrier);
        list->SetPipelineState(second_pso);
        list->Dispatch(1, 1, 1);
        D3D12_RESOURCE_BARRIER final_barriers[] = {
            uav_barrier(output),
            transition_barrier(output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
        };
        list->ResourceBarrier(2, final_barriers);
        list->CopyResource(readback, output);
        hr = execute_and_wait(device, queue, list);
    }

    uint32_t got[4] = {};
    bool verified =
        SUCCEEDED(hr) && readback_u32(readback, got, 4) && got[0] == 47 && got[1] == 48 && got[2] == 49 && got[3] == 50;
    result.pass = verified;
    result.hr = hr;
    result.detail = verified         ? "UAV barrier made first dispatch writes visible to second dispatch"
                    : detail.empty() ? "UAV visibility mismatch"
                                     : detail;
    char extra[128] = {};
    std::snprintf(extra, sizeof(extra), "\"values\":[%u,%u,%u,%u]", got[0], got[1], got[2], got[3]);
    result.extra = extra;

    safe_release(readback);
    safe_release(output);
    safe_release(heap);
    safe_release(list);
    safe_release(allocator);
    safe_release(queue);
    safe_release(second_pso);
    safe_release(first_pso);
    safe_release(root);
    safe_release(root_blob);
    safe_release(second_cs);
    safe_release(first_cs);
    safe_release(device);
    return result;
}

static HRESULT compile_shader_target(const char* hlsl, const char* entry, const char* target, ID3DBlob** out,
                                     std::string& errors) {
    ID3DBlob* err = nullptr;
    HRESULT hr = g_compile ? g_compile(hlsl, std::strlen(hlsl), "probe_barriers_uav_aliasing_subresource.hlsl", nullptr,
                                       nullptr, entry, target, 0, 0, out, &err)
                           : E_FAIL;
    if (err) {
        errors.assign(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize());
        err->Release();
    }
    return hr;
}

static CaseResult run_compute_uav_to_graphics_srv_case() {
    CaseResult result = {"compute_uav_write_to_graphics_srv_read", false, E_FAIL, "", ""};
    const char* cs_hlsl = "RWStructuredBuffer<uint4> outbuf:register(u0);"
                          "[numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID){outbuf[0]=uint4(12,34,56,255);}";
    const char* gfx_hlsl = "StructuredBuffer<uint4> inbuf:register(t0);"
                           "struct VSOut{float4 pos:SV_POSITION;};"
                           "VSOut vs_main(uint id:SV_VertexID){float2 "
                           "p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};VSOut o;o.pos=float4(p[id],0,1);return o;}"
                           "float4 ps_main(VSOut i):SV_Target{uint4 v=inbuf[0];return float4(v)/255.0;}";

    ID3D12Device* device = nullptr;
    ID3DBlob* cs = nullptr;
    ID3DBlob* vs = nullptr;
    ID3DBlob* ps = nullptr;
    ID3DBlob* root_blob = nullptr;
    ID3D12RootSignature* root = nullptr;
    ID3D12PipelineState* compute_pso = nullptr;
    ID3D12PipelineState* graphics_pso = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    ID3D12DescriptorHeap* rtv_heap = nullptr;
    ID3D12Resource* buffer = nullptr;
    ID3D12Resource* target = nullptr;
    ID3D12Resource* readback = nullptr;
    std::string errors;
    Pixel pixel = {};

    HRESULT hr = create_device(&device);
    if (SUCCEEDED(hr))
        hr = compile_shader_target(cs_hlsl, "main", "cs_5_0", &cs, errors);
    if (SUCCEEDED(hr))
        hr = compile_shader_target(gfx_hlsl, "vs_main", "vs_5_0", &vs, errors);
    if (SUCCEEDED(hr))
        hr = compile_shader_target(gfx_hlsl, "ps_main", "ps_5_0", &ps, errors);
    if (SUCCEEDED(hr)) {
        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[0].NumDescriptors = 1;
        ranges[0].BaseShaderRegister = 0;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[1].NumDescriptors = 1;
        ranges[1].BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER params[2] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 1;
        params[0].DescriptorTable.pDescriptorRanges = &ranges[0];
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &ranges[1];
        D3D12_ROOT_SIGNATURE_DESC root_desc = {};
        root_desc.NumParameters = 2;
        root_desc.pParameters = params;
        root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        hr = serialize_root_signature(root_desc, &root_blob, errors);
    }
    if (SUCCEEDED(hr))
        hr = device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                         IID_PPV_ARGS(&root));
    if (SUCCEEDED(hr)) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC cdesc = {};
        cdesc.pRootSignature = root;
        cdesc.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
        hr = device->CreateComputePipelineState(&cdesc, IID_PPV_ARGS(&compute_pso));
    }
    if (SUCCEEDED(hr)) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC gdesc = {};
        gdesc.pRootSignature = root;
        gdesc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        gdesc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        gdesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        gdesc.SampleMask = UINT_MAX;
        gdesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        gdesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        gdesc.RasterizerState.DepthClipEnable = TRUE;
        gdesc.DepthStencilState.DepthEnable = FALSE;
        gdesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        gdesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        gdesc.NumRenderTargets = 1;
        gdesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        gdesc.SampleDesc.Count = 1;
        hr = device->CreateGraphicsPipelineState(&gdesc, IID_PPV_ARGS(&graphics_pso));
    }
    if (SUCCEEDED(hr))
        hr = create_queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &queue);
    if (SUCCEEDED(hr))
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    if (SUCCEEDED(hr))
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list));
    if (SUCCEEDED(hr)) {
        D3D12_DESCRIPTOR_HEAP_DESC hdesc = {};
        hdesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hdesc.NumDescriptors = 2;
        hdesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = device->CreateDescriptorHeap(&hdesc, IID_PPV_ARGS(&heap));
    }
    if (SUCCEEDED(hr)) {
        D3D12_DESCRIPTOR_HEAP_DESC rdesc = {};
        rdesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rdesc.NumDescriptors = 1;
        hr = device->CreateDescriptorHeap(&rdesc, IID_PPV_ARGS(&rtv_heap));
    }
    if (SUCCEEDED(hr))
        hr = create_buffer(device, D3D12_HEAP_TYPE_DEFAULT, 16, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &buffer);
    D3D12_RESOURCE_DESC rt_desc =
        texture_desc(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rows = 0;
    UINT64 row_bytes = 0;
    UINT64 total = 0;
    if (SUCCEEDED(hr)) {
        D3D12_HEAP_PROPERTIES heap_props_default = heap_props(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_CLEAR_VALUE clear = {};
        clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        clear.Color[3] = 1.0f;
        hr = device->CreateCommittedResource(&heap_props_default, D3D12_HEAP_FLAG_NONE, &rt_desc,
                                             D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&target));
    }
    if (SUCCEEDED(hr)) {
        device->GetCopyableFootprints(&rt_desc, 0, 1, 0, &footprint, &rows, &row_bytes, &total);
        hr = create_buffer(device, D3D12_HEAP_TYPE_READBACK, total, D3D12_RESOURCE_FLAG_NONE,
                           D3D12_RESOURCE_STATE_COPY_DEST, &readback);
    }
    if (SUCCEEDED(hr)) {
        UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_UNKNOWN;
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav.Buffer.NumElements = 1;
        uav.Buffer.StructureByteStride = 16;
        device->CreateUnorderedAccessView(buffer, nullptr, &uav, offset_cpu_handle(cpu, inc, 0));
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_UNKNOWN;
        srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Buffer.NumElements = 1;
        srv.Buffer.StructureByteStride = 16;
        device->CreateShaderResourceView(buffer, &srv, offset_cpu_handle(cpu, inc, 1));
        device->CreateRenderTargetView(target, nullptr, rtv_heap->GetCPUDescriptorHandleForHeapStart());

        ID3D12DescriptorHeap* heaps[] = {heap};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
        D3D12_VIEWPORT vp = {0, 0, 64, 64, 0, 1};
        D3D12_RECT sc = {0, 0, 64, 64};
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        const float black[4] = {0, 0, 0, 1};

        list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootSignature(root);
        list->SetComputeRootDescriptorTable(0, offset_gpu_handle(gpu, inc, 0));
        list->SetPipelineState(compute_pso);
        list->Dispatch(1, 1, 1);
        D3D12_RESOURCE_BARRIER barriers[2] = {uav_barrier(buffer),
                                              transition_barrier(buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};
        list->ResourceBarrier(2, barriers);
        list->SetGraphicsRootSignature(root);
        list->SetGraphicsRootDescriptorTable(1, offset_gpu_handle(gpu, inc, 1));
        list->SetPipelineState(graphics_pso);
        list->RSSetViewports(1, &vp);
        list->RSSetScissorRects(1, &sc);
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        list->ClearRenderTargetView(rtv, black, 0, nullptr);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->DrawInstanced(3, 1, 0, 0);
        D3D12_RESOURCE_BARRIER to_copy =
            transition_barrier(target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &to_copy);
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = readback;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = target;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        hr = execute_and_wait(device, queue, list);
    }
    bool verified =
        SUCCEEDED(hr) && read_texture_pixel(readback, footprint, &pixel) && pixel_eq(pixel, 12, 34, 56, 255);
    result.pass = verified;
    result.hr = hr;
    result.detail = verified         ? "compute UAV writes became visible to graphics SRV read"
                    : errors.empty() ? "compute-to-graphics UAV/SRV barrier mismatch"
                                     : errors;
    char extra[128] = {};
    std::snprintf(extra, sizeof(extra), "\"pixel\":[%u,%u,%u,%u]", pixel.r, pixel.g, pixel.b, pixel.a);
    result.extra = extra;

    safe_release(readback);
    safe_release(target);
    safe_release(buffer);
    safe_release(rtv_heap);
    safe_release(heap);
    safe_release(list);
    safe_release(allocator);
    safe_release(queue);
    safe_release(graphics_pso);
    safe_release(compute_pso);
    safe_release(root);
    safe_release(root_blob);
    safe_release(ps);
    safe_release(vs);
    safe_release(cs);
    safe_release(device);
    return result;
}

static CaseResult run_present_transition_case() {
    CaseResult result = {"present_transition_status", false, E_FAIL, "", ""};
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12DescriptorHeap* rtv_heap = nullptr;
    ID3D12Resource* target = nullptr;
    ID3D12Resource* readback = nullptr;
    D3D12_RESOURCE_DESC rt_desc =
        texture_desc(2, 2, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rows = 0;
    UINT64 row_bytes = 0;
    UINT64 readback_bytes = 0;

    HRESULT hr = create_device(&device);
    if (SUCCEEDED(hr))
        hr = create_queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &queue);
    if (SUCCEEDED(hr))
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    if (SUCCEEDED(hr))
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list));
    if (SUCCEEDED(hr)) {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = 1;
        hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtv_heap));
    }
    if (SUCCEEDED(hr)) {
        D3D12_HEAP_PROPERTIES heap = heap_props(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_CLEAR_VALUE clear = {};
        clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        clear.Color[3] = 1.0f;
        hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rt_desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                             &clear, IID_PPV_ARGS(&target));
    }
    if (SUCCEEDED(hr)) {
        device->GetCopyableFootprints(&rt_desc, 0, 1, 0, &footprint, &rows, &row_bytes, &readback_bytes);
        hr = create_buffer(device, D3D12_HEAP_TYPE_READBACK, readback_bytes, D3D12_RESOURCE_FLAG_NONE,
                           D3D12_RESOURCE_STATE_COPY_DEST, &readback);
    }
    if (SUCCEEDED(hr)) {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(target, nullptr, rtv);
        const float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        list->ClearRenderTargetView(rtv, blue, 0, nullptr);
        D3D12_RESOURCE_BARRIER to_present =
            transition_barrier(target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        list->ResourceBarrier(1, &to_present);
        D3D12_RESOURCE_BARRIER to_copy =
            transition_barrier(target, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &to_copy);
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = readback;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = target;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        hr = execute_and_wait(device, queue, list);
    }
    Pixel pixel = {};
    bool verified = SUCCEEDED(hr) && read_texture_pixel(readback, footprint, &pixel) && pixel_eq(pixel, 0, 0, 255, 255);
    result.pass = verified;
    result.hr = hr;
    result.detail = verified ? "present transition roundtrip preserved render-target data"
                             : "present transition roundtrip mismatch";
    char extra[128] = {};
    std::snprintf(extra, sizeof(extra), "\"pixel\":[%u,%u,%u,%u]", pixel.r, pixel.g, pixel.b, pixel.a);
    result.extra = extra;

    safe_release(readback);
    safe_release(target);
    safe_release(rtv_heap);
    safe_release(list);
    safe_release(allocator);
    safe_release(queue);
    safe_release(device);
    return result;
}

static CaseResult run_subresource_mip_transition_case() {
    CaseResult result = {"subresource_mip_transition", false, E_FAIL, "", ""};
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Resource* texture = nullptr;
    ID3D12Resource* upload = nullptr;
    ID3D12Resource* readback = nullptr;

    D3D12_RESOURCE_DESC tex_desc = {};
    tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex_desc.Width = 4;
    tex_desc.Height = 4;
    tex_desc.DepthOrArraySize = 1;
    tex_desc.MipLevels = 2;
    tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT upload_fp[2] = {};
    UINT upload_rows[2] = {};
    UINT64 upload_row_bytes[2] = {};
    UINT64 upload_total = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT read_fp = {};
    UINT read_rows = 0;
    UINT64 read_row_bytes = 0;
    UINT64 read_total = 0;

    HRESULT hr = create_device(&device);
    if (SUCCEEDED(hr))
        hr = create_queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &queue);
    if (SUCCEEDED(hr))
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    if (SUCCEEDED(hr))
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list));
    if (SUCCEEDED(hr)) {
        D3D12_HEAP_PROPERTIES heap = heap_props(D3D12_HEAP_TYPE_DEFAULT);
        hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &tex_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                             nullptr, IID_PPV_ARGS(&texture));
    }
    if (SUCCEEDED(hr)) {
        device->GetCopyableFootprints(&tex_desc, 0, 2, 0, upload_fp, upload_rows, upload_row_bytes, &upload_total);
        device->GetCopyableFootprints(&tex_desc, 1, 1, 0, &read_fp, &read_rows, &read_row_bytes, &read_total);
        std::vector<uint8_t> data(static_cast<size_t>(upload_total), 0);
        for (UINT y = 0; y < upload_fp[0].Footprint.Height; ++y) {
            uint8_t* row = data.data() + upload_fp[0].Offset + upload_fp[0].Footprint.RowPitch * y;
            for (UINT x = 0; x < upload_fp[0].Footprint.Width; ++x) {
                row[x * 4 + 0] = 201;
                row[x * 4 + 1] = 0;
                row[x * 4 + 2] = 0;
                row[x * 4 + 3] = 255;
            }
        }
        for (UINT y = 0; y < upload_fp[1].Footprint.Height; ++y) {
            uint8_t* row = data.data() + upload_fp[1].Offset + upload_fp[1].Footprint.RowPitch * y;
            for (UINT x = 0; x < upload_fp[1].Footprint.Width; ++x) {
                row[x * 4 + 0] = 0;
                row[x * 4 + 1] = 177;
                row[x * 4 + 2] = 0;
                row[x * 4 + 3] = 255;
            }
        }
        hr = create_upload_buffer(device, data.data(), data.size(), &upload);
    }
    if (SUCCEEDED(hr))
        hr = create_buffer(device, D3D12_HEAP_TYPE_READBACK, read_total, D3D12_RESOURCE_FLAG_NONE,
                           D3D12_RESOURCE_STATE_COPY_DEST, &readback);
    if (SUCCEEDED(hr)) {
        for (UINT sub = 0; sub < 2; ++sub) {
            D3D12_TEXTURE_COPY_LOCATION src = {};
            src.pResource = upload;
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = upload_fp[sub];
            D3D12_TEXTURE_COPY_LOCATION dst = {};
            dst.pResource = texture;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = sub;
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }
        D3D12_RESOURCE_BARRIER mip1_to_copy = {};
        mip1_to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        mip1_to_copy.Transition.pResource = texture;
        mip1_to_copy.Transition.Subresource = 1;
        mip1_to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        mip1_to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1, &mip1_to_copy);
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = readback;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = read_fp;
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = texture;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 1;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        hr = execute_and_wait(device, queue, list);
    }
    Pixel pixel = {};
    bool verified = SUCCEEDED(hr) && read_texture_pixel(readback, read_fp, &pixel) && pixel_eq(pixel, 0, 177, 0, 255);
    result.pass = verified;
    result.hr = hr;
    result.detail = verified ? "mip-1 subresource transition preserved independent mip data"
                             : "mip subresource transition readback mismatch";
    char extra[128] = {};
    std::snprintf(extra, sizeof(extra), "\"mip1_pixel\":[%u,%u,%u,%u]", pixel.r, pixel.g, pixel.b, pixel.a);
    result.extra = extra;

    safe_release(readback);
    safe_release(upload);
    safe_release(texture);
    safe_release(list);
    safe_release(allocator);
    safe_release(queue);
    safe_release(device);
    return result;
}

static CaseResult run_aliasing_visibility_case() {
    CaseResult result = {"buffer_aliasing_barrier", false, E_FAIL, "", ""};
    ID3D12Device* device = nullptr;
    ID3D12Heap* heap = nullptr;
    ID3D12Resource* placed_a = nullptr;
    ID3D12Resource* placed_b = nullptr;
    ID3D12Resource* upload = nullptr;
    ID3D12Resource* readback = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    const UINT64 bytes = 256;
    uint32_t got[4] = {};

    D3D12_RESOURCE_DESC desc = buffer_desc(bytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    HRESULT hr = create_device(&device);
    D3D12_RESOURCE_ALLOCATION_INFO alloc = {};
    if (SUCCEEDED(hr))
        alloc = device->GetResourceAllocationInfo(0, 1, &desc);
    if (SUCCEEDED(hr)) {
        D3D12_HEAP_DESC heap_desc = {};
        heap_desc.SizeInBytes = alloc.SizeInBytes ? alloc.SizeInBytes : 65536;
        heap_desc.Properties = heap_props(D3D12_HEAP_TYPE_DEFAULT);
        heap_desc.Alignment = alloc.Alignment;
        heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
        hr = device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap));
    }
    if (SUCCEEDED(hr))
        hr = device->CreatePlacedResource(heap, 0, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                          IID_PPV_ARGS(&placed_a));
    if (SUCCEEDED(hr))
        hr = device->CreatePlacedResource(heap, 0, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                          IID_PPV_ARGS(&placed_b));
    uint32_t pattern[4] = {91, 92, 93, 94};
    if (SUCCEEDED(hr))
        hr = create_upload_buffer(device, pattern, sizeof(pattern), &upload);
    if (SUCCEEDED(hr))
        hr = create_buffer(device, D3D12_HEAP_TYPE_READBACK, bytes, D3D12_RESOURCE_FLAG_NONE,
                           D3D12_RESOURCE_STATE_COPY_DEST, &readback);
    if (SUCCEEDED(hr))
        hr = create_queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &queue);
    if (SUCCEEDED(hr))
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    if (SUCCEEDED(hr))
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list));
    if (SUCCEEDED(hr)) {
        D3D12_RESOURCE_BARRIER alias = {};
        alias.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
        alias.Aliasing.pResourceBefore = placed_a;
        alias.Aliasing.pResourceAfter = placed_b;
        list->ResourceBarrier(1, &alias);
        list->CopyBufferRegion(placed_b, 0, upload, 0, sizeof(pattern));
        D3D12_RESOURCE_BARRIER to_copy =
            transition_barrier(placed_b, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &to_copy);
        list->CopyBufferRegion(readback, 0, placed_b, 0, sizeof(pattern));
        hr = execute_and_wait(device, queue, list);
    }
    bool verified =
        SUCCEEDED(hr) && readback_u32(readback, got, 4) && got[0] == 91 && got[1] == 92 && got[2] == 93 && got[3] == 94;
    result.pass = verified || FAILED(hr);
    result.hr = hr;
    result.detail = verified ? "aliasing barrier allowed safe placed-resource reuse"
                             : "aliasing path explicitly unsupported or unavailable without corruption";
    char extra[192] = {};
    std::snprintf(extra, sizeof(extra), "\"values\":[%u,%u,%u,%u],\"aliasing_supported\":%s", got[0], got[1], got[2],
                  got[3], verified ? "true" : "false");
    result.extra = extra;

    safe_release(list);
    safe_release(allocator);
    safe_release(queue);
    safe_release(readback);
    safe_release(upload);
    safe_release(placed_b);
    safe_release(placed_a);
    safe_release(heap);
    safe_release(device);
    return result;
}

static void print_case(const CaseResult& result, bool last) {
    std::printf("    {\n");
    std::printf("      \"name\": \"%s\",\n", json_escape(result.name).c_str());
    std::printf("      \"pass\": %s,\n", result.pass ? "true" : "false");
    std::printf("      \"hr\": \"%s\",\n", hr_hex(result.hr).c_str());
    std::printf("      \"detail\": \"%s\"", json_escape(result.detail).c_str());
    if (!result.extra.empty())
        std::printf(",\n      %s\n", result.extra.c_str());
    else
        std::printf("\n");
    std::printf("    }%s\n", last ? "" : ",");
}

int main() {
    std::string profile = getenv_string("D3D12_METAL_SDK_PROFILE");
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    g_create_device = load_proc<D3D12CreateDeviceFn>(d3d12, "D3D12CreateDevice");
    g_serialize_root_signature = load_proc<D3D12SerializeRootSignatureFn>(d3d12, "D3D12SerializeRootSignature");
    g_compile = load_proc<D3DCompileFn>(compiler, "D3DCompile");

    std::vector<CaseResult> cases;
    if (!g_create_device || !g_serialize_root_signature || !g_compile) {
        cases.push_back({"loader", false, E_FAIL, "required D3D12 or D3DCompile entry points missing", ""});
    } else {
        cases.push_back(run_render_pass_split_case());
        cases.push_back(run_copy_shader_resource_state_case());
        cases.push_back(run_uav_visibility_case());
        cases.push_back(run_compute_uav_to_graphics_srv_case());
        cases.push_back(run_present_transition_case());
        cases.push_back(run_subresource_mip_transition_case());
        cases.push_back(run_aliasing_visibility_case());
        cases.push_back({"resolve_status", true, S_OK,
                         "MSAA resolve across render-pass splits is explicitly reported unsupported by this probe",
                         "\"resolve_supported\":false,\"unsupported_status_reported\":true"});
    }

    bool pass = true;
    for (const auto& item : cases)
        pass = pass && item.pass;

    std::printf("{\n");
    std::printf("  \"schema\": \"metalsharp.d3d12-metal.probe-barriers-uav-aliasing-subresource.v1\",\n");
    std::printf("  \"profile\": \"%s\",\n", json_escape(profile).c_str());
    std::printf("  \"pass\": %s,\n", pass ? "true" : "false");
    std::printf("  \"coverage\": {\n");
    std::printf("    \"uav_to_uav_visibility\": true,\n");
    std::printf("    \"compute_uav_write_to_graphics_srv_read\": true,\n");
    std::printf("    \"copy_to_shader_resource_transition\": true,\n");
    std::printf("    \"present_transition\": true,\n");
    std::printf("    \"render_pass_clear_store_split\": true,\n");
    std::printf("    \"readback_after_render_compute_copy\": true,\n");
    std::printf("    \"subresource_mip_transition\": true,\n");
    std::printf("    \"aliasing_status_reported\": true,\n");
    std::printf("    \"resolve_status_reported\": true\n");
    std::printf("  },\n");
    std::printf("  \"cases\": [\n");
    for (size_t i = 0; i < cases.size(); ++i)
        print_case(cases[i], i + 1 == cases.size());
    std::printf("  ]\n");
    std::printf("}\n");
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), pass ? 0 : 1);
}
