#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

using D3D12CreateDeviceFn = HRESULT(WINAPI *)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
using D3D12SerializeRootSignatureFn = HRESULT(WINAPI *)(const D3D12_ROOT_SIGNATURE_DESC *,
                                                        D3D_ROOT_SIGNATURE_VERSION, ID3DBlob **, ID3DBlob **);
using D3DCompileFn = HRESULT(WINAPI *)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR,
                                       LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
using CreateDXGIFactory2Fn = HRESULT(WINAPI *)(UINT, REFIID, void **);

static const GUID IID_D3D12DeviceProbe = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};

template <typename T> static void safe_release(T *&object) {
    if (object) {
        object->Release();
        object = nullptr;
    }
}

template <typename T> static T load_proc(HMODULE module, const char *name) {
    T fn = nullptr;
    FARPROC proc = module ? GetProcAddress(module, name) : nullptr;
    std::memcpy(&fn, &proc, sizeof(fn));
    return fn;
}

static std::string getenv_string(const char *key) {
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

static void fail_json(const char *stage, HRESULT hr, const std::string &detail = "") {
    std::printf("{\"ok\":false,\"stage\":\"%s\",\"hr\":\"%s\",\"detail\":\"%s\"}\n", stage, hr_hex(hr).c_str(),
                detail.c_str());
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_CLOSE) {
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
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return desc;
}

static D3D12_RESOURCE_DESC texture_desc(UINT width, UINT height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags) {
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

static D3D12_RESOURCE_BARRIER transition(ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
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

struct Image {
    UINT width = 0;
    UINT height = 0;
    std::vector<uint8_t> rgba;
};

static bool load_bmp_24(const std::string &path, Image &image, std::string &error) {
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "open bmp failed";
        return false;
    }
    DWORD size = GetFileSize(file, nullptr);
    std::vector<uint8_t> bytes(size);
    DWORD read = 0;
    BOOL ok = ReadFile(file, bytes.data(), size, &read, nullptr);
    CloseHandle(file);
    if (!ok || read != size || size < 54 || bytes[0] != 'B' || bytes[1] != 'M') {
        error = "invalid bmp";
        return false;
    }
    uint32_t data_offset = *reinterpret_cast<const uint32_t *>(&bytes[10]);
    int32_t width = *reinterpret_cast<const int32_t *>(&bytes[18]);
    int32_t height_raw = *reinterpret_cast<const int32_t *>(&bytes[22]);
    uint16_t bpp = *reinterpret_cast<const uint16_t *>(&bytes[28]);
    if (width <= 0 || height_raw == 0 || bpp != 24) {
        error = "only 24-bit bmp supported";
        return false;
    }

    UINT height = static_cast<UINT>(height_raw < 0 ? -height_raw : height_raw);
    UINT row_bytes = ((static_cast<UINT>(width) * 3u + 3u) / 4u) * 4u;
    if (data_offset + row_bytes * height > bytes.size()) {
        error = "bmp data truncated";
        return false;
    }

    image.width = static_cast<UINT>(width);
    image.height = height;
    image.rgba.assign(static_cast<size_t>(image.width) * image.height * 4u, 255);
    bool bottom_up = height_raw > 0;
    for (UINT y = 0; y < image.height; y++) {
        UINT src_y = bottom_up ? (image.height - 1u - y) : y;
        const uint8_t *src = bytes.data() + data_offset + static_cast<size_t>(src_y) * row_bytes;
        uint8_t *dst = image.rgba.data() + static_cast<size_t>(y) * image.width * 4u;
        for (UINT x = 0; x < image.width; x++) {
            dst[x * 4 + 0] = src[x * 3 + 2];
            dst[x * 4 + 1] = src[x * 3 + 1];
            dst[x * 4 + 2] = src[x * 3 + 0];
            dst[x * 4 + 3] = 255;
        }
    }
    return true;
}

static HRESULT compile_shader(const char *src, const char *entry, const char *target, ID3DBlob **blob,
                              std::string &errors) {
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    D3DCompileFn compile = load_proc<D3DCompileFn>(compiler, "D3DCompile");
    if (!compile)
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);

    ID3DBlob *err_blob = nullptr;
    HRESULT hr = compile(src, std::strlen(src), "subnautica_stress.hlsl", nullptr, nullptr, entry, target, 0, 0, blob,
                         &err_blob);
    if (err_blob) {
        errors.assign(static_cast<const char *>(err_blob->GetBufferPointer()), err_blob->GetBufferSize());
        err_blob->Release();
    }
    return hr;
}

static HRESULT serialize_root_signature(const D3D12_ROOT_SIGNATURE_DESC &desc, ID3DBlob **blob, std::string &errors) {
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    D3D12SerializeRootSignatureFn serialize =
        load_proc<D3D12SerializeRootSignatureFn>(d3d12, "D3D12SerializeRootSignature");
    if (!serialize)
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    ID3DBlob *err_blob = nullptr;
    HRESULT hr = serialize(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, blob, &err_blob);
    if (err_blob) {
        errors.assign(static_cast<const char *>(err_blob->GetBufferPointer()), err_blob->GetBufferSize());
        err_blob->Release();
    }
    return hr;
}

static bool wait_for_fence(ID3D12Fence *fence, UINT64 value, HANDLE event_handle) {
    if (fence->GetCompletedValue() >= value)
        return true;
    if (FAILED(fence->SetEventOnCompletion(value, event_handle)))
        return false;
    return WaitForSingleObject(event_handle, 10000) == WAIT_OBJECT_0;
}

struct SpriteVertex {
    float x;
    float y;
    float size;
    float phase;
};

struct QuadVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
};

