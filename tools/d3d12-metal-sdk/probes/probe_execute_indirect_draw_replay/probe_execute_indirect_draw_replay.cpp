#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <d3d12.h>
#include <d3dcompiler.h>

static const GUID IID_D3D12DeviceProbe = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};
using D3D12CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
using D3D12SerializeRootSignatureFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);

template <typename T> static void safe_release(T*& object) {
    if (object) { object->Release(); object = nullptr; }
}

template <typename T> static T load_proc(HMODULE module, const char* name) {
    T fn = nullptr;
    FARPROC proc = module ? GetProcAddress(module, name) : nullptr;
    static_assert(sizeof(fn) == sizeof(proc), "function pointer size mismatch");
    std::memcpy(&fn, &proc, sizeof(fn));
    return fn;
}

static std::string hr_hex(HRESULT hr) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08lx", static_cast<unsigned long>(static_cast<uint32_t>(hr)));
    return buf;
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

static D3D12_RESOURCE_DESC buffer_desc(UINT64 bytes) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

static D3D12_RESOURCE_DESC texture_desc(UINT width, UINT height, D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;
    return desc;
}

static D3D12_RESOURCE_BARRIER transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

static bool wait_for_fence(ID3D12Fence* fence, UINT64 value, HANDLE event_handle) {
    if (fence->GetCompletedValue() >= value) return true;
    if (FAILED(fence->SetEventOnCompletion(value, event_handle))) return false;
    return WaitForSingleObject(event_handle, 15000) == WAIT_OBJECT_0;
}

struct Pixel { uint8_t r, g, b, a; };
struct Vertex { float px, py, pz, pw; float r, g, b, a; };
struct CaseResult { const char* name; bool pass; HRESULT hr; Pixel observed; std::string detail; };

static bool upload_data(ID3D12Resource* resource, const void* data, size_t bytes, HRESULT& hr) {
    void* mapped = nullptr;
    D3D12_RANGE empty = {0, 0};
    hr = resource->Map(0, &empty, &mapped);
    if (FAILED(hr) || !mapped) return false;
    std::memcpy(mapped, data, bytes);
    resource->Unmap(0, nullptr);
    return true;
}

static bool read_render_target(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12CommandAllocator* allocator,
                               ID3D12GraphicsCommandList* list, ID3D12Fence* fence, HANDLE event_handle,
                               UINT64& fence_value, ID3D12Resource* render_target, Pixel& pixel, HRESULT& hr) {
    D3D12_RESOURCE_DESC tex_desc = render_target->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rows = 0; UINT64 row_size = 0; UINT64 total = 0;
    device->GetCopyableFootprints(&tex_desc, 0, 1, 0, &footprint, &rows, &row_size, &total);
    ID3D12Resource* readback = nullptr;
    auto rb_heap = heap_props(D3D12_HEAP_TYPE_READBACK);
    auto rb_desc = buffer_desc(total);
    hr = device->CreateCommittedResource(&rb_heap, D3D12_HEAP_FLAG_NONE, &rb_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                         nullptr, IID_PPV_ARGS(&readback));
    if (FAILED(hr)) return false;
    bool ok = false;
    if (SUCCEEDED(allocator->Reset())) hr = list->Reset(allocator, nullptr);
    if (SUCCEEDED(hr)) {
        auto to_copy = transition(render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &to_copy);
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = readback;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = render_target;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        hr = list->Close();
    }
    if (SUCCEEDED(hr)) {
        ID3D12CommandList* lists[] = {list};
        queue->ExecuteCommandLists(1, lists);
        fence_value++;
        hr = queue->Signal(fence, fence_value);
        if (SUCCEEDED(hr) && wait_for_fence(fence, fence_value, event_handle)) {
            void* mapped = nullptr;
            D3D12_RANGE range = {0, static_cast<SIZE_T>(total)};
            hr = readback->Map(0, &range, &mapped);
            if (SUCCEEDED(hr) && mapped) {
                auto* bytes = static_cast<const uint8_t*>(mapped);
                size_t off = static_cast<size_t>(footprint.Footprint.RowPitch) * 32 + 32 * 4;
                pixel = {bytes[off + 0], bytes[off + 1], bytes[off + 2], bytes[off + 3]};
                D3D12_RANGE written = {0, 0};
                readback->Unmap(0, &written);
                ok = true;
            }
        } else if (SUCCEEDED(hr)) {
            hr = E_FAIL;
        }
    }
    safe_release(readback);
    return ok;
}

