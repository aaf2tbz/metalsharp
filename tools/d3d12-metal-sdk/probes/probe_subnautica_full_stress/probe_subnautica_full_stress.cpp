#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

namespace fs = std::filesystem;

using D3D12CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using D3D12SerializeRootSignatureFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION,
                                                       ID3DBlob**, ID3DBlob**);
using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
                                      UINT, UINT, ID3DBlob**, ID3DBlob**);
using CreateDXGIFactory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);

template <typename T> static void safe_release(T*& object) {
    if (object) {
        object->Release();
        object = nullptr;
    }
}

template <typename T> static T load_proc(HMODULE module, const char* name) {
    T fn = nullptr;
    FARPROC proc = module ? GetProcAddress(module, name) : nullptr;
    std::memcpy(&fn, &proc, sizeof(fn));
    return fn;
}

static std::string getenv_string(const char* key) {
    DWORD needed = GetEnvironmentVariableA(key, nullptr, 0);
    if (!needed)
        return "";
    std::string value(needed, '\0');
    DWORD written = GetEnvironmentVariableA(key, value.data(), needed);
    if (!written)
        return "";
    value.resize(written);
    return value;
}

static std::string hr_hex(HRESULT hr) {
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%08lx", static_cast<unsigned long>(static_cast<uint32_t>(hr)));
    return buffer;
}

static void json_out(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
    std::fflush(stdout);
}

static void fail_json(const char* stage, HRESULT hr, const std::string& detail = "") {
    json_out("{\"ok\":false,\"stage\":\"%s\",\"hr\":\"%s\",\"detail\":\"%s\"}", stage, hr_hex(hr).c_str(),
             detail.c_str());
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_CLOSE)
        return 0;
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
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return desc;
}

static D3D12_RESOURCE_DESC texture2d_desc(UINT width, UINT height, DXGI_FORMAT format,
                                          D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;
    return desc;
}

static D3D12_RESOURCE_BARRIER transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                         D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}

static D3D12_CPU_DESCRIPTOR_HANDLE offset_cpu(D3D12_CPU_DESCRIPTOR_HANDLE start, UINT increment, UINT index) {
    start.ptr += static_cast<SIZE_T>(increment) * index;
    return start;
}

static void save_bmp_32(const char* path, const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t pitch) {
    uint32_t row_bytes = width * 3;
    uint32_t padded_row = ((row_bytes + 3) / 4) * 4;
    uint32_t img_size = padded_row * height;
    uint32_t file_size = 54 + img_size;
    uint8_t header[54] = {};
    header[0] = 'B';
    header[1] = 'M';
    header[2] = (uint8_t)(file_size);
    header[3] = (uint8_t)(file_size >> 8);
    header[4] = (uint8_t)(file_size >> 16);
    header[5] = (uint8_t)(file_size >> 24);
    header[10] = 54;
    header[14] = 40;
    header[18] = (uint8_t)(width);
    header[19] = (uint8_t)(width >> 8);
    header[20] = (uint8_t)(width >> 16);
    header[21] = (uint8_t)(width >> 24);
    header[22] = (uint8_t)(height);
    header[23] = (uint8_t)(height >> 8);
    header[24] = (uint8_t)(height >> 16);
    header[25] = (uint8_t)(height >> 24);
    header[26] = 1;
    header[27] = 0;
    header[28] = 24;
    header[34] = (uint8_t)(img_size);
    header[35] = (uint8_t)(img_size >> 8);
    header[36] = (uint8_t)(img_size >> 16);
    header[37] = (uint8_t)(img_size >> 24);
    header[38] = (uint8_t)(width);
    header[39] = (uint8_t)(width >> 8);
    header[42] = (uint8_t)(height);
    header[43] = (uint8_t)(height >> 8);
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(f, header, 54, &written, nullptr);
    std::vector<uint8_t> row(padded_row, 0);
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* src = rgba + (uint64_t)(height - 1 - y) * pitch;
        for (uint32_t x = 0; x < width; x++) {
            row[x * 3 + 0] = src[x * 4 + 2];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 0];
        }
        WriteFile(f, row.data(), padded_row, &written, nullptr);
    }
    CloseHandle(f);
}

struct ShaderBlob {
    std::string path;
    std::vector<uint8_t> data;
    std::string tag;
};

struct PsoResult {
    std::string name;
    HRESULT hr;
    bool compiled;
    bool drew;
    uint64_t nonzero_pixels;
};

static constexpr uint32_t FOURCC(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) | (static_cast<uint32_t>(c) << 16) |
           (static_cast<uint32_t>(d) << 24);
}

static std::string detect_shader_tag(const uint8_t* data, size_t size) {
    if (size < 32)
        return "unknown";
    if (data[0] != 'D' || data[1] != 'X' || data[2] != 'B' || data[3] != 'C')
        return "non_dxbc";

    const uint32_t* u32 = reinterpret_cast<const uint32_t*>(data);
    uint32_t num_chunks = u32[7];
    uint32_t chunk_offset_base = 8;

    for (uint32_t c = 0; c < num_chunks && c < 20; c++) {
        if (chunk_offset_base + c >= size / 4)
            break;
        uint32_t offset = u32[chunk_offset_base + c];
        if (offset + 8 > size)
            continue;
        uint32_t fourcc = u32[offset / 4];
        uint32_t chunk_size = u32[offset / 4 + 1];
        (void)chunk_size;
        if (fourcc == FOURCC('D', 'X', 'I', 'L')) {
            uint32_t payload_off = offset + 8;
            if (payload_off + 4 <= size) {
                uint32_t prog_ver = u32[payload_off / 4];
                uint32_t shader_kind = (prog_ver >> 16) & 0xFF;
                switch (shader_kind) {
                case 0:
                    return "ps";
                case 1:
                    return "vs";
                case 2:
                    return "gs";
                case 3:
                    return "hs";
                case 4:
                    return "ds";
                case 5:
                    return "cs";
                case 6:
                    return "lib";
                case 7:
                    return "raygen";
                case 8:
                    return "intersection";
                case 9:
                    return "anyhit";
                case 10:
                    return "closesthit";
                case 11:
                    return "miss";
                case 12:
                    return "callable";
                case 13:
                    return "mesh";
                case 14:
                    return "amp";
                default:
                    return "dxil_unknown";
                }
            }
        }
        if (fourcc == FOURCC('S', 'H', 'E', 'X')) {
            uint32_t payload_offset = offset + 8;
            if (payload_offset + 16 <= size) {
                uint16_t type = data[payload_offset + 3];
                switch (type) {
                case 0xFF:
                    return "cs";
                case 0xFE:
                    return "vs";
                case 0xFD:
                    return "ps";
                case 0xFC:
                    return "gs";
                case 0xFB:
                    return "hs";
                case 0xFA:
                    return "ds";
                }
            }
        }
        if (fourcc == FOURCC('S', 'T', 'A', 'T')) {
            if (offset + 8 + 11 < size) {
                uint8_t shader_type = data[offset + 8 + 10];
                switch (shader_type) {
                case 0:
                    return "vs";
                case 1:
                    return "ps";
                case 2:
                    return "gs";
                case 3:
                    return "hs";
                case 4:
                    return "ds";
                case 5:
                    return "cs";
                }
            }
        }
    }

    return "unknown";
}

