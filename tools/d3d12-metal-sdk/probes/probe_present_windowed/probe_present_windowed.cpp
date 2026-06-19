#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

static const GUID IID_D3D12DeviceProbe = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};

using D3D12CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using D3D12SerializeRootSignatureFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION,
                                                       ID3DBlob**, ID3DBlob**);
using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
                                      UINT, UINT, ID3DBlob**, ID3DBlob**);
using CreateDXGIFactory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);

struct Pixel {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
};

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
    if (needed == 0) {
        return "";
    }
    std::string value(needed, '\0');
    DWORD written = GetEnvironmentVariableA(key, value.data(), needed);
    if (written == 0) {
        return "";
    }
    value.resize(written);
    return value;
}

static UINT getenv_uint(const char* key, UINT fallback) {
    std::string value = getenv_string(key);
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    return end && end != value.c_str() ? static_cast<UINT>(parsed) : fallback;
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

static void print_hr(const char* key, HRESULT hr, bool comma = true) {
    std::printf("    \"%s\": \"0x%08lx\"%s\n", key, static_cast<unsigned long>(static_cast<uint32_t>(hr)),
                comma ? "," : "");
}

static void shutdown_log(const char* message) {
    std::fprintf(stderr, "%s\n", message);
    std::fflush(stderr);
}

static void shutdown_logf(const char* fmt, UINT value) {
    std::fprintf(stderr, fmt, value);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

static void pump_messages() {
    MSG msg = {};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void pump_messages_for_ms(DWORD ms) {
    DWORD start = GetTickCount();
    do {
        pump_messages();
        Sleep(10);
    } while (GetTickCount() - start < ms);
}

static LRESULT CALLBACK probe_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
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

static D3D12_CPU_DESCRIPTOR_HANDLE offset_cpu(D3D12_CPU_DESCRIPTOR_HANDLE start, UINT increment, UINT index) {
    start.ptr += static_cast<SIZE_T>(increment) * index;
    return start;
}

static D3D12_RESOURCE_BARRIER uav_barrier(ID3D12Resource* resource) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    return barrier;
}

static Pixel read_pixel(const uint8_t* data, UINT row_pitch, UINT x, UINT y) {
    Pixel pixel = {};
    const size_t offset = static_cast<size_t>(y) * row_pitch + static_cast<size_t>(x) * 4;
    pixel.r = data[offset + 0];
    pixel.g = data[offset + 1];
    pixel.b = data[offset + 2];
    pixel.a = data[offset + 3];
    return pixel;
}

static bool pixel_matches(const Pixel& pixel, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return pixel.r == r && pixel.g == g && pixel.b == b && pixel.a == a;
}

static bool wait_for_fence(ID3D12Fence* fence, UINT64 value, HANDLE event_handle) {
    if (fence->GetCompletedValue() >= value) {
        return true;
    }
    if (FAILED(fence->SetEventOnCompletion(value, event_handle))) {
        return false;
    }
    return WaitForSingleObject(event_handle, 5000) == WAIT_OBJECT_0;
}

static bool execute_and_wait(ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* list, ID3D12Fence* fence,
                             HANDLE event_handle, UINT64& fence_value) {
    ID3D12CommandList* base_list = list;
    queue->ExecuteCommandLists(1, &base_list);
    fence_value += 1;
    if (FAILED(queue->Signal(fence, fence_value))) {
        return false;
    }
    return wait_for_fence(fence, fence_value, event_handle);
}

static HRESULT compile_shader(D3DCompileFn compile_fn, const char* hlsl, const char* entry, const char* target,
                              ID3DBlob** out, std::string& errors) {
    if (!compile_fn) {
        return E_NOINTERFACE;
    }
    ID3DBlob* err = nullptr;
    HRESULT hr = compile_fn(hlsl, std::strlen(hlsl), nullptr, nullptr, nullptr, entry, target, 0, 0, out, &err);
    if (err) {
        errors.assign(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize());
        err->Release();
    }
    return hr;
}

static HRESULT serialize_root_signature(D3D12SerializeRootSignatureFn serialize_fn,
                                        const D3D12_ROOT_SIGNATURE_DESC& desc, ID3DBlob** out,
                                        std::string& errors) {
    if (!serialize_fn) {
        return E_NOINTERFACE;
    }
    ID3DBlob* err = nullptr;
    HRESULT hr = serialize_fn(&desc, D3D_ROOT_SIGNATURE_VERSION_1, out, &err);
    if (err) {
        errors.assign(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize());
        err->Release();
    }
    return hr;
}

struct DescriptorStressResult {
    UINT requested = 0;
    UINT completed = 0;
    HRESULT setup_hr = S_OK;
    HRESULT last_hr = S_OK;
    std::string detail;

    bool pass() const { return completed == requested && SUCCEEDED(setup_hr) && SUCCEEDED(last_hr); }
};

static DescriptorStressResult run_descriptor_compute_stress(
    ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12CommandAllocator* allocator,
    ID3D12GraphicsCommandList* list, ID3D12Fence* fence, HANDLE event_handle, UINT64& fence_value,
    D3D12SerializeRootSignatureFn serialize_fn, D3DCompileFn compile_fn, UINT frames) {
    DescriptorStressResult result = {};
    result.requested = frames;
    if (frames == 0) {
        return result;
    }
    if (!device || !queue || !allocator || !list || !fence || !event_handle) {
        result.setup_hr = E_POINTER;
        result.last_hr = E_POINTER;
        result.detail = "missing d3d12 object";
        return result;
    }

    ID3D12DescriptorHeap* heap = nullptr;
    ID3D12Resource* uav_buffer = nullptr;
    ID3DBlob* root_blob = nullptr;
    ID3DBlob* cs_blob = nullptr;
    ID3D12RootSignature* root_sig = nullptr;
    ID3D12PipelineState* pso = nullptr;

    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = 1;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    result.setup_hr = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap));

    if (SUCCEEDED(result.setup_hr)) {
        D3D12_HEAP_PROPERTIES default_heap = heap_props(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC desc = buffer_desc(4096, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        result.setup_hr = device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                          IID_PPV_ARGS(&uav_buffer));
    }
    if (SUCCEEDED(result.setup_hr)) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav.Format = DXGI_FORMAT_UNKNOWN;
        uav.Buffer.NumElements = 1024;
        uav.Buffer.StructureByteStride = 4;
        device->CreateUnorderedAccessView(uav_buffer, nullptr, &uav, heap->GetCPUDescriptorHandleForHeapStart());
    }
    if (SUCCEEDED(result.setup_hr)) {
        D3D12_DESCRIPTOR_RANGE range = {};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = 0;
        range.RegisterSpace = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        param.DescriptorTable.NumDescriptorRanges = 1;
        param.DescriptorTable.pDescriptorRanges = &range;
        D3D12_ROOT_SIGNATURE_DESC root_desc = {};
        root_desc.NumParameters = 1;
        root_desc.pParameters = &param;
        std::string errors;
        result.setup_hr = serialize_root_signature(serialize_fn, root_desc, &root_blob, errors);
        if (FAILED(result.setup_hr)) {
            result.detail = errors;
        }
    }
    if (SUCCEEDED(result.setup_hr)) {
        result.setup_hr = device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                                      IID_PPV_ARGS(&root_sig));
    }
    if (SUCCEEDED(result.setup_hr)) {
        const char* hlsl =
            "RWStructuredBuffer<uint> outbuf : register(u0);\n"
            "[numthreads(1,1,1)]\n"
            "void main(uint3 tid : SV_DispatchThreadID) {\n"
            "  uint oldv;\n"
            "  InterlockedAdd(outbuf[0], 1, oldv);\n"
            "}\n";
        std::string errors;
        result.setup_hr = compile_shader(compile_fn, hlsl, "main", "cs_5_0", &cs_blob, errors);
        if (FAILED(result.setup_hr)) {
            result.detail = errors;
        }
    }
    if (SUCCEEDED(result.setup_hr)) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
        pso_desc.pRootSignature = root_sig;
        pso_desc.CS.pShaderBytecode = cs_blob->GetBufferPointer();
        pso_desc.CS.BytecodeLength = cs_blob->GetBufferSize();
        result.setup_hr = device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso));
    }

    for (UINT frame = 0; frame < frames && SUCCEEDED(result.setup_hr); ++frame) {
        result.last_hr = allocator->Reset();
        if (SUCCEEDED(result.last_hr)) {
            result.last_hr = list->Reset(allocator, pso);
        }
        if (SUCCEEDED(result.last_hr)) {
            ID3D12DescriptorHeap* heaps[] = {heap};
            list->SetDescriptorHeaps(1, heaps);
            list->SetComputeRootSignature(root_sig);
            list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
            list->Dispatch(1, 1, 1);
            D3D12_RESOURCE_BARRIER barrier = uav_barrier(uav_buffer);
            list->ResourceBarrier(1, &barrier);
            result.last_hr = list->Close();
        }
        if (SUCCEEDED(result.last_hr)) {
            ID3D12CommandList* lists[] = {list};
            queue->ExecuteCommandLists(1, lists);
            fence_value += 1;
            result.last_hr = queue->Signal(fence, fence_value);
            if (SUCCEEDED(result.last_hr) && !wait_for_fence(fence, fence_value, event_handle)) {
                result.last_hr = E_FAIL;
            }
        }
        if (FAILED(result.last_hr)) {
            break;
        }
        result.completed += 1;
    }

    safe_release(pso);
    safe_release(root_sig);
    safe_release(cs_blob);
    safe_release(root_blob);
    safe_release(uav_buffer);
    safe_release(heap);
    return result;
}