static bool pixel_is(const Pixel& p, uint8_t r, uint8_t g, uint8_t b) {
    return p.r >= r - 5 && p.r <= r + 5 && p.g >= g - 5 && p.g <= g + 5 && p.b >= b - 5 && p.b <= b + 5 && p.a >= 250;
}

struct Context {
    HMODULE d3d12 = nullptr;
    HMODULE d3dcompiler = nullptr;
    D3D12CreateDeviceFn create_device = nullptr;
    D3DCompileFn compile = nullptr;
    D3D12SerializeRootSignatureFn serialize = nullptr;
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    HANDLE event_handle = nullptr;
    UINT64 fence_value = 0;
    ID3D12RootSignature* root = nullptr;
    ID3D12PipelineState* pso = nullptr;
    ID3D12CommandSignature* draw_sig = nullptr;
    ID3D12CommandSignature* draw_indexed_sig = nullptr;
    ID3D12Resource* vb = nullptr;
    ID3D12Resource* ib = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    D3D12_INDEX_BUFFER_VIEW ibv = {};
};

static HRESULT init_context(Context& ctx) {
    ctx.d3d12 = LoadLibraryA("d3d12.dll");
    ctx.d3dcompiler = LoadLibraryA("d3dcompiler_47.dll");
    ctx.create_device = load_proc<D3D12CreateDeviceFn>(ctx.d3d12, "D3D12CreateDevice");
    ctx.compile = load_proc<D3DCompileFn>(ctx.d3dcompiler, "D3DCompile");
    ctx.serialize = load_proc<D3D12SerializeRootSignatureFn>(ctx.d3d12, "D3D12SerializeRootSignature");
    if (!ctx.create_device || !ctx.compile || !ctx.serialize) return E_FAIL;
    HRESULT hr = ctx.create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_D3D12DeviceProbe, reinterpret_cast<void**>(&ctx.device));
    D3D12_COMMAND_QUEUE_DESC qdesc = {};
    if (SUCCEEDED(hr)) hr = ctx.device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&ctx.queue));
    if (SUCCEEDED(hr)) hr = ctx.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ctx.allocator));
    if (SUCCEEDED(hr)) hr = ctx.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ctx.allocator, nullptr, IID_PPV_ARGS(&ctx.list));
    if (SUCCEEDED(hr)) hr = ctx.list->Close();
    if (SUCCEEDED(hr)) hr = ctx.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ctx.fence));
    ctx.event_handle = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (SUCCEEDED(hr) && !ctx.event_handle) hr = E_FAIL;

    const char* hlsl =
        "struct VSIn{float4 pos:POSITION;float4 color:COLOR0;};"
        "struct PSIn{float4 pos:SV_POSITION;float4 color:COLOR0;};"
        "PSIn vs_main(VSIn i){PSIn o;o.pos=i.pos;o.color=i.color;return o;}"
        "float4 ps_main(PSIn i):SV_Target{return i.color;}";
    ID3DBlob* vs = nullptr; ID3DBlob* ps = nullptr; ID3DBlob* blob = nullptr; ID3DBlob* errors = nullptr;
    if (SUCCEEDED(hr)) hr = ctx.compile(hlsl, std::strlen(hlsl), "execute_indirect_draw", nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vs, &errors);
    safe_release(errors);
    if (SUCCEEDED(hr)) hr = ctx.compile(hlsl, std::strlen(hlsl), "execute_indirect_draw", nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &ps, &errors);
    safe_release(errors);
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    if (SUCCEEDED(hr)) hr = ctx.serialize(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    safe_release(errors);
    if (SUCCEEDED(hr)) hr = ctx.device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&ctx.root));

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.pRootSignature = ctx.root;
    pso_desc.VS = {vs ? vs->GetBufferPointer() : nullptr, vs ? vs->GetBufferSize() : 0};
    pso_desc.PS = {ps ? ps->GetBufferPointer() : nullptr, ps ? ps->GetBufferSize() : 0};
    pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.RasterizerState.DepthClipEnable = TRUE;
    pso_desc.DepthStencilState.DepthEnable = FALSE;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso_desc.InputLayout = {layout, 2};
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.SampleDesc.Count = 1;
    if (SUCCEEDED(hr)) hr = ctx.device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&ctx.pso));

    const Vertex vertices[] = {
        {-1.0f, -1.0f, 0, 1, 1, 0, 0, 1}, {-1.0f, 3.0f, 0, 1, 1, 0, 0, 1}, {3.0f, -1.0f, 0, 1, 1, 0, 0, 1},
        {-1.0f, -1.0f, 0, 1, 0, 1, 0, 1}, {-1.0f, 3.0f, 0, 1, 0, 1, 0, 1}, {3.0f, -1.0f, 0, 1, 0, 1, 0, 1},
        {-1.0f, -1.0f, 0, 1, 0, 0, 1, 1}, {-1.0f, 3.0f, 0, 1, 0, 0, 1, 1}, {3.0f, -1.0f, 0, 1, 0, 0, 1, 1},
    };
    const uint16_t indices[] = {3, 4, 5};
    auto upload_heap = heap_props(D3D12_HEAP_TYPE_UPLOAD);
    auto vb_desc = buffer_desc(sizeof(vertices));
    if (SUCCEEDED(hr)) hr = ctx.device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &vb_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&ctx.vb));
    if (SUCCEEDED(hr)) upload_data(ctx.vb, vertices, sizeof(vertices), hr);
    auto ib_desc = buffer_desc(sizeof(indices));
    if (SUCCEEDED(hr)) hr = ctx.device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &ib_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&ctx.ib));
    if (SUCCEEDED(hr)) upload_data(ctx.ib, indices, sizeof(indices), hr);
    if (SUCCEEDED(hr)) {
        ctx.vbv = {ctx.vb->GetGPUVirtualAddress(), sizeof(vertices), sizeof(Vertex)};
        ctx.ibv = {ctx.ib->GetGPUVirtualAddress(), sizeof(indices), DXGI_FORMAT_R16_UINT};
    }
    D3D12_INDIRECT_ARGUMENT_DESC draw_arg = {};
    draw_arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC draw_desc = {};
    draw_desc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    draw_desc.NumArgumentDescs = 1;
    draw_desc.pArgumentDescs = &draw_arg;
    if (SUCCEEDED(hr)) hr = ctx.device->CreateCommandSignature(&draw_desc, nullptr, IID_PPV_ARGS(&ctx.draw_sig));
    D3D12_INDIRECT_ARGUMENT_DESC indexed_arg = {};
    indexed_arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    D3D12_COMMAND_SIGNATURE_DESC indexed_desc = {};
    indexed_desc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    indexed_desc.NumArgumentDescs = 1;
    indexed_desc.pArgumentDescs = &indexed_arg;
    if (SUCCEEDED(hr)) hr = ctx.device->CreateCommandSignature(&indexed_desc, nullptr, IID_PPV_ARGS(&ctx.draw_indexed_sig));

    safe_release(errors); safe_release(blob); safe_release(ps); safe_release(vs);
    return hr;
}