static std::vector<ShaderBlob> scan_shader_cache(const std::string& cache_dir) {
    std::vector<ShaderBlob> blobs;
    if (!fs::exists(cache_dir))
        return blobs;

    uint32_t scanned = 0;
    uint32_t max_scan = 10000;

    for (const auto& entry : fs::directory_iterator(cache_dir)) {
        if (!entry.is_regular_file())
            continue;
        if (entry.file_size() < 64 || entry.file_size() > 2 * 1024 * 1024)
            continue;

        if (scanned >= max_scan)
            break;
        scanned++;

        ShaderBlob blob;
        blob.path = entry.path().string();
        auto fsize = entry.file_size();
        blob.data.resize(fsize);

        HANDLE file = CreateFileA(blob.path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            continue;
        DWORD read = 0;
        ReadFile(file, blob.data.data(), static_cast<DWORD>(fsize), &read, nullptr);
        CloseHandle(file);
        if (read != fsize)
            continue;

        if (blob.data.size() < 4 || blob.data[0] != 'D' || blob.data[1] != 'X' || blob.data[2] != 'B' ||
            blob.data[3] != 'C')
            continue;

        blob.tag = detect_shader_tag(blob.data.data(), blob.data.size());
        blobs.push_back(std::move(blob));
    }

    return blobs;
}

static HRESULT compile_shader(const char* src, const char* entry, const char* target, ID3DBlob** blob) {
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    D3DCompileFn compile = load_proc<D3DCompileFn>(compiler, "D3DCompile");
    if (!compile)
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    ID3DBlob* err_blob = nullptr;
    HRESULT hr = compile(src, std::strlen(src), "stress.hlsl", nullptr, nullptr, entry, target, 0, 0, blob, &err_blob);
    if (err_blob)
        err_blob->Release();
    return hr;
}

static HRESULT serialize_root_sig(const D3D12_ROOT_SIGNATURE_DESC& desc, ID3DBlob** blob) {
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    D3D12SerializeRootSignatureFn serialize =
        load_proc<D3D12SerializeRootSignatureFn>(d3d12, "D3D12SerializeRootSignature");
    if (!serialize)
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    ID3DBlob* err_blob = nullptr;
    HRESULT hr = serialize(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, blob, &err_blob);
    if (err_blob)
        err_blob->Release();
    return hr;
}

static bool wait_fence(ID3D12Fence* fence, UINT64 value, HANDLE event) {
    if (fence->GetCompletedValue() >= value)
        return true;
    fence->SetEventOnCompletion(value, event);
    return WaitForSingleObject(event, 15000) == WAIT_OBJECT_0;
}

struct FrameResources {
    ID3D12Resource* backbuffers[3] = {};
    ID3D12DescriptorHeap* rtv_heap = nullptr;
    UINT rtv_inc = 0;
    ID3D12DescriptorHeap* dsv_heap = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12DescriptorHeap* srv_heap = nullptr;
    UINT srv_inc = 0;
    ID3D12Resource* offscreen[2] = {};
    ID3D12Resource* texture_2d = nullptr;
    ID3D12Resource* texture_rgba = nullptr;
    ID3D12Resource* upload_buffer = nullptr;
    ID3D12Resource* vb_sprite = nullptr;
    ID3D12Resource* vb_quad = nullptr;
    ID3D12Resource* cb_root = nullptr;
    ID3D12Resource* compute_buf = nullptr;
    ID3D12Resource* index_buf = nullptr;
    ID3D12Resource* instance_buf = nullptr;
    ID3D12Resource* readback_buf = nullptr;
    ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    HANDLE fence_event = nullptr;
    UINT64 fence_val = 0;
    IDXGISwapChain3* swapchain = nullptr;

    static constexpr UINT WIDTH = 1280;
    static constexpr UINT HEIGHT = 720;
    static constexpr UINT BACKBUFFER_COUNT = 3;
};

static HRESULT init_frame_resources(FrameResources& fr, ID3D12Device* device, ID3D12CommandQueue* queue,
                                    IDXGIFactory4* factory, HWND hwnd) {
    HRESULT hr = E_FAIL;

    DXGI_SWAP_CHAIN_DESC1 sc_desc = {};
    sc_desc.Width = fr.WIDTH;
    sc_desc.Height = fr.HEIGHT;
    sc_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc_desc.SampleDesc.Count = 1;
    sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc_desc.BufferCount = fr.BACKBUFFER_COUNT;
    sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1* sc1 = nullptr;
    hr = factory->CreateSwapChainForHwnd(queue, hwnd, &sc_desc, nullptr, nullptr, &sc1);
    if (FAILED(hr))
        return hr;
    hr = sc1->QueryInterface(IID_PPV_ARGS(&fr.swapchain));
    sc1->Release();
    if (FAILED(hr))
        return hr;

    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {};
    rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.NumDescriptors = 8;
    hr = device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&fr.rtv_heap));
    if (FAILED(hr))
        return hr;
    fr.rtv_inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (UINT i = 0; i < fr.BACKBUFFER_COUNT; i++) {
        hr = fr.swapchain->GetBuffer(i, IID_PPV_ARGS(&fr.backbuffers[i]));
        if (FAILED(hr))
            return hr;
        device->CreateRenderTargetView(fr.backbuffers[i], nullptr,
                                       offset_cpu(fr.rtv_heap->GetCPUDescriptorHandleForHeapStart(), fr.rtv_inc, i));
    }

    {
        D3D12_CLEAR_VALUE cv = {};
        cv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        cv.Color[3] = 1.0f;
        auto desc =
            texture2d_desc(fr.WIDTH, fr.HEIGHT, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        auto heap = heap_props(D3D12_HEAP_TYPE_DEFAULT);
        for (int i = 0; i < 2; i++) {
            hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                 &cv, IID_PPV_ARGS(&fr.offscreen[i]));
            if (FAILED(hr))
                return hr;
            device->CreateRenderTargetView(
                fr.offscreen[i], nullptr,
                offset_cpu(fr.rtv_heap->GetCPUDescriptorHandleForHeapStart(), fr.rtv_inc, 3 + i));
        }
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC dsv_desc = {};
        dsv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsv_desc.NumDescriptors = 1;
        hr = device->CreateDescriptorHeap(&dsv_desc, IID_PPV_ARGS(&fr.dsv_heap));
        if (FAILED(hr))
            return hr;
        D3D12_CLEAR_VALUE dcv = {};
        dcv.Format = DXGI_FORMAT_D32_FLOAT;
        dcv.DepthStencil.Depth = 1.0f;
        auto desc = texture2d_desc(fr.WIDTH, fr.HEIGHT, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        auto heap = heap_props(D3D12_HEAP_TYPE_DEFAULT);
        hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dcv,
                                             IID_PPV_ARGS(&fr.depth));
        if (FAILED(hr))
            return hr;
        device->CreateDepthStencilView(fr.depth, nullptr, fr.dsv_heap->GetCPUDescriptorHandleForHeapStart());
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC srv_desc = {};
        srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_desc.NumDescriptors = 16;
        srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&fr.srv_heap));
        if (FAILED(hr))
            return hr;
        fr.srv_inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    {
        auto heap = heap_props(D3D12_HEAP_TYPE_DEFAULT);
        auto desc = texture2d_desc(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE);
        hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                             nullptr, IID_PPV_ARGS(&fr.texture_2d));
        if (FAILED(hr))
            return hr;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(fr.texture_2d, &srv, fr.srv_heap->GetCPUDescriptorHandleForHeapStart());
    }

    {
        auto heap = heap_props(D3D12_HEAP_TYPE_DEFAULT);
        auto desc = texture2d_desc(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                             IID_PPV_ARGS(&fr.texture_rgba));
        if (FAILED(hr))
            return hr;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(fr.texture_rgba, &srv,
                                         offset_cpu(fr.srv_heap->GetCPUDescriptorHandleForHeapStart(), fr.srv_inc, 1));
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(fr.texture_rgba, nullptr, &uav,
                                          offset_cpu(fr.srv_heap->GetCPUDescriptorHandleForHeapStart(), fr.srv_inc, 2));
    }

    {
        auto heap = heap_props(D3D12_HEAP_TYPE_UPLOAD);
        auto desc = buffer_desc(64 * 64 * 4);
        hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                             nullptr, IID_PPV_ARGS(&fr.upload_buffer));
        if (FAILED(hr))
            return hr;
        void* mapped = nullptr;
        fr.upload_buffer->Map(0, nullptr, &mapped);
        uint8_t checker[64 * 64 * 4];
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) {
                int idx = (y * 64 + x) * 4;
                bool white = (x / 8 + y / 8) % 2 == 0;
                checker[idx + 0] = white ? 255 : 40;
                checker[idx + 1] = white ? 200 : 20;
                checker[idx + 2] = white ? 150 : 60;
                checker[idx + 3] = 255;
            }
        std::memcpy(mapped, checker, sizeof(checker));
        fr.upload_buffer->Unmap(0, nullptr);
    }

    {
        auto heap = heap_props(D3D12_HEAP_TYPE_UPLOAD);
        {
            auto desc = buffer_desc(sizeof(float) * 4 * 8);
            hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                 nullptr, IID_PPV_ARGS(&fr.vb_sprite));
            if (FAILED(hr))
                return hr;
        }
        {
            auto desc = buffer_desc(sizeof(float) * 8 * 36);
            hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                 nullptr, IID_PPV_ARGS(&fr.vb_quad));
            if (FAILED(hr))
                return hr;
        }
        {
            auto desc = buffer_desc(256);
            hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                 nullptr, IID_PPV_ARGS(&fr.cb_root));
            if (FAILED(hr))
                return hr;
        }
    }

    {
        auto heap = heap_props(D3D12_HEAP_TYPE_DEFAULT);
        auto desc = buffer_desc(1024 * 16, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                             nullptr, IID_PPV_ARGS(&fr.compute_buf));
        if (FAILED(hr))
            return hr;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_UNKNOWN;
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav.Buffer.NumElements = 1024;
        uav.Buffer.StructureByteStride = 16;
        device->CreateUnorderedAccessView(fr.compute_buf, nullptr, &uav,
                                          offset_cpu(fr.srv_heap->GetCPUDescriptorHandleForHeapStart(), fr.srv_inc, 3));
    }

    {
        auto heap = heap_props(D3D12_HEAP_TYPE_UPLOAD);
        auto desc = buffer_desc(sizeof(uint16_t) * 36);
        hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                             nullptr, IID_PPV_ARGS(&fr.index_buf));
        if (FAILED(hr))
            return hr;
        uint16_t indices[36];
        for (int i = 0; i < 6; i++) {
            indices[i * 6 + 0] = static_cast<uint16_t>(i * 4 + 0);
            indices[i * 6 + 1] = static_cast<uint16_t>(i * 4 + 1);
            indices[i * 6 + 2] = static_cast<uint16_t>(i * 4 + 2);
            indices[i * 6 + 3] = static_cast<uint16_t>(i * 4 + 2);
            indices[i * 6 + 4] = static_cast<uint16_t>(i * 4 + 1);
            indices[i * 6 + 5] = static_cast<uint16_t>(i * 4 + 3);
        }
        void* mapped = nullptr;
        fr.index_buf->Map(0, nullptr, &mapped);
        std::memcpy(mapped, indices, sizeof(indices));
        fr.index_buf->Unmap(0, nullptr);
    }

    {
        auto heap = heap_props(D3D12_HEAP_TYPE_UPLOAD);
        auto desc = buffer_desc(sizeof(float) * 4 * 64);
        hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                             nullptr, IID_PPV_ARGS(&fr.instance_buf));
        if (FAILED(hr))
            return hr;
    }

    {
        auto heap = heap_props(D3D12_HEAP_TYPE_READBACK);
        auto desc = buffer_desc(fr.WIDTH * fr.HEIGHT * 4);
        hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                             nullptr, IID_PPV_ARGS(&fr.readback_buf));
        if (FAILED(hr))
            return hr;
    }

    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&fr.alloc));
    if (FAILED(hr))
        return hr;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, fr.alloc, nullptr, IID_PPV_ARGS(&fr.list));
    if (FAILED(hr))
        return hr;
    fr.list->Close();
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fr.fence));
    if (FAILED(hr))
        return hr;
    fr.fence_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!fr.fence_event)
        return E_FAIL;

    return S_OK;
}