static const char *kGraphicsHlsl = R"(
Texture2D splashTex : register(t0);
SamplerState splashSampler : register(s0);

struct VSIn {
  float2 pos : POSITION;
  float size : SIZE;
  float phase : PHASE;
};

struct VSOut {
  float2 pos : POSITION;
  float size : SIZE;
  float phase : PHASE;
};

struct GSOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
  float4 tint : COLOR0;
};

VSOut VSMain(VSIn input) {
  VSOut o;
  o.pos = input.pos;
  o.size = input.size;
  o.phase = input.phase;
  return o;
}

[maxvertexcount(4)]
void GSMain(point VSOut input[1], inout TriangleStream<GSOut> stream) {
  float s = input[0].size;
  float2 p = input[0].pos;
  float wobble = sin(input[0].phase) * 0.08;
  float4 tint = float4(0.55 + 0.45 * cos(input[0].phase), 0.8, 1.0, 1.0);
  float2 offsets[4] = {
    float2(-s, -s * 0.65 + wobble),
    float2(-s,  s * 0.65 + wobble),
    float2( s, -s * 0.65 - wobble),
    float2( s,  s * 0.65 - wobble)
  };
  float2 uvs[4] = {
    float2(0, 1),
    float2(0, 0),
    float2(1, 1),
    float2(1, 0)
  };
  [unroll]
  for (int i = 0; i < 4; i++) {
    GSOut o;
    o.pos = float4(p + offsets[i], 0.25, 1.0);
    o.uv = uvs[i];
    o.tint = tint;
    stream.Append(o);
  }
}

struct PSOut {
  float4 color0 : SV_Target0;
  float4 color1 : SV_Target1;
};

PSOut PSMain(GSOut input) {
  float4 tex = splashTex.Sample(splashSampler, input.uv);
  PSOut o;
  o.color0 = tex * input.tint;
  o.color1 = float4(tex.b, tex.g * 0.5, tex.r, 1.0);
  return o;
}
)";

static const char *kFallbackHlsl = R"(
Texture2D splashTex : register(t0);
SamplerState splashSampler : register(s0);

struct VSIn {
  float3 pos : POSITION;
  float2 uv : TEXCOORD0;
  float4 tint : COLOR0;
};

struct VSOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
  float4 tint : COLOR0;
};

VSOut VSMain(VSIn input) {
  VSOut o;
  o.pos = float4(input.pos, 1.0);
  o.uv = input.uv;
  o.tint = input.tint;
  return o;
}

struct PSOut {
  float4 color0 : SV_Target0;
  float4 color1 : SV_Target1;
};

PSOut PSMain(VSOut input) {
  float4 tex = splashTex.Sample(splashSampler, input.uv);
  PSOut o;
  o.color0 = tex * input.tint;
  o.color1 = float4(tex.b * input.tint.b, tex.g * 0.45, tex.r * input.tint.r, 1.0);
  return o;
}
)";