static bool verify_texture_color(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12CommandAllocator* allocator,
                                 ID3D12GraphicsCommandList* list, ID3D12Fence* fence, HANDLE event_handle,
                                 UINT64& fence_value, ID3D12Resource* texture, D3D12_RESOURCE_STATES before_state,
                                 uint8_t expected_r, uint8_t expected_g, uint8_t expected_b, Pixel& pixel_out,
                                 HRESULT& map_hr_out) {
    D3D12_RESOURCE_DESC texture_desc = texture->GetDesc();

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT num_rows = 0;
    UINT64 row_size = 0;
    UINT64 total_bytes = 0;
    device->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint, &num_rows, &row_size, &total_bytes);

    D3D12_HEAP_PROPERTIES readback_heap = heap_props(D3D12_HEAP_TYPE_READBACK);
    D3D12_RESOURCE_DESC readback_desc = buffer_desc(total_bytes);
    ID3D12Resource* readback = nullptr;
    HRESULT hr = device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback));
    if (FAILED(hr)) {
        map_hr_out = hr;
        return false;
    }

    bool ok = false;
    if (SUCCEEDED(allocator->Reset()) && SUCCEEDED(list->Reset(allocator, nullptr))) {
        D3D12_RESOURCE_BARRIER to_copy = transition_barrier(texture, before_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &to_copy);

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = texture;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = readback;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprint;

        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER restore =
            transition_barrier(texture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
        list->ResourceBarrier(1, &restore);

        if (SUCCEEDED(list->Close()) && execute_and_wait(queue, list, fence, event_handle, fence_value)) {
            void* mapped = nullptr;
            D3D12_RANGE read_range = {0, static_cast<SIZE_T>(total_bytes)};
            map_hr_out = readback->Map(0, &read_range, &mapped);
            if (SUCCEEDED(map_hr_out)) {
                const uint8_t* bytes = static_cast<const uint8_t*>(mapped);
                pixel_out =
                    read_pixel(bytes, footprint.Footprint.RowPitch, texture_desc.Width / 2, texture_desc.Height / 2);
                ok = pixel_matches(pixel_out, expected_r, expected_g, expected_b, 255);
                readback->Unmap(0, nullptr);
            }
        }
    }

    safe_release(readback);
    return ok;
}