static void upload_checker_texture(FrameResources& fr, ID3D12Device* device, ID3D12CommandQueue* queue) {
    fr.alloc->Reset();
    fr.list->Reset(fr.alloc, nullptr);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rows = 0;
    UINT64 row_size = 0;
    UINT64 total = 0;
    D3D12_RESOURCE_DESC tex_desc = texture2d_desc(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM);
    device->GetCopyableFootprints(&tex_desc, 0, 1, 0, &footprint, &rows, &row_size, &total);

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = fr.upload_buffer;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = fr.texture_2d;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    fr.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER tex_barrier =
        transition(fr.texture_2d, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    fr.list->ResourceBarrier(1, &tex_barrier);

    fr.list->Close();
    ID3D12CommandList* base = fr.list;
    queue->ExecuteCommandLists(1, &base);
    queue->Signal(fr.fence, ++fr.fence_val);
    wait_fence(fr.fence, fr.fence_val, fr.fence_event);
}

static const char* kBasicVS = R"(
struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 tint : COLOR0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 tint : COLOR0; };
VSOut VSMain(VSIn i) { VSOut o; o.pos = float4(i.pos, 1); o.uv = i.uv; o.tint = i.tint; return o; }
)";

static const char* kBasicPS = R"(
Texture2D tex : register(t0);
SamplerState samp : register(s0);
struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 tint : COLOR0; };
float4 PSMain(PSIn i) : SV_Target { return tex.Sample(samp, i.uv) * i.tint; }
)";