static const char *kComputeHlsl = R"(
RWStructuredBuffer<float4> values : register(u0);
[numthreads(64, 1, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
  values[tid.x] = float4(tid.x / 64.0, 1.0 - tid.x / 64.0, 0.25, 1.0);
}
)";

int main() {
    const UINT width = 1280;
    const UINT height = 720;
    std::string root = getenv_string("SUBNAUTICA2_ROOT");
    if (root.empty())
        root = "Z:/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2";
    std::string bmp_path = root + "/Subnautica2/Content/Splash/Splash.bmp";

    Image splash;
    std::string load_error;
    if (!load_bmp_24(bmp_path, splash, load_error)) {
        fail_json("load_subnautica_splash", E_FAIL, load_error);
        return 2;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MetalSharpSubnauticaStressGame";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"MetalSharp D3D12 Subnautica Stress Game", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        fail_json("create_window", HRESULT_FROM_WIN32(GetLastError()));
        return 2;
    }
    ShowWindow(hwnd, SW_SHOW);

    HMODULE d3d12_mod = LoadLibraryA("d3d12.dll");
    HMODULE dxgi_mod = LoadLibraryA("dxgi.dll");
    D3D12CreateDeviceFn create_device = load_proc<D3D12CreateDeviceFn>(d3d12_mod, "D3D12CreateDevice");
    CreateDXGIFactory2Fn create_factory = load_proc<CreateDXGIFactory2Fn>(dxgi_mod, "CreateDXGIFactory2");
    if (!create_device || !create_factory) {
        fail_json("load_d3d12_dxgi_exports", HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND));
        return 2;
    }

    IDXGIFactory4 *factory = nullptr;
    HRESULT hr = create_factory(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        fail_json("CreateDXGIFactory2", hr);
        return 2;
    }

    ID3D12Device *device = nullptr;
    hr = create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_D3D12DeviceProbe, reinterpret_cast<void **>(&device));
    if (FAILED(hr)) {
        fail_json("D3D12CreateDevice", hr);
        return 2;
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue *queue = nullptr;
    hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue));
    if (FAILED(hr)) {
        fail_json("CreateCommandQueue", hr);
        return 2;
    }

    DXGI_SWAP_CHAIN_DESC1 sc_desc = {};
    sc_desc.Width = width;
    sc_desc.Height = height;
    sc_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc_desc.SampleDesc.Count = 1;
    sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc_desc.BufferCount = 3;
    sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1 *sc1 = nullptr;
    hr = factory->CreateSwapChainForHwnd(queue, hwnd, &sc_desc, nullptr, nullptr, &sc1);
    if (FAILED(hr)) {
        fail_json("CreateSwapChainForHwnd", hr);
        return 2;
    }
    IDXGISwapChain3 *swapchain = nullptr;
    hr = sc1->QueryInterface(IID_PPV_ARGS(&swapchain));
    safe_release(sc1);
    if (FAILED(hr)) {
        fail_json("QuerySwapChain3", hr);
        return 2;
    }

    ID3D12DescriptorHeap *rtv_heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.NumDescriptors = 4;
    hr = device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap));
    if (FAILED(hr)) {
        fail_json("CreateRTVHeap", hr);
        return 2;
    }
    UINT rtv_inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    ID3D12Resource *backbuffers[3] = {};
    for (UINT i = 0; i < 3; i++) {
        hr = swapchain->GetBuffer(i, IID_PPV_ARGS(&backbuffers[i]));
        if (FAILED(hr)) {
            fail_json("SwapChainGetBuffer", hr);
            return 2;
        }
        device->CreateRenderTargetView(backbuffers[i], nullptr, offset_cpu(rtv_heap->GetCPUDescriptorHandleForHeapStart(), rtv_inc, i));
    }

    ID3D12Resource *offscreen = nullptr;
    D3D12_CLEAR_VALUE off_clear = {};
    off_clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    off_clear.Color[3] = 1.0f;
    D3D12_RESOURCE_DESC off_desc =
        texture_desc(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    D3D12_HEAP_PROPERTIES default_heap = heap_props(D3D12_HEAP_TYPE_DEFAULT);
    hr = device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &off_desc,
                                         D3D12_RESOURCE_STATE_RENDER_TARGET, &off_clear, IID_PPV_ARGS(&offscreen));
    if (FAILED(hr)) {
        fail_json("CreateOffscreenMRT", hr);
        return 2;
    }
    device->CreateRenderTargetView(offscreen, nullptr, offset_cpu(rtv_heap->GetCPUDescriptorHandleForHeapStart(), rtv_inc, 3));

    ID3D12DescriptorHeap *dsv_heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
    dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv_heap_desc.NumDescriptors = 1;
    hr = device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&dsv_heap));
    if (FAILED(hr)) {
        fail_json("CreateDSVHeap", hr);
        return 2;
    }

    ID3D12Resource *depth = nullptr;
    D3D12_CLEAR_VALUE depth_clear = {};
    depth_clear.Format = DXGI_FORMAT_D32_FLOAT;
    depth_clear.DepthStencil.Depth = 1.0f;
    D3D12_RESOURCE_DESC depth_desc =
        texture_desc(width, height, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    hr = device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &depth_desc,
                                         D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth_clear, IID_PPV_ARGS(&depth));
    if (FAILED(hr)) {
        fail_json("CreateDepth", hr);
        return 2;
    }
    device->CreateDepthStencilView(depth, nullptr, dsv_heap->GetCPUDescriptorHandleForHeapStart());

    ID3D12DescriptorHeap *srv_heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {};
    srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap_desc.NumDescriptors = 2;
    srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&srv_heap));
    if (FAILED(hr)) {
        fail_json("CreateSRVUAVHeap", hr);
        return 2;
    }
    UINT srv_inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ID3D12Resource *texture = nullptr;
    D3D12_RESOURCE_DESC tex_desc = texture_desc(splash.width, splash.height, DXGI_FORMAT_R8G8B8A8_UNORM,
                                                D3D12_RESOURCE_FLAG_NONE);
    hr = device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &tex_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                         nullptr, IID_PPV_ARGS(&texture));
    if (FAILED(hr)) {
        fail_json("CreateSplashTexture", hr);
        return 2;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rows = 0;
    UINT64 row_size = 0;
    UINT64 upload_size = 0;
    device->GetCopyableFootprints(&tex_desc, 0, 1, 0, &footprint, &rows, &row_size, &upload_size);
    ID3D12Resource *upload = nullptr;
    D3D12_HEAP_PROPERTIES upload_heap = heap_props(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC upload_desc = buffer_desc(upload_size);
    hr = device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc,
                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));
    if (FAILED(hr)) {
        fail_json("CreateTextureUpload", hr);
        return 2;
    }
    void *mapped = nullptr;
    hr = upload->Map(0, nullptr, &mapped);
    if (FAILED(hr)) {
        fail_json("MapTextureUpload", hr);
        return 2;
    }
    for (UINT y = 0; y < splash.height; y++) {
        std::memcpy(static_cast<uint8_t *>(mapped) + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
                    splash.rgba.data() + static_cast<size_t>(y) * splash.width * 4u, static_cast<size_t>(splash.width) * 4u);
    }
    upload->Unmap(0, nullptr);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(texture, &srv_desc, srv_heap->GetCPUDescriptorHandleForHeapStart());

    ID3D12Resource *compute_buffer = nullptr;
    D3D12_RESOURCE_DESC compute_desc = buffer_desc(64 * 16, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    hr = device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &compute_desc,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&compute_buffer));
    if (FAILED(hr)) {
        fail_json("CreateComputeUAV", hr);
        return 2;
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
    uav_desc.Format = DXGI_FORMAT_UNKNOWN;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.NumElements = 64;
    uav_desc.Buffer.StructureByteStride = 16;
    device->CreateUnorderedAccessView(compute_buffer, nullptr, &uav_desc,
                                      offset_cpu(srv_heap->GetCPUDescriptorHandleForHeapStart(), srv_inc, 1));

    std::string shader_errors;
    ID3DBlob *vs = nullptr;
    ID3DBlob *gs = nullptr;
    ID3DBlob *ps = nullptr;
    ID3DBlob *fallback_vs = nullptr;
    ID3DBlob *fallback_ps = nullptr;
    ID3DBlob *cs = nullptr;
    if (FAILED(hr = compile_shader(kGraphicsHlsl, "VSMain", "vs_5_0", &vs, shader_errors)) ||
        FAILED(hr = compile_shader(kGraphicsHlsl, "GSMain", "gs_5_0", &gs, shader_errors)) ||
        FAILED(hr = compile_shader(kGraphicsHlsl, "PSMain", "ps_5_0", &ps, shader_errors)) ||
        FAILED(hr = compile_shader(kFallbackHlsl, "VSMain", "vs_5_0", &fallback_vs, shader_errors)) ||
        FAILED(hr = compile_shader(kFallbackHlsl, "PSMain", "ps_5_0", &fallback_ps, shader_errors)) ||
        FAILED(hr = compile_shader(kComputeHlsl, "CSMain", "cs_5_0", &cs, shader_errors))) {
        fail_json("D3DCompile", hr, shader_errors);
        return 2;
    }

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 1;

    D3D12_ROOT_PARAMETER root_params[2] = {};
    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_params[0].DescriptorTable.NumDescriptorRanges = 1;
    root_params[0].DescriptorTable.pDescriptorRanges = &ranges[0];
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_params[1].DescriptorTable.NumDescriptorRanges = 1;
    root_params[1].DescriptorTable.pDescriptorRanges = &ranges[1];
    root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rs_desc = {};
    rs_desc.NumParameters = 2;
    rs_desc.pParameters = root_params;
    rs_desc.NumStaticSamplers = 1;
    rs_desc.pStaticSamplers = &sampler;
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob *rs_blob = nullptr;
    if (FAILED(hr = serialize_root_signature(rs_desc, &rs_blob, shader_errors))) {
        fail_json("SerializeRootSignature", hr, shader_errors);
        return 2;
    }
    ID3D12RootSignature *root_sig = nullptr;
    hr = device->CreateRootSignature(0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(), IID_PPV_ARGS(&root_sig));
    if (FAILED(hr)) {
        fail_json("CreateRootSignature", hr);
        return 2;
    }

    D3D12_INPUT_ELEMENT_DESC input_elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"SIZE", 0, DXGI_FORMAT_R32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"PHASE", 0, DXGI_FORMAT_R32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.pRootSignature = root_sig;
    pso_desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pso_desc.GS = {gs->GetBufferPointer(), gs->GetBufferSize()};
    pso_desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pso_desc.InputLayout = {input_elements, 3};
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    pso_desc.NumRenderTargets = 2;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.SampleDesc.Count = 1;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.RasterizerState.DepthClipEnable = TRUE;
    pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso_desc.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso_desc.DepthStencilState.DepthEnable = TRUE;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    ID3D12PipelineState *graphics_pso = nullptr;
    hr = device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&graphics_pso));
    if (FAILED(hr)) {
        fail_json("CreateGeometryGraphicsPSO", hr);
        graphics_pso = nullptr;
    }

    D3D12_INPUT_ELEMENT_DESC fallback_input_elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC fallback_pso_desc = pso_desc;
    fallback_pso_desc.VS = {fallback_vs->GetBufferPointer(), fallback_vs->GetBufferSize()};
    fallback_pso_desc.GS = {};
    fallback_pso_desc.PS = {fallback_ps->GetBufferPointer(), fallback_ps->GetBufferSize()};
    fallback_pso_desc.InputLayout = {fallback_input_elements, 3};
    fallback_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    ID3D12PipelineState *fallback_pso = nullptr;
    hr = device->CreateGraphicsPipelineState(&fallback_pso_desc, IID_PPV_ARGS(&fallback_pso));
    if (FAILED(hr)) {
        fail_json("CreateFallbackGraphicsPSO", hr);
        return 2;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC cpso_desc = {};
    cpso_desc.pRootSignature = root_sig;
    cpso_desc.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
    ID3D12PipelineState *compute_pso = nullptr;
    hr = device->CreateComputePipelineState(&cpso_desc, IID_PPV_ARGS(&compute_pso));
    if (FAILED(hr)) {
        fail_json("CreateComputePSO", hr);
        return 2;
    }

    ID3D12CommandAllocator *allocator = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    ID3D12Fence *fence = nullptr;
    HANDLE fence_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    UINT64 fence_value = 0;
    if (FAILED(hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
        FAILED(hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list))) ||
        FAILED(hr = list->Close()) || FAILED(hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) ||
        !fence_event) {
        fail_json("CreateCommandObjects", hr);
        return 2;
    }

    if (FAILED(allocator->Reset()) || FAILED(list->Reset(allocator, nullptr))) {
        fail_json("InitialCommandReset", E_FAIL);
        return 2;
    }
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = upload;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = texture;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER tex_ready = transition(texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    list->ResourceBarrier(1, &tex_ready);
    list->Close();
    ID3D12CommandList *base = list;
    queue->ExecuteCommandLists(1, &base);
    queue->Signal(fence, ++fence_value);
    wait_for_fence(fence, fence_value, fence_event);

    ID3D12Resource *vertex_buffer = nullptr;
    D3D12_RESOURCE_DESC vb_desc = buffer_desc(sizeof(SpriteVertex) * 6);
    hr = device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &vb_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                         nullptr, IID_PPV_ARGS(&vertex_buffer));
    if (FAILED(hr)) {
        fail_json("CreateVertexBuffer", hr);
        return 2;
    }
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
    vbv.SizeInBytes = sizeof(SpriteVertex) * 6;
    vbv.StrideInBytes = sizeof(SpriteVertex);

    ID3D12Resource *quad_buffer = nullptr;
    constexpr UINT kQuadCount = 10;
    constexpr UINT kQuadVertexCount = kQuadCount * 6;
    D3D12_RESOURCE_DESC quad_vb_desc = buffer_desc(sizeof(QuadVertex) * kQuadVertexCount);
    hr = device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &quad_vb_desc,
                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&quad_buffer));
    if (FAILED(hr)) {
        fail_json("CreateFallbackQuadBuffer", hr);
        return 2;
    }
    D3D12_VERTEX_BUFFER_VIEW quad_vbv = {};
    quad_vbv.BufferLocation = quad_buffer->GetGPUVirtualAddress();
    quad_vbv.SizeInBytes = sizeof(QuadVertex) * kQuadVertexCount;
    quad_vbv.StrideInBytes = sizeof(QuadVertex);

    std::printf("{\"ok\":true,\"stage\":\"running\",\"asset\":\"%s\",\"geometry_pso_api_ok\":%s,"
                "\"fallback_pso_ok\":true,\"controls\":\"arrows move, escape quits\"}\n",
                bmp_path.c_str(), graphics_pso ? "true" : "false");
    std::fflush(stdout);

    bool running = true;
    const char *exit_reason = "loop_complete";
    UINT frame = 0;
    float player_x = 0.0f;
    float player_y = 0.0f;
    while (running) {
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

        SpriteVertex sprites[6] = {
            {player_x, player_y, 0.32f, frame * 0.05f},
            {-0.65f, 0.45f, 0.22f, frame * 0.03f + 1.0f},
            {0.65f, 0.45f, 0.22f, frame * 0.04f + 2.0f},
            {-0.55f, -0.45f, 0.18f, frame * 0.07f + 3.0f},
            {0.55f, -0.45f, 0.18f, frame * 0.06f + 4.0f},
            {0.0f, -0.68f, 0.14f, frame * 0.09f + 5.0f},
        };
        void *vb_mapped = nullptr;
        vertex_buffer->Map(0, nullptr, &vb_mapped);
        std::memcpy(vb_mapped, sprites, sizeof(sprites));
        vertex_buffer->Unmap(0, nullptr);

        auto write_quad = [](QuadVertex *dst, float cx, float cy, float sx, float sy, float z, float r, float g,
                             float b) {
            QuadVertex q[6] = {
                {cx - sx, cy - sy, z, 0.0f, 1.0f, r, g, b, 1.0f},
                {cx - sx, cy + sy, z, 0.0f, 0.0f, r, g, b, 1.0f},
                {cx + sx, cy - sy, z, 1.0f, 1.0f, r, g, b, 1.0f},
                {cx + sx, cy - sy, z, 1.0f, 1.0f, r, g, b, 1.0f},
                {cx - sx, cy + sy, z, 0.0f, 0.0f, r, g, b, 1.0f},
                {cx + sx, cy + sy, z, 1.0f, 0.0f, r, g, b, 1.0f},
            };
            std::memcpy(dst, q, sizeof(q));
        };
        QuadVertex quads[kQuadVertexCount] = {};
        float pulse = 0.85f + 0.15f * static_cast<float>((frame % 120) < 60 ? (frame % 60) / 60.0 : (120 - frame % 120) / 60.0);
        write_quad(&quads[0], 0.0f, 0.0f, 1.32f, 0.88f, 0.55f, 0.55f, 0.72f, pulse);
        write_quad(&quads[6], player_x, player_y, 0.24f, 0.16f, 0.18f, 1.05f, 1.05f, 1.05f);
        write_quad(&quads[12], -player_x * 0.6f, -player_y * 0.6f, 0.18f, 0.12f, 0.28f, pulse, 0.75f, 1.15f);
        for (UINT qi = 3; qi < kQuadCount; qi++) {
            float t = frame * (0.013f + 0.002f * qi);
            float cx = std::sin(t + qi * 0.7f) * (0.78f - 0.035f * qi);
            float cy = std::cos(t * 0.8f + qi * 0.35f) * (0.48f - 0.018f * qi);
            float s = 0.07f + 0.012f * static_cast<float>(qi % 4);
            write_quad(&quads[qi * 6], cx, cy, s * 1.5f, s, 0.20f + 0.025f * qi, 0.45f + 0.05f * qi,
                       0.75f, 1.15f - 0.04f * qi);
        }
        void *quad_mapped = nullptr;
        quad_buffer->Map(0, nullptr, &quad_mapped);
        std::memcpy(quad_mapped, quads, sizeof(quads));
        quad_buffer->Unmap(0, nullptr);

        UINT idx = swapchain->GetCurrentBackBufferIndex();
        allocator->Reset();
        list->Reset(allocator, nullptr);
        ID3D12DescriptorHeap *heaps[] = {srv_heap};
        list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootSignature(root_sig);
        list->SetPipelineState(compute_pso);
        D3D12_GPU_DESCRIPTOR_HANDLE gpu0 = srv_heap->GetGPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gpu1 = gpu0;
        gpu1.ptr += srv_inc;
        list->SetComputeRootDescriptorTable(1, gpu1);
        list->Dispatch(1, 1, 1);

        D3D12_RESOURCE_BARRIER to_rtv = transition(backbuffers[idx], D3D12_RESOURCE_STATE_PRESENT,
                                                   D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->ResourceBarrier(1, &to_rtv);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {
            offset_cpu(rtv_heap->GetCPUDescriptorHandleForHeapStart(), rtv_inc, idx),
            offset_cpu(rtv_heap->GetCPUDescriptorHandleForHeapStart(), rtv_inc, 3),
        };
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
        const float clear0[4] = {0.01f, 0.04f, 0.08f, 1.0f};
        const float clear1[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        list->ClearRenderTargetView(rtvs[0], clear0, 0, nullptr);
        list->ClearRenderTargetView(rtvs[1], clear1, 0, nullptr);
        list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        list->OMSetRenderTargets(2, rtvs, FALSE, &dsv);
        D3D12_VIEWPORT vp = {0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
        D3D12_RECT scissor = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        list->RSSetViewports(1, &vp);
        list->RSSetScissorRects(1, &scissor);
        list->SetGraphicsRootSignature(root_sig);
        list->SetGraphicsRootDescriptorTable(0, gpu0);
        if (graphics_pso && frame < 16) {
            list->SetPipelineState(graphics_pso);
            list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
            list->IASetVertexBuffers(0, 1, &vbv);
            list->DrawInstanced(6, 1, 0, 0);
        }
        list->SetPipelineState(fallback_pso);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->IASetVertexBuffers(0, 1, &quad_vbv);
        list->DrawInstanced(kQuadVertexCount, 1, 0, 0);
        D3D12_RESOURCE_BARRIER to_present = transition(backbuffers[idx], D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                       D3D12_RESOURCE_STATE_PRESENT);
        list->ResourceBarrier(1, &to_present);
        list->Close();
        base = list;
        queue->ExecuteCommandLists(1, &base);
        queue->Signal(fence, ++fence_value);
        if (!wait_for_fence(fence, fence_value, fence_event)) {
            fail_json("FrameFenceWait", E_FAIL);
            return 2;
        }
        hr = swapchain->Present(1, 0);
        if (FAILED(hr)) {
            fail_json("Present", hr);
            return 2;
        }
        frame++;
        if ((frame % 120u) == 0u) {
            std::printf("{\"ok\":true,\"stage\":\"heartbeat\",\"frames\":%u,\"player_x\":%.3f,\"player_y\":%.3f}\n",
                        frame, player_x, player_y);
            std::fflush(stdout);
        }
        Sleep(16);
    }

    std::printf("{\"ok\":true,\"stage\":\"exited\",\"frames\":%u,\"reason\":\"%s\"}\n", frame, exit_reason);
    return 0;
}