static bool clear_present_and_verify(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12CommandAllocator* allocator,
                                     ID3D12GraphicsCommandList* list, ID3D12Fence* fence, HANDLE event_handle,
                                     UINT64& fence_value, IDXGISwapChain3* swapchain, ID3D12Resource* backbuffer,
                                     D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle, D3D12_RESOURCE_STATES before_state,
                                     const float clear_color[4], uint8_t expected_r, uint8_t expected_g,
                                     uint8_t expected_b, Pixel& pixel_out, HRESULT& map_hr_out, HRESULT& present_hr_out,
                                     HANDLE frame_latency_waitable) {
    bool verified = false;

    if (SUCCEEDED(allocator->Reset()) && SUCCEEDED(list->Reset(allocator, nullptr))) {
        D3D12_RESOURCE_BARRIER to_rtv =
            transition_barrier(backbuffer, before_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->ResourceBarrier(1, &to_rtv);
        list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
        list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);

        D3D12_RESOURCE_BARRIER back_to_source =
            transition_barrier(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->ResourceBarrier(1, &back_to_source);

        if (SUCCEEDED(list->Close()) && execute_and_wait(queue, list, fence, event_handle, fence_value)) {
            verified = verify_texture_color(device, queue, allocator, list, fence, event_handle, fence_value,
                                            backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, expected_r, expected_g,
                                            expected_b, pixel_out, map_hr_out);
            if (verified) {
                present_hr_out = swapchain->Present(0, 0);
                if (SUCCEEDED(present_hr_out) && frame_latency_waitable) {
                    WaitForSingleObject(frame_latency_waitable, 0);
                }
                pump_messages();
                return verified && SUCCEEDED(present_hr_out);
            }
        }
    }

    if (map_hr_out == S_OK) {
        map_hr_out = E_FAIL;
    }
    present_hr_out = E_FAIL;
    return false;
}

static bool clear_present_only(ID3D12CommandQueue* queue, ID3D12CommandAllocator* allocator,
                               ID3D12GraphicsCommandList* list, ID3D12Fence* fence, HANDLE event_handle,
                               UINT64& fence_value, IDXGISwapChain3* swapchain, ID3D12Resource* backbuffer,
                               D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle, D3D12_RESOURCE_STATES before_state,
                               const float clear_color[4], HRESULT& present_hr_out, HANDLE frame_latency_waitable) {
    present_hr_out = E_FAIL;
    if (FAILED(allocator->Reset()) || FAILED(list->Reset(allocator, nullptr))) {
        return false;
    }
    D3D12_RESOURCE_BARRIER to_rtv = transition_barrier(backbuffer, before_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
    list->ResourceBarrier(1, &to_rtv);
    list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
    list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);
    D3D12_RESOURCE_BARRIER to_present = transition_barrier(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                           D3D12_RESOURCE_STATE_PRESENT);
    list->ResourceBarrier(1, &to_present);
    if (FAILED(list->Close()) || !execute_and_wait(queue, list, fence, event_handle, fence_value)) {
        return false;
    }
    present_hr_out = swapchain->Present(0, 0);
    if (SUCCEEDED(present_hr_out) && frame_latency_waitable) {
        WaitForSingleObject(frame_latency_waitable, 0);
    }
    pump_messages();
    return SUCCEEDED(present_hr_out);
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    std::string profile = getenv_string("D3D12_METAL_SDK_PROFILE");
    UINT stress_frames_requested = getenv_uint("M12_PRESENT_WINDOWED_STRESS_FRAMES", 0);
    UINT descriptor_stress_frames_requested = getenv_uint("M12_PRESENT_WINDOWED_DESCRIPTOR_STRESS_FRAMES", 0);
    UINT shutdown_grace_ms = getenv_uint("M12_PRESENT_WINDOWED_SHUTDOWN_GRACE_MS", 500);
    UINT skip_freelib = getenv_uint("M12_PRESENT_WINDOWED_SKIP_FREELIB", 0);

    const UINT initial_width = 128;
    const UINT initial_height = 96;
    const UINT resized_width = 192;
    const UINT resized_height = 128;

    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    HMODULE d3dcompiler = LoadLibraryA("d3dcompiler_47.dll");
    if (!d3dcompiler) {
        d3dcompiler = LoadLibraryA("d3dcompiler_43.dll");
    }

    auto d3d12_create_device = load_proc<D3D12CreateDeviceFn>(d3d12, "D3D12CreateDevice");
    auto d3d12_serialize_root_signature =
        load_proc<D3D12SerializeRootSignatureFn>(d3d12, "D3D12SerializeRootSignature");
    auto d3d_compile = load_proc<D3DCompileFn>(d3dcompiler, "D3DCompile");
    auto create_dxgi_factory2 = load_proc<CreateDXGIFactory2Fn>(dxgi, "CreateDXGIFactory2");

    HRESULT register_class_hr = S_OK;
    HRESULT create_factory_hr = S_OK;
    HRESULT create_device_hr = S_OK;
    HRESULT create_queue_hr = S_OK;
    HRESULT create_swapchain_hr = S_OK;
    HRESULT query_swapchain3_hr = S_OK;
    HRESULT get_hwnd_hr = S_OK;
    HRESULT create_rtv_heap_hr = S_OK;
    HRESULT create_allocator_hr = S_OK;
    HRESULT create_list_hr = S_OK;
    HRESULT create_fence_hr = S_OK;
    HRESULT resize_hr = S_OK;
    HRESULT map0_hr = S_OK;
    HRESULT map1_hr = S_OK;
    HRESULT map_resize_hr = S_OK;
    HRESULT present0_hr = S_OK;
    HRESULT present1_hr = S_OK;
    HRESULT present_resize_hr = S_OK;
    HRESULT get_swapchain_device_hr = S_OK;
    HRESULT get_buffer0_device_hr = S_OK;
    HRESULT set_frame_latency_hr = S_OK;
    HRESULT get_frame_latency_hr = S_OK;
    HRESULT check_sdr_color_space_hr = S_OK;
    HRESULT set_sdr_color_space_hr = S_OK;
    HRESULT check_hdr_color_space_hr = S_OK;
    HRESULT set_hdr_color_space_hr = S_OK;
    HRESULT get_fullscreen_state_initial_hr = S_OK;
    HRESULT set_fullscreen_state_hr = S_OK;
    HRESULT get_fullscreen_state_after_set_hr = S_OK;
    HRESULT restore_windowed_hr = S_OK;
    HRESULT get_fullscreen_state_restored_hr = S_OK;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = probe_window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MetalSharpD3D12PresentProbe";
    ATOM atom = RegisterClassW(&wc);
    if (atom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        register_class_hr = HRESULT_FROM_WIN32(GetLastError());
    }

    RECT rect = {0, 0, static_cast<LONG>(initial_width), static_cast<LONG>(initial_height)};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"MetalSharp D3D12 Present Probe", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr,
                                nullptr, wc.hInstance, nullptr);
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        pump_messages();
    }

    IDXGIFactory4* factory4 = nullptr;
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGISwapChain1* swapchain1 = nullptr;
    IDXGISwapChain3* swapchain3 = nullptr;
    ID3D12DescriptorHeap* rtv_heap = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    HANDLE fence_event = nullptr;
    HANDLE frame_latency_waitable = nullptr;
    UINT64 fence_value = 0;
    HWND swapchain_hwnd = nullptr;
    ID3D12Device* swapchain_device = nullptr;
    ID3D12Device* buffer0_device = nullptr;

    std::array<ID3D12Resource*, 2> buffers = {nullptr, nullptr};
    std::array<ID3D12Resource*, 2> resized_buffers = {nullptr, nullptr};
    Pixel pixel0 = {};
    Pixel pixel1 = {};
    Pixel pixel_resize = {};

    UINT initial_present_count = 0;
    UINT present_count_after_first = 0;
    UINT present_count_after_second = 0;
    UINT present_count_after_resize = 0;
    UINT stress_frames_completed = 0;
    HRESULT stress_last_present_hr = S_OK;
    UINT present_count_after_stress = 0;
    DescriptorStressResult descriptor_stress = {};
    UINT initial_index = 0;
    UINT index_after_first = 0;
    UINT index_after_second = 0;
    UINT index_after_resize = 0;

    bool backbuffers_distinct = false;
    bool hwnd_matches = false;
    bool present_counts_verified = false;
    bool index_progression_verified = false;
    bool initial_dimensions_verified = false;
    bool resize_dimensions_verified = false;
    bool resize_replaced_buffers = false;
    bool buffer0_verified = false;
    bool buffer1_verified = false;
    bool resized_buffer_verified = false;
    bool device_ownership_verified = false;
    bool frame_latency_verified = false;
    bool color_space_verified = false;
    bool fullscreen_windowed_verified = false;
    bool stress_verified = stress_frames_requested == 0;
    bool descriptor_stress_verified = descriptor_stress_frames_requested == 0;

    if (!d3d12 || !dxgi || !d3d12_create_device || !create_dxgi_factory2 || !hwnd || FAILED(register_class_hr)) {
        create_factory_hr = d3d12_create_device ? create_factory_hr : E_NOINTERFACE;
        create_device_hr = create_dxgi_factory2 ? create_device_hr : E_NOINTERFACE;
    } else {
        create_factory_hr = create_dxgi_factory2(0, IID_PPV_ARGS(&factory4));
        if (SUCCEEDED(create_factory_hr)) {
            create_device_hr = d3d12_create_device(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
        }
        if (SUCCEEDED(create_device_hr)) {
            D3D12_COMMAND_QUEUE_DESC queue_desc = {};
            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            create_queue_hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue));
        }
        if (SUCCEEDED(create_queue_hr)) {
            DXGI_SWAP_CHAIN_DESC1 desc = {};
            desc.Width = initial_width;
            desc.Height = initial_height;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.Scaling = DXGI_SCALING_STRETCH;
            desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

            create_swapchain_hr = factory4->CreateSwapChainForHwnd(queue, hwnd, &desc, nullptr, nullptr, &swapchain1);
        }
        if (SUCCEEDED(create_swapchain_hr)) {
            query_swapchain3_hr = swapchain1->QueryInterface(IID_PPV_ARGS(&swapchain3));
        }
        if (SUCCEEDED(query_swapchain3_hr)) {
            get_hwnd_hr = swapchain3->GetHwnd(&swapchain_hwnd);
            get_swapchain_device_hr = swapchain3->GetDevice(IID_PPV_ARGS(&swapchain_device));
        }
        if (SUCCEEDED(query_swapchain3_hr)) {
            D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
            heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            heap_desc.NumDescriptors = 2;
            create_rtv_heap_hr = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap));
        }
        if (SUCCEEDED(create_rtv_heap_hr)) {
            create_allocator_hr =
                device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
        }
        if (SUCCEEDED(create_allocator_hr)) {
            create_list_hr =
                device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list));
            if (SUCCEEDED(create_list_hr)) {
                list->Close();
            }
        }
        if (SUCCEEDED(create_list_hr)) {
            create_fence_hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
            if (SUCCEEDED(create_fence_hr)) {
                fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (!fence_event) {
                    create_fence_hr = HRESULT_FROM_WIN32(GetLastError());
                }
            }
        }
    }

    if (SUCCEEDED(create_fence_hr)) {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_start = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        UINT rtv_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        for (UINT i = 0; i < 2; ++i) {
            swapchain3->GetBuffer(i, IID_PPV_ARGS(&buffers[i]));
            if (buffers[i]) {
                device->CreateRenderTargetView(buffers[i], nullptr, offset_cpu(rtv_start, rtv_increment, i));
            }
        }
        if (buffers[0]) {
            get_buffer0_device_hr = buffers[0]->GetDevice(IID_PPV_ARGS(&buffer0_device));
        }

        D3D12_RESOURCE_DESC buffer0_desc = {};
        D3D12_RESOURCE_DESC buffer1_desc = {};
        if (buffers[0]) {
            buffer0_desc = buffers[0]->GetDesc();
        }
        if (buffers[1]) {
            buffer1_desc = buffers[1]->GetDesc();
        }
        initial_dimensions_verified = buffers[0] && buffers[1] && buffer0_desc.Width == initial_width &&
                                      buffer0_desc.Height == initial_height && buffer1_desc.Width == initial_width &&
                                      buffer1_desc.Height == initial_height;
        backbuffers_distinct = buffers[0] && buffers[1] && buffers[0] != buffers[1];
        hwnd_matches = SUCCEEDED(get_hwnd_hr) && swapchain_hwnd == hwnd;
        device_ownership_verified = SUCCEEDED(get_swapchain_device_hr) && SUCCEEDED(get_buffer0_device_hr) &&
                                    swapchain_device == device && buffer0_device == device;

        set_frame_latency_hr = swapchain3->SetMaximumFrameLatency(2);
        UINT frame_latency = 0;
        get_frame_latency_hr = swapchain3->GetMaximumFrameLatency(&frame_latency);
        frame_latency_waitable = swapchain3->GetFrameLatencyWaitableObject();
        DWORD initial_wait = frame_latency_waitable ? WaitForSingleObject(frame_latency_waitable, 0) : WAIT_FAILED;
        frame_latency_verified = SUCCEEDED(set_frame_latency_hr) && SUCCEEDED(get_frame_latency_hr) &&
                                 frame_latency == 2 && frame_latency_waitable != nullptr &&
                                 (initial_wait == WAIT_OBJECT_0 || initial_wait == WAIT_TIMEOUT);

        UINT sdr_color_space_support = 0;
        UINT hdr_color_space_support = 0;
        check_sdr_color_space_hr =
            swapchain3->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, &sdr_color_space_support);
        set_sdr_color_space_hr = swapchain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        check_hdr_color_space_hr =
            swapchain3->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &hdr_color_space_support);
        set_hdr_color_space_hr = swapchain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
        color_space_verified = SUCCEEDED(check_sdr_color_space_hr) &&
                               (sdr_color_space_support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) &&
                               SUCCEEDED(set_sdr_color_space_hr) && SUCCEEDED(check_hdr_color_space_hr) &&
                               hdr_color_space_support == 0 && FAILED(set_hdr_color_space_hr);

        BOOL initial_fullscreen = TRUE;
        BOOL after_set_fullscreen = TRUE;
        BOOL restored_fullscreen = TRUE;
        get_fullscreen_state_initial_hr = swapchain3->GetFullscreenState(&initial_fullscreen, nullptr);
        set_fullscreen_state_hr = swapchain3->SetFullscreenState(FALSE, nullptr);
        get_fullscreen_state_after_set_hr = swapchain3->GetFullscreenState(&after_set_fullscreen, nullptr);
        restore_windowed_hr = swapchain3->SetFullscreenState(FALSE, nullptr);
        get_fullscreen_state_restored_hr = swapchain3->GetFullscreenState(&restored_fullscreen, nullptr);
        fullscreen_windowed_verified = SUCCEEDED(get_fullscreen_state_initial_hr) && initial_fullscreen == FALSE &&
                                       SUCCEEDED(set_fullscreen_state_hr) &&
                                       SUCCEEDED(get_fullscreen_state_after_set_hr) && after_set_fullscreen == FALSE &&
                                       SUCCEEDED(restore_windowed_hr) && SUCCEEDED(get_fullscreen_state_restored_hr) &&
                                       restored_fullscreen == FALSE;

        swapchain3->GetLastPresentCount(&initial_present_count);
        initial_index = swapchain3->GetCurrentBackBufferIndex();

        const float red_clear[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        const float green_clear[4] = {0.0f, 1.0f, 0.0f, 1.0f};
        const float blue_clear[4] = {0.0f, 0.0f, 1.0f, 1.0f};

        if (buffers[0] && initial_index == 0) {
            buffer0_verified = clear_present_and_verify(device, queue, allocator, list, fence, fence_event, fence_value,
                                                        swapchain3, buffers[0], offset_cpu(rtv_start, rtv_increment, 0),
                                                        D3D12_RESOURCE_STATE_RENDER_TARGET, red_clear, 255, 0, 0,
                                                        pixel0, map0_hr, present0_hr, frame_latency_waitable);
        }

        swapchain3->GetLastPresentCount(&present_count_after_first);
        index_after_first = swapchain3->GetCurrentBackBufferIndex();

        if (buffers[1] && index_after_first == 1) {
            buffer1_verified = clear_present_and_verify(device, queue, allocator, list, fence, fence_event, fence_value,
                                                        swapchain3, buffers[1], offset_cpu(rtv_start, rtv_increment, 1),
                                                        D3D12_RESOURCE_STATE_RENDER_TARGET, green_clear, 0, 255, 0,
                                                        pixel1, map1_hr, present1_hr, frame_latency_waitable);
        }

        swapchain3->GetLastPresentCount(&present_count_after_second);
        index_after_second = swapchain3->GetCurrentBackBufferIndex();

        for (auto& buffer : buffers) {
            safe_release(buffer);
        }

        resize_hr = swapchain3->ResizeBuffers(2, resized_width, resized_height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        if (SUCCEEDED(resize_hr)) {
            DXGI_SWAP_CHAIN_DESC1 resized_desc = {};
            swapchain3->GetDesc1(&resized_desc);
            resize_dimensions_verified = resized_desc.Width == resized_width && resized_desc.Height == resized_height &&
                                         resized_desc.BufferCount == 2;

            for (UINT i = 0; i < 2; ++i) {
                swapchain3->GetBuffer(i, IID_PPV_ARGS(&resized_buffers[i]));
                if (resized_buffers[i]) {
                    device->CreateRenderTargetView(resized_buffers[i], nullptr,
                                                   offset_cpu(rtv_start, rtv_increment, i));
                }
            }

            D3D12_RESOURCE_DESC resized0_desc = {};
            D3D12_RESOURCE_DESC resized1_desc = {};
            if (resized_buffers[0]) {
                resized0_desc = resized_buffers[0]->GetDesc();
            }
            if (resized_buffers[1]) {
                resized1_desc = resized_buffers[1]->GetDesc();
            }
            resize_dimensions_verified = resize_dimensions_verified && resized_buffers[0] && resized_buffers[1] &&
                                         resized0_desc.Width == resized_width &&
                                         resized0_desc.Height == resized_height &&
                                         resized1_desc.Width == resized_width && resized1_desc.Height == resized_height;
            resize_replaced_buffers =
                resized_buffers[0] && resized_buffers[1] && resized_buffers[0] != resized_buffers[1];

            UINT resize_index = swapchain3->GetCurrentBackBufferIndex();
            if (resize_index < resized_buffers.size() && resized_buffers[resize_index]) {
                resized_buffer_verified = clear_present_and_verify(
                    device, queue, allocator, list, fence, fence_event, fence_value, swapchain3,
                    resized_buffers[resize_index], offset_cpu(rtv_start, rtv_increment, resize_index),
                    D3D12_RESOURCE_STATE_RENDER_TARGET, blue_clear, 0, 0, 255, pixel_resize, map_resize_hr,
                    present_resize_hr, frame_latency_waitable);
            }

            swapchain3->GetLastPresentCount(&present_count_after_resize);
            index_after_resize = swapchain3->GetCurrentBackBufferIndex();

            descriptor_stress = run_descriptor_compute_stress(
                device, queue, allocator, list, fence, fence_event, fence_value, d3d12_serialize_root_signature,
                d3d_compile, descriptor_stress_frames_requested);
            descriptor_stress_verified = descriptor_stress.pass();

            static const float stress_colors[4][4] = {
                {1.0f, 0.1f, 0.1f, 1.0f},
                {0.1f, 1.0f, 0.1f, 1.0f},
                {0.1f, 0.1f, 1.0f, 1.0f},
                {1.0f, 1.0f, 0.1f, 1.0f},
            };
            for (UINT frame = 0; frame < stress_frames_requested; ++frame) {
                UINT current = swapchain3->GetCurrentBackBufferIndex();
                if (current >= resized_buffers.size() || !resized_buffers[current]) {
                    stress_last_present_hr = E_FAIL;
                    break;
                }
                bool ok = clear_present_only(queue, allocator, list, fence, fence_event, fence_value, swapchain3,
                                             resized_buffers[current], offset_cpu(rtv_start, rtv_increment, current),
                                             D3D12_RESOURCE_STATE_PRESENT, stress_colors[frame % 4],
                                             stress_last_present_hr, frame_latency_waitable);
                if (!ok) {
                    break;
                }
                stress_frames_completed += 1;
            }
            swapchain3->GetLastPresentCount(&present_count_after_stress);
            stress_verified = stress_frames_completed == stress_frames_requested && SUCCEEDED(stress_last_present_hr);
        }

        present_counts_verified = initial_present_count == 0 && present_count_after_first == 1 &&
                                  present_count_after_second == 2 && present_count_after_resize == 3;
        index_progression_verified =
            initial_index == 0 && index_after_first == 1 && index_after_second == 0 && index_after_resize == 1;
    }

    bool pass = SUCCEEDED(register_class_hr) && hwnd != nullptr && d3d12 && dxgi && d3d12_create_device &&
                create_dxgi_factory2 && SUCCEEDED(create_factory_hr) && SUCCEEDED(create_device_hr) &&
                SUCCEEDED(create_queue_hr) && SUCCEEDED(create_swapchain_hr) && SUCCEEDED(query_swapchain3_hr) &&
                SUCCEEDED(get_hwnd_hr) && SUCCEEDED(create_rtv_heap_hr) && SUCCEEDED(create_allocator_hr) &&
                SUCCEEDED(create_list_hr) && SUCCEEDED(create_fence_hr) && backbuffers_distinct && hwnd_matches &&
                initial_dimensions_verified && buffer0_verified && buffer1_verified && present_counts_verified &&
                index_progression_verified && SUCCEEDED(resize_hr) && resize_dimensions_verified &&
                resize_replaced_buffers && resized_buffer_verified && device_ownership_verified &&
                frame_latency_verified && color_space_verified && fullscreen_windowed_verified && stress_verified &&
                descriptor_stress_verified;

    std::printf("{\n");
    std::printf("  \"schema\": \"metalsharp.d3d12-metal.probe-present-windowed.v1\",\n");
    std::printf("  \"profile\": \"%s\",\n", json_escape(profile).c_str());
    std::printf("  \"pass\": %s,\n", pass ? "true" : "false");
    std::printf("  \"checks\": {\n");
    std::printf("    \"window_created\": %s,\n", hwnd ? "true" : "false");
    std::printf("    \"backbuffers_distinct\": %s,\n", backbuffers_distinct ? "true" : "false");
    std::printf("    \"swapchain_hwnd_matches\": %s,\n", hwnd_matches ? "true" : "false");
    std::printf("    \"initial_dimensions_verified\": %s,\n", initial_dimensions_verified ? "true" : "false");
    std::printf("    \"buffer0_verified\": %s,\n", buffer0_verified ? "true" : "false");
    std::printf("    \"buffer1_verified\": %s,\n", buffer1_verified ? "true" : "false");
    std::printf("    \"present_counts_verified\": %s,\n", present_counts_verified ? "true" : "false");
    std::printf("    \"index_progression_verified\": %s,\n", index_progression_verified ? "true" : "false");
    std::printf("    \"resize_dimensions_verified\": %s,\n", resize_dimensions_verified ? "true" : "false");
    std::printf("    \"resize_replaced_buffers\": %s,\n", resize_replaced_buffers ? "true" : "false");
    std::printf("    \"resized_buffer_verified\": %s,\n", resized_buffer_verified ? "true" : "false");
    std::printf("    \"device_ownership_verified\": %s,\n", device_ownership_verified ? "true" : "false");
    std::printf("    \"frame_latency_verified\": %s,\n", frame_latency_verified ? "true" : "false");
    std::printf("    \"color_space_verified\": %s,\n", color_space_verified ? "true" : "false");
    std::printf("    \"fullscreen_windowed_verified\": %s,\n", fullscreen_windowed_verified ? "true" : "false");
    std::printf("    \"stress_verified\": %s,\n", stress_verified ? "true" : "false");
    std::printf("    \"descriptor_stress_verified\": %s\n", descriptor_stress_verified ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"window\": {\n");
    std::printf("    \"hwnd\": \"%p\",\n", hwnd);
    std::printf("    \"swapchain_hwnd\": \"%p\"\n", swapchain_hwnd);
    std::printf("  },\n");
    std::printf("  \"present_counts\": {\n");
    std::printf("    \"initial\": %u,\n", initial_present_count);
    std::printf("    \"after_first\": %u,\n", present_count_after_first);
    std::printf("    \"after_second\": %u,\n", present_count_after_second);
    std::printf("    \"after_resize\": %u,\n", present_count_after_resize);
    std::printf("    \"after_stress\": %u\n", present_count_after_stress);
    std::printf("  },\n");
    std::printf("  \"backbuffer_indices\": {\n");
    std::printf("    \"initial\": %u,\n", initial_index);
    std::printf("    \"after_first\": %u,\n", index_after_first);
    std::printf("    \"after_second\": %u,\n", index_after_second);
    std::printf("    \"after_resize\": %u\n", index_after_resize);
    std::printf("  },\n");
    std::printf("  \"stress\": {\n");
    std::printf("    \"requested_frames\": %u,\n", stress_frames_requested);
    std::printf("    \"completed_frames\": %u,\n", stress_frames_completed);
    print_hr("last_present", stress_last_present_hr, false);
    std::printf("  },\n");
    std::printf("  \"descriptor_stress\": {\n");
    std::printf("    \"requested_frames\": %u,\n", descriptor_stress.requested);
    std::printf("    \"completed_frames\": %u,\n", descriptor_stress.completed);
    print_hr("setup", descriptor_stress.setup_hr);
    print_hr("last", descriptor_stress.last_hr);
    std::printf("    \"detail\": \"%s\"\n", json_escape(descriptor_stress.detail).c_str());
    std::printf("  },\n");
    std::printf("  \"ownership\": {\n");
    std::printf("    \"created_device\": \"%p\",\n", device);
    std::printf("    \"swapchain_device\": \"%p\",\n", swapchain_device);
    std::printf("    \"buffer0_device\": \"%p\"\n", buffer0_device);
    std::printf("  },\n");
    std::printf("  \"frame_latency\": {\n");
    std::printf("    \"waitable_object\": \"%p\"\n", frame_latency_waitable);
    std::printf("  },\n");
    std::printf("  \"sampled_pixels\": {\n");
    std::printf("    \"buffer0_center\": [%u, %u, %u, %u],\n", pixel0.r, pixel0.g, pixel0.b, pixel0.a);
    std::printf("    \"buffer1_center\": [%u, %u, %u, %u],\n", pixel1.r, pixel1.g, pixel1.b, pixel1.a);
    std::printf("    \"resized_buffer_center\": [%u, %u, %u, %u]\n", pixel_resize.r, pixel_resize.g, pixel_resize.b,
                pixel_resize.a);
    std::printf("  },\n");
    std::printf("  \"hrs\": {\n");
    print_hr("register_class", register_class_hr);
    print_hr("create_factory2", create_factory_hr);
    print_hr("create_device", create_device_hr);
    print_hr("create_queue", create_queue_hr);
    print_hr("create_swapchain", create_swapchain_hr);
    print_hr("query_swapchain3", query_swapchain3_hr);
    print_hr("get_hwnd", get_hwnd_hr);
    print_hr("get_swapchain_device", get_swapchain_device_hr);
    print_hr("get_buffer0_device", get_buffer0_device_hr);
    print_hr("set_frame_latency", set_frame_latency_hr);
    print_hr("get_frame_latency", get_frame_latency_hr);
    print_hr("check_sdr_color_space", check_sdr_color_space_hr);
    print_hr("set_sdr_color_space", set_sdr_color_space_hr);
    print_hr("check_hdr_color_space", check_hdr_color_space_hr);
    print_hr("set_hdr_color_space", set_hdr_color_space_hr);
    print_hr("get_fullscreen_state_initial", get_fullscreen_state_initial_hr);
    print_hr("set_fullscreen_state_windowed", set_fullscreen_state_hr);
    print_hr("get_fullscreen_state_after_set", get_fullscreen_state_after_set_hr);
    print_hr("restore_windowed", restore_windowed_hr);
    print_hr("get_fullscreen_state_restored", get_fullscreen_state_restored_hr);
    print_hr("create_rtv_heap", create_rtv_heap_hr);
    print_hr("create_allocator", create_allocator_hr);
    print_hr("create_list", create_list_hr);
    print_hr("create_fence", create_fence_hr);
    print_hr("map_buffer0", map0_hr);
    print_hr("present_buffer0", present0_hr);
    print_hr("map_buffer1", map1_hr);
    print_hr("present_buffer1", present1_hr);
    print_hr("resize_buffers", resize_hr);
    print_hr("map_resized_buffer", map_resize_hr);
    print_hr("present_resized_buffer", present_resize_hr, false);
    std::printf("  }\n");
    std::printf("}\n");

    shutdown_log("probe shutdown: release backbuffers");
    for (auto& buffer : resized_buffers) {
        safe_release(buffer);
    }
    for (auto& buffer : buffers) {
        safe_release(buffer);
    }

    shutdown_log("probe shutdown: release D3D12 objects");
    safe_release(list);
    safe_release(allocator);
    safe_release(rtv_heap);
    safe_release(buffer0_device);
    safe_release(swapchain_device);
    safe_release(swapchain3);
    safe_release(swapchain1);
    safe_release(queue);
    safe_release(fence);
    if (fence_event) {
        CloseHandle(fence_event);
        fence_event = nullptr;
    }
    safe_release(device);
    safe_release(factory4);

    shutdown_logf("probe shutdown: pump/grace %u ms", shutdown_grace_ms);
    pump_messages_for_ms(shutdown_grace_ms);

    shutdown_log("probe shutdown: destroy window");
    if (hwnd) {
        DestroyWindow(hwnd);
        hwnd = nullptr;
    }
    pump_messages_for_ms(50);

    shutdown_logf("probe shutdown: free libraries skip=%u", skip_freelib);
    if (!skip_freelib && d3d12) {
        shutdown_log("probe shutdown: FreeLibrary d3d12");
        FreeLibrary(d3d12);
        shutdown_log("probe shutdown: FreeLibrary d3d12 done");
    }
    if (!skip_freelib && dxgi) {
        shutdown_log("probe shutdown: FreeLibrary dxgi");
        FreeLibrary(dxgi);
        shutdown_log("probe shutdown: FreeLibrary dxgi done");
    }
    if (!skip_freelib && d3dcompiler) {
        shutdown_log("probe shutdown: FreeLibrary d3dcompiler");
        FreeLibrary(d3dcompiler);
        shutdown_log("probe shutdown: FreeLibrary d3dcompiler done");
    }
    shutdown_log("probe shutdown: return");

    ExitProcess(pass ? 0 : 1);
    return pass ? 0 : 1;
}