static const char* kMrtPS = R"(
Texture2D tex : register(t0);
SamplerState samp : register(s0);
struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 tint : COLOR0; };
struct PSOut { float4 c0 : SV_Target0; float4 c1 : SV_Target1; };
PSOut PSMain(PSIn i) {
  PSOut o;
  float4 t = tex.Sample(samp, i.uv);
  o.c0 = t * i.tint;
  o.c1 = float4(t.b, t.g, t.r, 1);
  return o;
}
)";

static const char* kDepthPS = R"(
struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 tint : COLOR0; };
float4 PSMain(PSIn i) : SV_Target {
  float d = i.pos.z;
  return float4(d, d, d, 1) * i.tint;
}
)";

static const char* kRootConstantVS = R"(
cbuffer RootCB : register(b0) { float4x4 mvp; float4 color_shift; };
struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 tint : COLOR0; };
VSOut VSMain(VSIn i) {
  VSOut o;
  o.pos = mul(mvp, float4(i.pos, 1));
  o.uv = i.uv;
  o.tint = color_shift;
  return o;
}
)";

static const char* kComputeCS = R"(
RWStructuredBuffer<float4> output : register(u0);
[numthreads(64, 1, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
  float t = float(tid.x) / 1024.0;
  output[tid.x] = float4(t, 1.0 - t, 0.5, 1.0);
}
)";

struct TestPSO {
    const char* name;
    ID3D12PipelineState* pso = nullptr;
    D3D12_PRIMITIVE_TOPOLOGY topology;
    bool uses_depth;
    bool uses_mrt;
    bool uses_indexed;
    bool uses_instanced;
    bool uses_root_constants;
    D3D12_INPUT_ELEMENT_DESC input_elems[4] = {};
    UINT input_elem_count = 0;
};

static HRESULT create_basic_root_sig(ID3D12Device* device, ID3D12RootSignature** out_sig) {
    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 4;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 2;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 4;

    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 2;
    params[0].DescriptorTable.pDescriptorRanges = ranges;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.Num32BitValues = 8;
    params[1].Constants.ShaderRegister = 0;
    params[1].Constants.RegisterSpace = 0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = samplers[0].AddressV = samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].ShaderRegister = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[1].AddressU = samplers[1].AddressV = samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[1].ShaderRegister = 1;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 2;
    desc.pParameters = params;
    desc.NumStaticSamplers = 2;
    desc.pStaticSamplers = samplers;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* blob = nullptr;
    HRESULT hr = serialize_root_sig(desc, &blob);
    if (FAILED(hr))
        return hr;
    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(out_sig));
    blob->Release();
    return hr;
}

static HRESULT create_compute_root_sig(ID3D12Device* device, ID3D12RootSignature** out_sig) {
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 4;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 1;
    param.DescriptorTable.pDescriptorRanges = &range;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &param;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob* blob = nullptr;
    HRESULT hr = serialize_root_sig(desc, &blob);
    if (FAILED(hr))
        return hr;
    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(out_sig));
    blob->Release();
    return hr;
}