static void destroy_context(Context& ctx) {
    safe_release(ctx.ib); safe_release(ctx.vb); safe_release(ctx.draw_indexed_sig); safe_release(ctx.draw_sig);
    safe_release(ctx.pso); safe_release(ctx.root); safe_release(ctx.fence); safe_release(ctx.list);
    safe_release(ctx.allocator); safe_release(ctx.queue); safe_release(ctx.device);
    if (ctx.event_handle) CloseHandle(ctx.event_handle);
    if (ctx.d3dcompiler) FreeLibrary(ctx.d3dcompiler);
    if (ctx.d3d12) FreeLibrary(ctx.d3d12);
}

static CaseResult run_case(Context& ctx, const char* name, ID3D12CommandSignature* sig, UINT max_command_count,
                           const void* initial_args, size_t args_size, const void* mutation_args, size_t mutation_size,
                           const uint32_t* count_value, uint8_t er, uint8_t eg, uint8_t eb,
                           bool default_heap_args = false, bool default_heap_count = false,
                           bool placed_heap_args = false, bool placed_heap_count = false) {
    CaseResult result = {name, false, E_FAIL, {0,0,0,0}, ""};
    ID3D12Resource* render_target = nullptr; ID3D12DescriptorHeap* rtv_heap = nullptr;
    ID3D12Resource* args = nullptr; ID3D12Resource* args_upload = nullptr; ID3D12Heap* args_heap = nullptr;
    ID3D12Resource* count = nullptr; ID3D12Resource* count_upload = nullptr; ID3D12Heap* count_heap = nullptr;
    HRESULT hr = S_OK;
    auto default_heap = heap_props(D3D12_HEAP_TYPE_DEFAULT);
    auto upload_heap = heap_props(D3D12_HEAP_TYPE_UPLOAD);
    auto rt_desc = texture_desc(64, 64, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    D3D12_CLEAR_VALUE clear = {}; clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM; clear.Color[3] = 1.0f;
    hr = ctx.device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &rt_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&render_target));
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {}; rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rtv_desc.NumDescriptors = 1;
    if (SUCCEEDED(hr)) hr = ctx.device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap));
    if (SUCCEEDED(hr)) ctx.device->CreateRenderTargetView(render_target, nullptr, rtv_heap->GetCPUDescriptorHandleForHeapStart());
    auto arg_desc = buffer_desc(args_size);
    if (SUCCEEDED(hr) && default_heap_args) {
        if (placed_heap_args) {
            D3D12_HEAP_DESC heap_desc = {};
            heap_desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT + args_size;
            heap_desc.Properties = default_heap;
            heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
            hr = ctx.device->CreateHeap(&heap_desc, IID_PPV_ARGS(&args_heap));
            if (SUCCEEDED(hr)) hr = ctx.device->CreatePlacedResource(args_heap, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, &arg_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&args));
        } else {
            hr = ctx.device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &arg_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&args));
        }
        if (SUCCEEDED(hr)) hr = ctx.device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &arg_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&args_upload));
        if (SUCCEEDED(hr)) upload_data(args_upload, initial_args, args_size, hr);
    } else if (SUCCEEDED(hr)) {
        hr = ctx.device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &arg_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&args));
        if (SUCCEEDED(hr)) upload_data(args, initial_args, args_size, hr);
    }
    if (SUCCEEDED(hr) && count_value) {
        auto count_desc = buffer_desc(sizeof(uint32_t));
        if (default_heap_count) {
            if (placed_heap_count) {
                D3D12_HEAP_DESC heap_desc = {};
                heap_desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT + sizeof(uint32_t);
                heap_desc.Properties = default_heap;
                heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
                heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
                hr = ctx.device->CreateHeap(&heap_desc, IID_PPV_ARGS(&count_heap));
                if (SUCCEEDED(hr)) hr = ctx.device->CreatePlacedResource(count_heap, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, &count_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&count));
            } else {
                hr = ctx.device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &count_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&count));
            }
            if (SUCCEEDED(hr)) hr = ctx.device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &count_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&count_upload));
            if (SUCCEEDED(hr)) upload_data(count_upload, count_value, sizeof(uint32_t), hr);
        } else {
            hr = ctx.device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &count_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&count));
            if (SUCCEEDED(hr)) upload_data(count, count_value, sizeof(uint32_t), hr);
        }
    }
    if (SUCCEEDED(hr)) hr = ctx.allocator->Reset();
    if (SUCCEEDED(hr)) hr = ctx.list->Reset(ctx.allocator, ctx.pso);
    if (SUCCEEDED(hr)) {
        if (args_upload) {
            ctx.list->CopyBufferRegion(args, 0, args_upload, 0, args_size);
            auto barrier = transition(args, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
            ctx.list->ResourceBarrier(1, &barrier);
        }
        if (count_upload) {
            ctx.list->CopyBufferRegion(count, 0, count_upload, 0, sizeof(uint32_t));
            auto barrier = transition(count, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
            ctx.list->ResourceBarrier(1, &barrier);
        }
        D3D12_VIEWPORT vp = {0,0,64,64,0,1}; D3D12_RECT sc = {0,0,64,64};
        const float black[4] = {0,0,0,1};
        auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        ctx.list->RSSetViewports(1, &vp); ctx.list->RSSetScissorRects(1, &sc);
        ctx.list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        ctx.list->ClearRenderTargetView(rtv, black, 0, nullptr);
        ctx.list->SetGraphicsRootSignature(ctx.root); ctx.list->SetPipelineState(ctx.pso);
        ctx.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx.list->IASetVertexBuffers(0, 1, &ctx.vbv); ctx.list->IASetIndexBuffer(&ctx.ibv);
        ctx.list->ExecuteIndirect(sig, max_command_count, args, 0, count, 0);
        hr = ctx.list->Close();
    }
    if (SUCCEEDED(hr) && mutation_args && mutation_size) upload_data(args, mutation_args, mutation_size, hr);
    if (SUCCEEDED(hr)) {
        ID3D12CommandList* lists[] = {ctx.list};
        ctx.queue->ExecuteCommandLists(1, lists);
        ctx.fence_value++;
        hr = ctx.queue->Signal(ctx.fence, ctx.fence_value);
        if (SUCCEEDED(hr) && !wait_for_fence(ctx.fence, ctx.fence_value, ctx.event_handle)) hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) read_render_target(ctx.device, ctx.queue, ctx.allocator, ctx.list, ctx.fence, ctx.event_handle, ctx.fence_value, render_target, result.observed, hr);
    result.pass = SUCCEEDED(hr) && pixel_is(result.observed, er, eg, eb);
    result.hr = hr;
    if (result.pass && (default_heap_args || default_heap_count))
        result.detail = "GPU/default-heap ExecuteIndirect input materialized and replayed expected draw";
    else
        result.detail = result.pass ? "ExecuteIndirect draw replay matched expected color" : "ExecuteIndirect draw replay color mismatch";
    safe_release(count_upload); safe_release(count); safe_release(count_heap); safe_release(args_upload); safe_release(args); safe_release(args_heap); safe_release(rtv_heap); safe_release(render_target);
    return result;
}

static void print_case(const CaseResult& c, bool last) {
    std::printf("    {\n");
    std::printf("      \"name\": \"%s\",\n", c.name);
    std::printf("      \"pass\": %s,\n", c.pass ? "true" : "false");
    std::printf("      \"hr\": \"%s\",\n", hr_hex(c.hr).c_str());
    std::printf("      \"observed\": [%u,%u,%u,%u],\n", c.observed.r, c.observed.g, c.observed.b, c.observed.a);
    std::printf("      \"detail\": \"%s\"\n", c.detail.c_str());
    std::printf("    }%s\n", last ? "" : ",");
}

int main() {
    Context ctx;
    HRESULT init_hr = init_context(ctx);
    std::vector<CaseResult> cases;
    if (SUCCEEDED(init_hr)) {
        D3D12_DRAW_ARGUMENTS draw_red = {3, 1, 0, 0};
        D3D12_DRAW_ARGUMENTS draw_blue = {3, 1, 6, 0};
        D3D12_DRAW_ARGUMENTS draw_pair[2] = {{3, 1, 0, 0}, {3, 1, 6, 0}};
        D3D12_DRAW_INDEXED_ARGUMENTS draw_indexed_green = {3, 1, 0, 0, 0};
        uint32_t zero = 0, one = 1, two = 2;
        cases.push_back(run_case(ctx, "execute_indirect_draw", ctx.draw_sig, 1, &draw_red, sizeof(draw_red), nullptr, 0, nullptr, 255, 0, 0));
        cases.push_back(run_case(ctx, "execute_indirect_draw_indexed", ctx.draw_indexed_sig, 1, &draw_indexed_green, sizeof(draw_indexed_green), nullptr, 0, nullptr, 0, 255, 0));
        cases.push_back(run_case(ctx, "execute_indirect_count_zero", ctx.draw_sig, 1, &draw_red, sizeof(draw_red), nullptr, 0, &zero, 0, 0, 0));
        cases.push_back(run_case(ctx, "execute_indirect_count_n", ctx.draw_sig, 2, draw_pair, sizeof(draw_pair), nullptr, 0, &two, 0, 0, 255));
        cases.push_back(run_case(ctx, "execute_indirect_args_mutated_before_execute", ctx.draw_sig, 1, &draw_red, sizeof(draw_red), &draw_blue, sizeof(draw_blue), &one, 0, 0, 255));
        cases.push_back(run_case(ctx, "execute_indirect_gpu_argument_buffer_materialized", ctx.draw_sig, 1, &draw_red, sizeof(draw_red), nullptr, 0, nullptr, 255, 0, 0, true, false));
        cases.push_back(run_case(ctx, "execute_indirect_gpu_count_buffer_materialized", ctx.draw_sig, 1, &draw_red, sizeof(draw_red), nullptr, 0, &one, 255, 0, 0, false, true));
        cases.push_back(run_case(ctx, "execute_indirect_placed_gpu_argument_buffer_materialized", ctx.draw_sig, 1, &draw_red, sizeof(draw_red), nullptr, 0, nullptr, 255, 0, 0, true, false, true, false));
        cases.push_back(run_case(ctx, "execute_indirect_placed_gpu_count_buffer_materialized", ctx.draw_sig, 1, &draw_red, sizeof(draw_red), nullptr, 0, &one, 255, 0, 0, false, true, false, true));
    } else {
        cases.push_back({"setup", false, init_hr, {0,0,0,0}, "failed to initialize D3D12 ExecuteIndirect draw probe"});
    }
    bool pass = true;
    for (const auto& c : cases) pass = pass && c.pass;
    std::printf("{\n");
    std::printf("  \"schema\": \"metalsharp.d3d12-metal.probe-execute-indirect-draw-replay.v1\",\n");
    std::printf("  \"profile\": \"metalsharp\",\n");
    std::printf("  \"pass\": %s,\n", pass ? "true" : "false");
    std::printf("  \"coverage\": {\n");
    std::printf("    \"draw\": %s,\n", cases.size() > 0 && cases[0].pass ? "true" : "false");
    std::printf("    \"draw_indexed\": %s,\n", cases.size() > 1 && cases[1].pass ? "true" : "false");
    std::printf("    \"count_zero\": %s,\n", cases.size() > 2 && cases[2].pass ? "true" : "false");
    std::printf("    \"count_n\": %s,\n", cases.size() > 3 && cases[3].pass ? "true" : "false");
    std::printf("    \"args_mutated_before_execute\": %s,\n", cases.size() > 4 && cases[4].pass ? "true" : "false");
    std::printf("    \"gpu_argument_buffer_materialized\": %s,\n", cases.size() > 5 && cases[5].pass ? "true" : "false");
    std::printf("    \"gpu_count_buffer_materialized\": %s,\n", cases.size() > 6 && cases[6].pass ? "true" : "false");
    std::printf("    \"placed_gpu_argument_buffer_materialized\": %s,\n", cases.size() > 7 && cases[7].pass ? "true" : "false");
    std::printf("    \"placed_gpu_count_buffer_materialized\": %s,\n", cases.size() > 8 && cases[8].pass ? "true" : "false");
    std::printf("    \"gpu_authored_args\": %s\n", cases.size() > 5 && cases[5].pass && cases.size() > 7 && cases[7].pass ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"cases\": [\n");
    for (size_t i = 0; i < cases.size(); ++i) print_case(cases[i], i + 1 == cases.size());
    std::printf("  ]\n");
    std::printf("}\n");
    destroy_context(ctx);
    return pass ? 0 : 1;
}