static HRESULT build_test_psos(ID3D12Device* device, ID3D12RootSignature* root_sig,
                               ID3D12RootSignature* compute_root_sig, TestPSO* psos, uint32_t* out_count) {
    *out_count = 0;
    HRESULT hr;

    ID3DBlob* basic_vs = nullptr;
    ID3DBlob* basic_ps = nullptr;
    ID3DBlob* mrt_ps = nullptr;
    ID3DBlob* depth_ps = nullptr;
    ID3DBlob* root_const_vs = nullptr;
    ID3DBlob* compute_cs = nullptr;

    hr = compile_shader(kBasicVS, "VSMain", "vs_5_0", &basic_vs);
    if (FAILED(hr))
        return hr;
    hr = compile_shader(kBasicPS, "PSMain", "ps_5_0", &basic_ps);
    if (FAILED(hr))
        return hr;
    hr = compile_shader(kMrtPS, "PSMain", "ps_5_0", &mrt_ps);
    if (FAILED(hr))
        return hr;
    hr = compile_shader(kDepthPS, "PSMain", "ps_5_0", &depth_ps);
    if (FAILED(hr))
        return hr;
    hr = compile_shader(kRootConstantVS, "VSMain", "vs_5_0", &root_const_vs);
    if (FAILED(hr))
        return hr;
    hr = compile_shader(kComputeCS, "CSMain", "cs_5_0", &compute_cs);
    if (FAILED(hr))
        return hr;

    D3D12_INPUT_ELEMENT_DESC simple_input[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    auto make_gfx_pso = [&](D3D12_SHADER_BYTECODE vs, D3D12_SHADER_BYTECODE ps, const char* name,
                            D3D12_PRIMITIVE_TOPOLOGY_TYPE prim_type, UINT num_rts, bool depth,
                            D3D12_INPUT_ELEMENT_DESC* inputs, UINT input_count) -> HRESULT {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = root_sig;
        desc.VS = vs;
        desc.PS = ps;
        desc.InputLayout = {inputs, input_count};
        desc.PrimitiveTopologyType = prim_type;
        desc.NumRenderTargets = num_rts;
        for (UINT i = 0; i < num_rts; i++)
            desc.RTVFormats[i] = DXGI_FORMAT_R8G8B8A8_UNORM;
        if (depth)
            desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.SampleMask = UINT_MAX;
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable = TRUE;
        for (UINT i = 0; i < num_rts; i++)
            desc.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        if (depth) {
            desc.DepthStencilState.DepthEnable = TRUE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        }
        ID3D12PipelineState* pso = nullptr;
        HRESULT res = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
        if (*out_count < 32) {
            TestPSO& tp = psos[*out_count];
            tp.name = name;
            tp.pso = pso;
            tp.topology = (prim_type == D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE) ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
                                                                                : D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
            tp.uses_depth = depth;
            tp.uses_mrt = num_rts > 1;
            tp.uses_indexed = false;
            tp.uses_instanced = false;
            tp.uses_root_constants = false;
            if (input_count > 0) {
                std::memcpy(tp.input_elems, inputs, sizeof(D3D12_INPUT_ELEMENT_DESC) * input_count);
            }
            tp.input_elem_count = input_count;
            (*out_count)++;
        }
        return res;
    };

    D3D12_SHADER_BYTECODE basic_vs_bc = {basic_vs->GetBufferPointer(), basic_vs->GetBufferSize()};
    D3D12_SHADER_BYTECODE basic_ps_bc = {basic_ps->GetBufferPointer(), basic_ps->GetBufferSize()};
    D3D12_SHADER_BYTECODE mrt_ps_bc = {mrt_ps->GetBufferPointer(), mrt_ps->GetBufferSize()};
    D3D12_SHADER_BYTECODE depth_ps_bc = {depth_ps->GetBufferPointer(), depth_ps->GetBufferSize()};
    D3D12_SHADER_BYTECODE root_const_vs_bc = {root_const_vs->GetBufferPointer(), root_const_vs->GetBufferSize()};

    make_gfx_pso(basic_vs_bc, basic_ps_bc, "basic_triangle", D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 1, false,
                 simple_input, 3);
    make_gfx_pso(basic_vs_bc, basic_ps_bc, "triangle_depth", D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 1, true,
                 simple_input, 3);
    make_gfx_pso(basic_vs_bc, mrt_ps_bc, "mrt_2rt", D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 2, false, simple_input, 3);
    make_gfx_pso(basic_vs_bc, mrt_ps_bc, "mrt_2rt_depth", D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 2, true, simple_input,
                 3);
    make_gfx_pso(basic_vs_bc, depth_ps_bc, "depth_visualize", D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 1, true,
                 simple_input, 3);
    make_gfx_pso(root_const_vs_bc, basic_ps_bc, "root_constants", D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 1, false,
                 simple_input, 2);

    {
        D3D12_INPUT_ELEMENT_DESC inst_input[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"INSTANCEPOS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        };
        make_gfx_pso(basic_vs_bc, basic_ps_bc, "instanced", D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 1, false,
                     inst_input, 3);
        psos[*out_count - 1].uses_instanced = true;
    }

    if (*out_count < 32) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC cdesc = {};
        cdesc.pRootSignature = compute_root_sig;
        cdesc.CS = {compute_cs->GetBufferPointer(), compute_cs->GetBufferSize()};
        ID3D12PipelineState* compute_pso = nullptr;
        hr = device->CreateComputePipelineState(&cdesc, IID_PPV_ARGS(&compute_pso));
        TestPSO& tp = psos[*out_count];
        tp.name = "compute_dispatch";
        tp.pso = compute_pso;
        tp.topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        tp.uses_depth = false;
        tp.uses_mrt = false;
        tp.uses_indexed = false;
        tp.uses_instanced = false;
        tp.uses_root_constants = false;
        tp.input_elem_count = 0;
        (*out_count)++;
    }

    basic_vs->Release();
    basic_ps->Release();
    mrt_ps->Release();
    depth_ps->Release();
    root_const_vs->Release();
    compute_cs->Release();

    return S_OK;
}

static void fill_vertex_data(FrameResources& fr, float player_x, float player_y, uint32_t frame) {
    {
        float verts[8 * 4 * 4] = {};
        int v = 0;
        auto quad = [&](float cx, float cy, float sx, float sy, float r, float g, float b) {
            verts[v++] = cx - sx;
            verts[v++] = cy - sy;
            verts[v++] = 0.3f;
            verts[v++] = 0.0f;
            verts[v++] = 1.0f;
            verts[v++] = r;
            verts[v++] = g;
            verts[v++] = b;
            verts[v++] = cx + sx;
            verts[v++] = cy - sy;
            verts[v++] = 0.3f;
            verts[v++] = 1.0f;
            verts[v++] = 1.0f;
            verts[v++] = r;
            verts[v++] = g;
            verts[v++] = b;
        };
        quad(player_x, player_y, 0.15f, 0.1f, 1, 1, 1);
        quad(-player_x * 0.5f, -player_y * 0.5f, 0.08f, 0.06f, 0.7f, 0.9f, 1.0f);
        quad(0.5f, 0.3f, 0.12f, 0.08f, 0.4f, 0.8f, 1.0f);
        quad(-0.4f, -0.3f, 0.1f, 0.07f, 1.0f, 0.6f, 0.3f);
        void* mapped = nullptr;
        fr.vb_quad->Map(0, nullptr, &mapped);
        std::memcpy(mapped, verts, sizeof(verts));
        fr.vb_quad->Unmap(0, nullptr);
    }

    {
        float instances[64] = {};
        for (int i = 0; i < 16; i++) {
            float t = frame * 0.02f + i * 0.4f;
            instances[i * 4 + 0] = std::sin(t) * 0.6f;
            instances[i * 4 + 1] = std::cos(t * 0.7f) * 0.4f;
            instances[i * 4 + 2] = 0.15f + 0.02f * i;
            instances[i * 4 + 3] = 1.0f;
        }
        void* mapped = nullptr;
        fr.instance_buf->Map(0, nullptr, &mapped);
        std::memcpy(mapped, instances, sizeof(instances));
        fr.instance_buf->Unmap(0, nullptr);
    }

    {
        float cb_data[64] = {};
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                cb_data[i * 4 + j] = (i == j) ? 1.0f : 0.0f;
        float pulse = 0.85f + 0.15f * std::sin(frame * 0.05f);
        cb_data[16] = pulse;
        cb_data[17] = 0.9f;
        cb_data[18] = 1.0f;
        cb_data[19] = 1.0f;
        void* mapped = nullptr;
        fr.cb_root->Map(0, nullptr, &mapped);
        std::memcpy(mapped, cb_data, sizeof(float) * 8 * 4);
        fr.cb_root->Unmap(0, nullptr);
    }
}

struct DrawResult {
    bool drew;
    uint64_t nonzero_pixels;
    uint8_t max_byte;
};

static DrawResult execute_test_draw(FrameResources& fr, ID3D12CommandQueue* queue, ID3D12RootSignature* root_sig,
                                    ID3D12RootSignature* compute_root_sig, TestPSO& pso) {
    DrawResult result = {};

    fr.alloc->Reset();
    fr.list->Reset(fr.alloc, nullptr);

    if (pso.topology == D3D_PRIMITIVE_TOPOLOGY_UNDEFINED) {
        fr.list->SetComputeRootSignature(compute_root_sig);
        fr.list->SetPipelineState(pso.pso);
        fr.list->SetComputeRootDescriptorTable(0, fr.srv_heap->GetGPUDescriptorHandleForHeapStart());
        fr.list->Dispatch(16, 1, 1);
        fr.list->Close();
        ID3D12CommandList* base = fr.list;
        queue->ExecuteCommandLists(1, &base);
        queue->Signal(fr.fence, ++fr.fence_val);
        wait_fence(fr.fence, fr.fence_val, fr.fence_event);
        result.drew = true;
        return result;
    }

    UINT idx = fr.swapchain->GetCurrentBackBufferIndex();

    D3D12_RESOURCE_BARRIER to_rt =
        transition(fr.backbuffers[idx], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    fr.list->ResourceBarrier(1, &to_rt);

    UINT rt_count = pso.uses_mrt ? 2 : 1;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {
        offset_cpu(fr.rtv_heap->GetCPUDescriptorHandleForHeapStart(), fr.rtv_inc, idx),
        offset_cpu(fr.rtv_heap->GetCPUDescriptorHandleForHeapStart(), fr.rtv_inc, 3),
    };
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = fr.dsv_heap->GetCPUDescriptorHandleForHeapStart();

    const float clear_color[4] = {0.02f, 0.02f, 0.06f, 1.0f};
    fr.list->ClearRenderTargetView(rtvs[0], clear_color, 0, nullptr);
    if (pso.uses_mrt)
        fr.list->ClearRenderTargetView(rtvs[1], clear_color, 0, nullptr);
    if (pso.uses_depth)
        fr.list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    fr.list->OMSetRenderTargets(rt_count, rtvs, FALSE, pso.uses_depth ? &dsv : nullptr);

    D3D12_VIEWPORT vp = {0, 0, static_cast<float>(fr.WIDTH), static_cast<float>(fr.HEIGHT), 0, 1};
    D3D12_RECT scissor = {0, 0, static_cast<LONG>(fr.WIDTH), static_cast<LONG>(fr.HEIGHT)};
    fr.list->RSSetViewports(1, &vp);
    fr.list->RSSetScissorRects(1, &scissor);

    fr.list->SetGraphicsRootSignature(root_sig);
    fr.list->SetGraphicsRootDescriptorTable(0, fr.srv_heap->GetGPUDescriptorHandleForHeapStart());
    if (pso.uses_root_constants) {
        fr.list->SetGraphicsRoot32BitConstants(1, 8, nullptr, 0);
    }

    ID3D12DescriptorHeap* heaps[] = {fr.srv_heap};
    fr.list->SetDescriptorHeaps(1, heaps);

    fr.list->SetPipelineState(pso.pso);
    fr.list->IASetPrimitiveTopology(pso.topology);

    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = fr.vb_quad->GetGPUVirtualAddress();
    vbv.StrideInBytes = sizeof(float) * 8;
    vbv.SizeInBytes = sizeof(float) * 8 * 4;

    fr.list->IASetVertexBuffers(0, 1, &vbv);

    if (pso.uses_instanced) {
        D3D12_VERTEX_BUFFER_VIEW inst_vbv = {};
        inst_vbv.BufferLocation = fr.instance_buf->GetGPUVirtualAddress();
        inst_vbv.StrideInBytes = sizeof(float) * 4;
        inst_vbv.SizeInBytes = sizeof(float) * 4 * 16;
        fr.list->IASetVertexBuffers(1, 1, &inst_vbv);
        fr.list->DrawInstanced(6, 4, 0, 0);
    } else {
        fr.list->DrawInstanced(4, 1, 0, 0);
    }

    D3D12_RESOURCE_BARRIER to_present =
        transition(fr.backbuffers[idx], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    fr.list->ResourceBarrier(1, &to_present);
    fr.list->Close();

    ID3D12CommandList* base = fr.list;
    queue->ExecuteCommandLists(1, &base);
    queue->Signal(fr.fence, ++fr.fence_val);
    wait_fence(fr.fence, fr.fence_val, fr.fence_event);

    fr.swapchain->Present(1, 0);

    result.drew = true;
    return result;
}

static uint64_t count_nonzero_readback(FrameResources& fr, ID3D12Device* device, ID3D12CommandQueue* queue,
                                       const char* bmp_path = nullptr) {
    fr.alloc->Reset();
    fr.list->Reset(fr.alloc, nullptr);

    UINT idx = fr.swapchain->GetCurrentBackBufferIndex();
    D3D12_RESOURCE_DESC bb_desc;
    fr.backbuffers[idx]->GetDesc(&bb_desc);

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = fr.backbuffers[idx];
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rows = 0;
    UINT64 row_size = 0;
    UINT64 total = 0;
    device->GetCopyableFootprints(&bb_desc, 0, 1, 0, &footprint, &rows, &row_size, &total);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = fr.readback_buf;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;

    D3D12_RESOURCE_BARRIER to_copy =
        transition(fr.backbuffers[idx], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    fr.list->ResourceBarrier(1, &to_copy);
    fr.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER back_to_present =
        transition(fr.backbuffers[idx], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    fr.list->ResourceBarrier(1, &back_to_present);
    fr.list->Close();

    ID3D12CommandList* base = fr.list;
    queue->ExecuteCommandLists(1, &base);
    queue->Signal(fr.fence, ++fr.fence_val);
    wait_fence(fr.fence, fr.fence_val, fr.fence_event);

    void* mapped = nullptr;
    HRESULT hr = fr.readback_buf->Map(0, nullptr, &mapped);
    if (FAILED(hr) || !mapped)
        return 0;

    uint64_t nonzero = 0;
    uint32_t pitch = footprint.Footprint.RowPitch;
    auto* row_data = static_cast<const uint8_t*>(mapped);
    for (UINT y = 0; y < fr.HEIGHT; y++) {
        const auto* row = row_data + static_cast<size_t>(y) * pitch;
        for (UINT x = 0; x < fr.WIDTH; x++) {
            const auto* px = row + x * 4;
            if (px[0] > 10 || px[1] > 10 || px[2] > 10)
                nonzero++;
        }
    }
    fr.readback_buf->Unmap(0, nullptr);

    if (bmp_path && nonzero > 0) {
        save_bmp_32(bmp_path, static_cast<const uint8_t*>(mapped), fr.WIDTH, fr.HEIGHT, pitch);
    }

    return nonzero;
}

static HRESULT test_dxbc_pso(ID3D12Device* device, ID3D12RootSignature* root_sig, const ShaderBlob& vs_blob,
                             const ShaderBlob& ps_blob) {
    D3D12_INPUT_ELEMENT_DESC inputs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_sig;
    desc.VS = {vs_blob.data.data(), vs_blob.data.size()};
    desc.PS = {ps_blob.data.data(), ps_blob.data.size()};
    desc.InputLayout = {inputs, 3};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    ID3D12PipelineState* pso = nullptr;
    HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
    if (pso)
        pso->Release();
    return hr;
}

int main() {
    const UINT WIDTH = 1280;
    const UINT HEIGHT = 720;

    json_out("{\"ok\":true,\"stage\":\"init\",\"msg\":\"Subnautica full D3D12 stress probe starting\"}");

    std::string shader_cache = getenv_string("DXMT_SHADER_CACHE");
    if (shader_cache.empty())
        shader_cache = "Z:\\tmp\\dxmt_shader_cache";
    std::string subnautica_root = getenv_string("SUBNAUTICA2_ROOT");
    if (subnautica_root.empty())
        subnautica_root = "Z:\\Volumes\\AverySSD\\SteamLibrary\\steamapps\\common\\Subnautica2";

    json_out("{\"ok\":true,\"stage\":\"scan_config\",\"shader_cache\":\"%s\",\"game_root\":\"%s\"}",
             shader_cache.c_str(), subnautica_root.c_str());

    auto shader_blobs = scan_shader_cache(shader_cache);

    uint32_t vs_count = 0, ps_count = 0, cs_count = 0, gs_count = 0, other_count = 0;
    for (auto& b : shader_blobs) {
        if (b.tag == "vs")
            vs_count++;
        else if (b.tag == "ps")
            ps_count++;
        else if (b.tag == "cs")
            cs_count++;
        else if (b.tag == "gs")
            gs_count++;
        else
            other_count++;
    }

    json_out(
        "{\"ok\":true,\"stage\":\"shader_corpus\",\"total\":%u,\"vs\":%u,\"ps\":%u,\"cs\":%u,\"gs\":%u,\"other\":%u}",
        (uint32_t)shader_blobs.size(), vs_count, ps_count, cs_count, gs_count, other_count);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MetalSharpFullStress";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"MetalSharp D3D12 Full Stress - Subnautica Shader Corpus",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, WIDTH, HEIGHT, nullptr, nullptr,
                                wc.hInstance, nullptr);
    if (!hwnd) {
        fail_json("create_window", HRESULT_FROM_WIN32(GetLastError()));
        return 2;
    }
    ShowWindow(hwnd, SW_SHOW);

    HMODULE d3d12_mod = LoadLibraryA("d3d12.dll");
    HMODULE dxgi_mod = LoadLibraryA("dxgi.dll");
    auto create_device = load_proc<D3D12CreateDeviceFn>(d3d12_mod, "D3D12CreateDevice");
    auto create_factory = load_proc<CreateDXGIFactory2Fn>(dxgi_mod, "CreateDXGIFactory2");
    if (!create_device || !create_factory) {
        fail_json("load_exports", HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND));
        return 2;
    }

    IDXGIFactory4* factory = nullptr;
    HRESULT hr = create_factory(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        fail_json("CreateDXGIFactory2", hr);
        return 2;
    }

    {
        UINT adapter_index = 0;
        IDXGIAdapter1* adapter = nullptr;
        while (factory->EnumAdapters1(adapter_index, &adapter) != DXGI_ERROR_NOT_FOUND) {
            DXGI_ADAPTER_DESC1 desc = {};
            adapter->GetDesc1(&desc);
            char name[128] = {};
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
            json_out("{\"ok\":true,\"stage\":\"adapter\",\"index\":%u,\"name\":\"%s\",\"vram_mb\":%llu}", adapter_index,
                     name, (unsigned long long)(desc.DedicatedVideoMemory / (1024 * 1024)));
            adapter->Release();
            adapter_index++;
        }
    }

    ID3D12Device* device = nullptr;
    hr = create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    if (FAILED(hr)) {
        fail_json("D3D12CreateDevice", hr);
        return 2;
    }

    D3D12_FEATURE_DATA_FEATURE_LEVELS fl_query = {};
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
                                  D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    fl_query.pFeatureLevelsRequested = levels;
    fl_query.NumFeatureLevels = 5;
    device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fl_query, sizeof(fl_query));
    json_out("{\"ok\":true,\"stage\":\"feature_level\",\"max_supported\":%u}",
             (unsigned)fl_query.MaxSupportedFeatureLevel);

    D3D12_FEATURE_DATA_SHADER_MODEL sm_query = {};
    sm_query.HighestShaderModel = D3D_SHADER_MODEL_6_6;
    device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm_query, sizeof(sm_query));
    json_out("{\"ok\":true,\"stage\":\"shader_model\",\"max_supported\":%u}", (unsigned)sm_query.HighestShaderModel);

    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr;
    hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue));
    if (FAILED(hr)) {
        fail_json("CreateCommandQueue", hr);
        return 2;
    }

    FrameResources fr = {};
    hr = init_frame_resources(fr, device, queue, factory, hwnd);
    if (FAILED(hr)) {
        fail_json("init_frame_resources", hr);
        return 2;
    }

    upload_checker_texture(fr, device, queue);
    json_out("{\"ok\":true,\"stage\":\"texture_upload\",\"msg\":\"checker texture uploaded\"}");

    ID3D12RootSignature* root_sig = nullptr;
    hr = create_basic_root_sig(device, &root_sig);
    if (FAILED(hr)) {
        fail_json("create_root_sig", hr);
        return 2;
    }

    ID3D12RootSignature* compute_root_sig = nullptr;
    hr = create_compute_root_sig(device, &compute_root_sig);
    if (FAILED(hr)) {
        fail_json("create_compute_root_sig", hr);
        return 2;
    }

    TestPSO test_psos[32] = {};
    uint32_t test_pso_count = 0;
    hr = build_test_psos(device, root_sig, compute_root_sig, test_psos, &test_pso_count);
    if (FAILED(hr)) {
        fail_json("build_test_psos", hr);
        return 2;
    }

    uint32_t pso_ok = 0, pso_fail = 0;
    for (uint32_t i = 0; i < test_pso_count; i++) {
        if (test_psos[i].pso)
            pso_ok++;
        else
            pso_fail++;
    }
    json_out("{\"ok\":true,\"stage\":\"builtin_psos\",\"total\":%u,\"ok\":%u,\"fail\":%u}", test_pso_count, pso_ok,
             pso_fail);

    json_out("{\"ok\":true,\"stage\":\"dxbc_pso_test_begin\",\"corpus_size\":%u}", (uint32_t)shader_blobs.size());

    std::vector<const ShaderBlob*> vs_blobs, ps_blobs;
    for (auto& b : shader_blobs) {
        if (b.tag == "vs")
            vs_blobs.push_back(&b);
        else if (b.tag == "ps")
            ps_blobs.push_back(&b);
    }

    uint32_t dxbc_tested = 0;
    uint32_t dxbc_pass = 0;
    uint32_t dxbc_fail = 0;
    uint32_t dxbc_max_pairs = std::min({(uint32_t)vs_blobs.size(), (uint32_t)ps_blobs.size(), 200u});

    for (uint32_t i = 0; i < dxbc_max_pairs; i++) {
        const auto& vs = *vs_blobs[i % vs_blobs.size()];
        const auto& ps = *ps_blobs[i % ps_blobs.size()];
        char test_name[64];
        std::snprintf(test_name, sizeof(test_name), "dxbc_pair_%u", i);

        HRESULT pso_hr = test_dxbc_pso(device, root_sig, vs, ps);
        dxbc_tested++;
        if (SUCCEEDED(pso_hr)) {
            dxbc_pass++;
        } else {
            dxbc_fail++;
            if (dxbc_fail <= 20) {
                json_out("{\"ok\":false,\"stage\":\"dxbc_pso_create\",\"pair\":%u,\"hr\":\"%s\",\"vs_size\":%u,\"ps_"
                         "size\":%u}",
                         i, hr_hex(pso_hr).c_str(), (uint32_t)vs.data.size(), (uint32_t)ps.data.size());
            }
        }

        if (dxbc_tested % 50 == 0) {
            json_out("{\"ok\":true,\"stage\":\"dxbc_pso_progress\",\"tested\":%u,\"pass\":%u,\"fail\":%u}", dxbc_tested,
                     dxbc_pass, dxbc_fail);
        }
    }

    json_out("{\"ok\":true,\"stage\":\"dxbc_pso_test_done\",\"tested\":%u,\"pass\":%u,\"fail\":%u}", dxbc_tested,
             dxbc_pass, dxbc_fail);

    json_out("{\"ok\":true,\"stage\":\"render_loop_start\",\"pso_count\":%u,\"msg\":\"Cycling through PSOs with draw "
             "calls\"}",
             test_pso_count);

    bool running = true;
    const char* exit_reason = "loop_complete";
    uint32_t frame = 0;
    uint32_t total_frames = 600;
    float player_x = 0.0f, player_y = 0.0f;

    uint32_t draws_ok = 0;
    uint32_t draws_fail = 0;
    uint64_t total_nonzero = 0;

    std::string screen_dir = getenv_string("FULL_STRESS_SCREEN_DIR");
    if (screen_dir.empty())
        screen_dir = "Z:\\tmp\\stress_screens";
    CreateDirectoryA(screen_dir.c_str(), nullptr);

    uint32_t last_pso_idx = 0xFFFFFFFF;

    while (running && frame < total_frames) {
        MSG msg = {};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                exit_reason = "wm_quit";
                running = false;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            exit_reason = "escape";
            running = false;
        }
        if (GetAsyncKeyState(VK_LEFT) & 0x8000)
            player_x -= 0.02f;
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
            player_x += 0.02f;
        if (GetAsyncKeyState(VK_UP) & 0x8000)
            player_y += 0.02f;
        if (GetAsyncKeyState(VK_DOWN) & 0x8000)
            player_y -= 0.02f;

        fill_vertex_data(fr, player_x, player_y, frame);

        uint32_t pso_idx = frame % test_pso_count;
        TestPSO& current_pso = test_psos[pso_idx];

        if (current_pso.pso) {
            auto draw_result = execute_test_draw(fr, queue, root_sig, compute_root_sig, current_pso);
            if (draw_result.drew) {
                draws_ok++;
            } else {
                draws_fail++;
            }
        }

        if (pso_idx != last_pso_idx) {
            last_pso_idx = pso_idx;
            if (frame > 0) {
                char snap[256];
                std::snprintf(snap, sizeof(snap), "%s\\frame_%u_pso_%s.bmp", screen_dir.c_str(), frame,
                              current_pso.name);
                uint64_t nz = count_nonzero_readback(fr, device, queue, snap);
                total_nonzero += nz;
                json_out("{\"ok\":true,\"stage\":\"pso_transition\",\"frame\":%u,\"pso\":\"%s\",\"nonzero_px\":%llu,"
                         "\"screen\":\"%s\"}",
                         frame, current_pso.name, (unsigned long long)nz, snap);
            }
        }

        if (frame % 60 == 0 && frame > 0) {
            char snap[256];
            std::snprintf(snap, sizeof(snap), "%s\\heartbeat_%u.bmp", screen_dir.c_str(), frame);
            uint64_t nz = count_nonzero_readback(fr, device, queue, snap);
            total_nonzero += nz;
            json_out("{\"ok\":true,\"stage\":\"render_heartbeat\",\"frame\":%u,\"pso\":\"%s\",\"nonzero_px\":%llu,"
                     "\"screen\":\"%s\"}",
                     frame, current_pso.name, (unsigned long long)nz, snap);
        }

        frame++;
        Sleep(8);
    }

    {
        char final_snap[256];
        std::snprintf(final_snap, sizeof(final_snap), "%s\\final.bmp", screen_dir.c_str());
        uint64_t final_nz = count_nonzero_readback(fr, device, queue, final_snap);
        total_nonzero += final_nz;
        json_out("{\"ok\":true,\"stage\":\"final_readback\",\"nonzero_px\":%llu,\"screen\":\"%s\"}",
                 (unsigned long long)final_nz, final_snap);
    }

    json_out("{\"ok\":true,\"stage\":\"complete\",\"frames\":%u,\"draws_ok\":%u,\"draws_fail\":%u,"
             "\"dxbc_tested\":%u,\"dxbc_pass\":%u,\"dxbc_fail\":%u,"
             "\"builtin_pso_ok\":%u,\"total_nonzero_px\":%llu,\"exit\":\"%s\"}",
             frame, draws_ok, draws_fail, dxbc_tested, dxbc_pass, dxbc_fail, pso_ok, (unsigned long long)total_nonzero,
             exit_reason);

    return 0;
}
