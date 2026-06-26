#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

static const GUID IID_D3D12DeviceFresh = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};

using D3D12CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using CreateDXGIFactory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
                                      UINT, UINT, ID3DBlob**, ID3DBlob**);
using SerializeRootSignatureFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION,
                                                  ID3DBlob**, ID3DBlob**);

struct ColorVertex {
    float position[3];
    float color[4];
};

template <typename T> static void safe_release(T*& object) {
    if (object) {
        object->Release();
        object = nullptr;
    }
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

static uint32_t getenv_u32(const char* key, uint32_t fallback) {
    std::string value = getenv_string(key);
    if (value.empty())
        return fallback;
    char* end = nullptr;
    unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed == 0 || parsed > 36000)
        return fallback;
    return static_cast<uint32_t>(parsed);
}

static std::string wine_path(std::string path) {
    if (!path.empty() && path[0] == '/')
        path = "Z:" + path;
    for (char& c : path) {
        if (c == '/')
            c = '\\';
    }
    return path;
}

static uint64_t fnv1a_update(uint64_t hash, const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static bool read_file_hash(const std::string& path, uint64_t& aggregate_hash, uint64_t& bytes, uint64_t& file_hash) {
    std::ifstream file(wine_path(path), std::ios::binary);
    if (!file)
        return false;
    file_hash = 1469598103934665603ull;
    std::array<uint8_t, 64 * 1024> buffer{};
    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        std::streamsize count = file.gcount();
        if (count > 0) {
            aggregate_hash = fnv1a_update(aggregate_hash, buffer.data(), static_cast<size_t>(count));
            file_hash = fnv1a_update(file_hash, buffer.data(), static_cast<size_t>(count));
            bytes += static_cast<uint64_t>(count);
        }
    }
    return true;
}

struct CorpusStats {
    std::string tsv_path;
    uint32_t rows = 0;
    uint32_t shaders_seen = 0;
    uint32_t textures_seen = 0;
    uint32_t shader_files_loaded = 0;
    uint32_t texture_files_loaded = 0;
    uint32_t failed_loads = 0;
    uint64_t bytes_loaded = 0;
    uint64_t fnv1a64 = 1469598103934665603ull;
    std::vector<uint64_t> shader_hashes;
    std::vector<uint64_t> texture_hashes;
};

static std::vector<std::string> split_tsv(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::stringstream ss(line);
    while (std::getline(ss, field, '\t'))
        fields.push_back(field);
    return fields;
}

static CorpusStats load_corpus_tsv(const std::string& path, uint32_t target_shaders, uint32_t target_textures) {
    CorpusStats stats;
    stats.tsv_path = path;
    if (path.empty())
        return stats;

    std::ifstream file(wine_path(path));
    std::string line;
    bool header = true;
    while (std::getline(file, line)) {
        if (header) {
            header = false;
            continue;
        }
        auto fields = split_tsv(line);
        if (fields.size() < 8)
            continue;
        const std::string& category = fields[2];
        const std::string& destination = fields[6];
        stats.rows++;
        if (category == "shader") {
            stats.shaders_seen++;
            if (stats.shader_files_loaded >= target_shaders)
                continue;
            uint64_t file_hash = 0;
            if (read_file_hash(destination, stats.fnv1a64, stats.bytes_loaded, file_hash)) {
                stats.shader_files_loaded++;
                stats.shader_hashes.push_back(file_hash);
            } else {
                stats.failed_loads++;
            }
        } else if (category == "texture") {
            stats.textures_seen++;
            if (stats.texture_files_loaded >= target_textures)
                continue;
            uint64_t file_hash = 0;
            if (read_file_hash(destination, stats.fnv1a64, stats.bytes_loaded, file_hash)) {
                stats.texture_files_loaded++;
                stats.texture_hashes.push_back(file_hash);
            } else {
                stats.failed_loads++;
            }
        }
        if (stats.shader_files_loaded >= target_shaders && stats.texture_files_loaded >= target_textures)
            break;
    }
    return stats;
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
    desc.Alignment = 0;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    return desc;
}

static D3D12_RESOURCE_DESC texture_desc(UINT width, UINT height, DXGI_FORMAT format) {
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
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
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

static LRESULT CALLBACK game_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
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

static void pump_messages() {
    MSG msg = {};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static bool wait_for_fence(ID3D12Fence* fence, UINT64 value, HANDLE event_handle) {
    if (fence->GetCompletedValue() >= value)
        return true;
    if (FAILED(fence->SetEventOnCompletion(value, event_handle)))
        return false;
    return WaitForSingleObject(event_handle, 5000) == WAIT_OBJECT_0;
}

static HRESULT compile_shader(D3DCompileFn compile, const char* source, const char* entry, const char* target,
                              ID3DBlob** blob) {
    ID3DBlob* errors = nullptr;
    HRESULT hr = compile ? compile(source, std::strlen(source), "m12_fresh_game_loading.hlsl", nullptr, nullptr, entry,
                                   target, 0, 0, blob, &errors)
                         : E_FAIL;
    if (errors)
        errors->Release();
    return hr;
}

static HRESULT serialize_root_signature(SerializeRootSignatureFn serialize, const D3D12_ROOT_SIGNATURE_DESC& desc,
                                        ID3DBlob** blob) {
    ID3DBlob* errors = nullptr;
    HRESULT hr = serialize ? serialize(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, blob, &errors) : E_FAIL;
    if (errors)
        errors->Release();
    return hr;
}

struct VisibleSceneStats {
    bool d3dcompiler_loaded = false;
    HRESULT compile_vs_hr = E_FAIL;
    HRESULT compile_ps_hr = E_FAIL;
    HRESULT serialize_root_hr = E_FAIL;
    HRESULT create_root_hr = E_FAIL;
    HRESULT create_pso_hr = E_FAIL;
    HRESULT create_vertex_buffer_hr = E_FAIL;
    uint32_t target_frames = 0;
    uint32_t draw_calls = 0;
    uint32_t quads_per_frame = 0;
    uint32_t vertices_per_frame = 0;
    bool pass = false;
};

struct VisibleSceneResources {
    ID3D12RootSignature* root_signature = nullptr;
    ID3D12PipelineState* pipeline_state = nullptr;
    ID3D12Resource* vertex_buffer = nullptr;
    ColorVertex* mapped_vertices = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertex_view = {};
    uint32_t max_quads = 96;
    VisibleSceneStats stats;
};

static const char* kVisibleLoadingHlsl = R"(
struct VSIn {
  float3 position : POSITION;
  float4 color : COLOR0;
};
struct PSIn {
  float4 position : SV_POSITION;
  float4 color : COLOR0;
};
PSIn VSMain(VSIn input) {
  PSIn output;
  output.position = float4(input.position, 1.0);
  output.color = input.color;
  return output;
}
float4 PSMain(PSIn input) : SV_TARGET {
  return input.color;
}
)";

static void write_quad(ColorVertex* dst, float x0, float y0, float x1, float y1, float z, float r, float g, float b,
                       float a) {
    const ColorVertex quad[6] = {
        {{x0, y0, z}, {r, g, b, a}}, {{x0, y1, z}, {r, g, b, a}}, {{x1, y0, z}, {r, g, b, a}},
        {{x1, y0, z}, {r, g, b, a}}, {{x0, y1, z}, {r, g, b, a}}, {{x1, y1, z}, {r, g, b, a}},
    };
    std::memcpy(dst, quad, sizeof(quad));
}

static uint32_t populate_visible_loading_vertices(VisibleSceneResources& scene, uint32_t frame,
                                                  const CorpusStats& corpus) {
    if (!scene.mapped_vertices)
        return 0;
    ColorVertex* out = scene.mapped_vertices;
    uint32_t quads = 0;
    const auto push = [&](float x0, float y0, float x1, float y1, float z, float r, float g, float b, float a) {
        if (quads >= scene.max_quads)
            return;
        write_quad(out + quads * 6, x0, y0, x1, y1, z, r, g, b, a);
        quads++;
    };

    const float progress = static_cast<float>((frame % 240u) + 1u) / 240.0f;
    const uint64_t tint = corpus.fnv1a64;
    const float tint_r = 0.20f + static_cast<float>((tint >> 0) & 0xffu) / 510.0f;
    const float tint_g = 0.20f + static_cast<float>((tint >> 16) & 0xffu) / 510.0f;
    const float tint_b = 0.35f + static_cast<float>((tint >> 32) & 0xffu) / 420.0f;

    push(-0.94f, -0.90f, 0.94f, 0.90f, 0.50f, 0.015f, 0.025f, 0.055f, 1.0f);
    push(-0.88f, 0.62f, 0.88f, 0.76f, 0.45f, tint_r, tint_g, tint_b, 1.0f);
    push(-0.86f, -0.76f, 0.86f, -0.66f, 0.35f, 0.05f, 0.08f, 0.12f, 1.0f);
    push(-0.84f, -0.74f, -0.84f + 1.68f * progress, -0.68f, 0.25f, 0.20f, 0.80f, 1.0f, 1.0f);

    const float center_x = -0.72f;
    const float center_y = -0.42f;
    const float cell_w = 0.10f;
    const float cell_h = 0.10f;
    uint32_t tile_index = 0;
    for (uint32_t y = 0; y < 5; ++y) {
        for (uint32_t x = 0; x < 12; ++x) {
            uint64_t seed = corpus.texture_hashes.empty()
                                ? (corpus.fnv1a64 + tile_index * 0x9e3779b97f4a7c15ull)
                                : corpus.texture_hashes[tile_index % corpus.texture_hashes.size()];
            const bool hot = ((tile_index + frame / 4u) % 17u) == 0u;
            const float r = (static_cast<float>((seed >> 8) & 0xffu) / 255.0f) * (hot ? 1.0f : 0.55f);
            const float g = (static_cast<float>((seed >> 24) & 0xffu) / 255.0f) * (hot ? 1.0f : 0.55f);
            const float b = (static_cast<float>((seed >> 40) & 0xffu) / 255.0f) * (hot ? 1.0f : 0.70f);
            const float x0 = center_x + x * cell_w;
            const float y0 = center_y + y * cell_h;
            push(x0, y0, x0 + cell_w * 0.72f, y0 + cell_h * 0.72f, 0.18f, 0.15f + r, 0.15f + g, 0.20f + b, 1.0f);
            tile_index++;
        }
    }

    const float pulse = static_cast<float>((frame % 90u) < 45u ? (frame % 45u) : (90u - (frame % 90u))) / 45.0f;
    push(-0.16f, 0.18f, 0.16f, 0.50f, 0.12f, 0.90f, 0.96f, 1.0f, 1.0f);
    push(-0.28f, 0.05f, -0.04f, 0.29f, 0.10f, 0.20f + pulse, 0.50f, 1.0f, 1.0f);
    push(0.04f, 0.05f, 0.28f, 0.29f, 0.10f, 1.0f, 0.45f + pulse * 0.35f, 0.25f, 1.0f);

    scene.stats.quads_per_frame = quads;
    scene.stats.vertices_per_frame = quads * 6;
    return quads * 6;
}

static VisibleSceneResources create_visible_scene(ID3D12Device* device, D3DCompileFn compile,
                                                  SerializeRootSignatureFn serialize, uint32_t target_frames) {
    VisibleSceneResources scene;
    scene.stats.target_frames = target_frames;
    scene.stats.d3dcompiler_loaded = compile != nullptr;
    ID3DBlob* vs = nullptr;
    ID3DBlob* ps = nullptr;
    ID3DBlob* root_blob = nullptr;
    scene.stats.compile_vs_hr = compile_shader(compile, kVisibleLoadingHlsl, "VSMain", "vs_5_0", &vs);
    scene.stats.compile_ps_hr = compile_shader(compile, kVisibleLoadingHlsl, "PSMain", "ps_5_0", &ps);

    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    scene.stats.serialize_root_hr = serialize_root_signature(serialize, root_desc, &root_blob);
    if (device && root_blob) {
        scene.stats.create_root_hr = device->CreateRootSignature(
            0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(), IID_PPV_ARGS(&scene.root_signature));
    }

    if (device && scene.root_signature && vs && ps) {
        D3D12_INPUT_ELEMENT_DESC input_elements[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
        pso_desc.pRootSignature = scene.root_signature;
        pso_desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pso_desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pso_desc.InputLayout = {input_elements, 2};
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.NumRenderTargets = 1;
        pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso_desc.SampleDesc.Count = 1;
        pso_desc.SampleMask = UINT_MAX;
        pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso_desc.RasterizerState.DepthClipEnable = TRUE;
        pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso_desc.DepthStencilState.DepthEnable = FALSE;
        pso_desc.DepthStencilState.StencilEnable = FALSE;
        scene.stats.create_pso_hr = device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&scene.pipeline_state));
    }

    if (device) {
        D3D12_HEAP_PROPERTIES upload_heap = heap_props(D3D12_HEAP_TYPE_UPLOAD);
        const UINT64 vb_bytes = static_cast<UINT64>(scene.max_quads) * 6u * sizeof(ColorVertex);
        D3D12_RESOURCE_DESC vb_desc = buffer_desc(vb_bytes);
        scene.stats.create_vertex_buffer_hr = device->CreateCommittedResource(
            &upload_heap, D3D12_HEAP_FLAG_NONE, &vb_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&scene.vertex_buffer));
        if (SUCCEEDED(scene.stats.create_vertex_buffer_hr) && scene.vertex_buffer) {
            scene.vertex_buffer->Map(0, nullptr, reinterpret_cast<void**>(&scene.mapped_vertices));
            scene.vertex_view.BufferLocation = scene.vertex_buffer->GetGPUVirtualAddress();
            scene.vertex_view.SizeInBytes = static_cast<UINT>(vb_bytes);
            scene.vertex_view.StrideInBytes = sizeof(ColorVertex);
        }
    }

    if (root_blob)
        root_blob->Release();
    if (vs)
        vs->Release();
    if (ps)
        ps->Release();
    scene.stats.pass = scene.stats.d3dcompiler_loaded && SUCCEEDED(scene.stats.compile_vs_hr) &&
                       SUCCEEDED(scene.stats.compile_ps_hr) && SUCCEEDED(scene.stats.serialize_root_hr) &&
                       SUCCEEDED(scene.stats.create_root_hr) && SUCCEEDED(scene.stats.create_pso_hr) &&
                       SUCCEEDED(scene.stats.create_vertex_buffer_hr) && scene.mapped_vertices != nullptr;
    return scene;
}

static void destroy_visible_scene(VisibleSceneResources& scene) {
    if (scene.vertex_buffer && scene.mapped_vertices) {
        scene.vertex_buffer->Unmap(0, nullptr);
        scene.mapped_vertices = nullptr;
    }
    safe_release(scene.vertex_buffer);
    safe_release(scene.pipeline_state);
    safe_release(scene.root_signature);
}

struct GpuTextureStats {
    HRESULT create_descriptor_heap_hr = E_FAIL;
    HRESULT close_hr = E_FAIL;
    HRESULT signal_hr = E_FAIL;
    uint32_t textures_requested = 0;
    uint32_t textures_created = 0;
    uint32_t upload_buffers_created = 0;
    uint32_t srv_descriptors_created = 0;
    uint32_t copy_texture_region_commands = 0;
    uint32_t transition_barriers = 0;
    uint64_t upload_bytes = 0;
    bool fence_wait_ok = false;
    bool pass = false;
};

static void fill_texture_upload(ID3D12Resource* upload, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint,
                                uint64_t seed, UINT width, UINT height) {
    uint8_t* mapped = nullptr;
    if (FAILED(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped))) || !mapped)
        return;
    const size_t row_pitch = static_cast<size_t>(footprint.Footprint.RowPitch);
    uint8_t* base = mapped + static_cast<size_t>(footprint.Offset);
    for (UINT y = 0; y < height; ++y) {
        uint8_t* row = base + static_cast<size_t>(y) * row_pitch;
        for (UINT x = 0; x < width; ++x) {
            const uint64_t mixed = seed ^ (static_cast<uint64_t>(x) * 0x9e3779b97f4a7c15ull) ^
                                   (static_cast<uint64_t>(y) * 0xbf58476d1ce4e5b9ull);
            row[x * 4 + 0] = static_cast<uint8_t>(mixed & 0xffu);
            row[x * 4 + 1] = static_cast<uint8_t>((mixed >> 17) & 0xffu);
            row[x * 4 + 2] = static_cast<uint8_t>((mixed >> 41) & 0xffu);
            row[x * 4 + 3] = 0xffu;
        }
    }
    upload->Unmap(0, nullptr);
}

static GpuTextureStats exercise_corpus_gpu_textures(ID3D12Device* device, ID3D12CommandQueue* queue,
                                                    ID3D12CommandAllocator* allocator, ID3D12GraphicsCommandList* list,
                                                    ID3D12Fence* fence, HANDLE fence_event, UINT64& fence_value,
                                                    const std::vector<uint64_t>& texture_hashes) {
    GpuTextureStats stats;
    if (!device || !queue || !allocator || !list || !fence || !fence_event)
        return stats;

    constexpr UINT texture_width = 16;
    constexpr UINT texture_height = 16;
    constexpr uint32_t max_textures = 300;
    const uint32_t texture_count = std::min<uint32_t>(max_textures, static_cast<uint32_t>(texture_hashes.size()));
    stats.textures_requested = texture_count;
    if (!texture_count)
        return stats;

    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = texture_count;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap* srv_heap = nullptr;
    stats.create_descriptor_heap_hr = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&srv_heap));
    if (FAILED(stats.create_descriptor_heap_hr))
        return stats;

    const UINT descriptor_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu = srv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_RESOURCE_DESC tex_desc = texture_desc(texture_width, texture_height, DXGI_FORMAT_R8G8B8A8_UNORM);
    D3D12_HEAP_PROPERTIES default_heap = heap_props(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_HEAP_PROPERTIES upload_heap = heap_props(D3D12_HEAP_TYPE_UPLOAD);

    std::vector<ID3D12Resource*> textures(texture_count, nullptr);
    std::vector<ID3D12Resource*> uploads(texture_count, nullptr);
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(texture_count);

    bool creation_ok = true;
    for (uint32_t i = 0; i < texture_count; ++i) {
        HRESULT hr =
            device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &tex_desc,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&textures[i]));
        if (FAILED(hr)) {
            creation_ok = false;
            break;
        }
        stats.textures_created++;

        UINT rows = 0;
        UINT64 row_bytes = 0;
        UINT64 upload_bytes = 0;
        device->GetCopyableFootprints(&tex_desc, 0, 1, 0, &footprints[i], &rows, &row_bytes, &upload_bytes);
        D3D12_RESOURCE_DESC upload_desc = buffer_desc(upload_bytes);
        hr = device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploads[i]));
        if (FAILED(hr)) {
            creation_ok = false;
            break;
        }
        stats.upload_buffers_created++;
        stats.upload_bytes += upload_bytes;
        fill_texture_upload(uploads[i], footprints[i], texture_hashes[i], texture_width, texture_height);

        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(textures[i], &srv_desc, offset_cpu(srv_cpu, descriptor_increment, i));
        stats.srv_descriptors_created++;
    }

    if (creation_ok && SUCCEEDED(allocator->Reset()) && SUCCEEDED(list->Reset(allocator, nullptr))) {
        for (uint32_t i = 0; i < texture_count; ++i) {
            D3D12_TEXTURE_COPY_LOCATION dst = {};
            dst.pResource = textures[i];
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = 0;
            D3D12_TEXTURE_COPY_LOCATION src = {};
            src.pResource = uploads[i];
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = footprints[i];
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            stats.copy_texture_region_commands++;
            D3D12_RESOURCE_BARRIER barrier = transition_barrier(textures[i], D3D12_RESOURCE_STATE_COPY_DEST,
                                                                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            list->ResourceBarrier(1, &barrier);
            stats.transition_barriers++;
        }
        stats.close_hr = list->Close();
        if (SUCCEEDED(stats.close_hr)) {
            ID3D12CommandList* base = list;
            queue->ExecuteCommandLists(1, &base);
            fence_value++;
            stats.signal_hr = queue->Signal(fence, fence_value);
            stats.fence_wait_ok = SUCCEEDED(stats.signal_hr) && wait_for_fence(fence, fence_value, fence_event);
        }
    }

    stats.pass = creation_ok && stats.textures_created == texture_count &&
                 stats.upload_buffers_created == texture_count && stats.srv_descriptors_created == texture_count &&
                 stats.copy_texture_region_commands == texture_count && stats.transition_barriers == texture_count &&
                 SUCCEEDED(stats.close_hr) && SUCCEEDED(stats.signal_hr) && stats.fence_wait_ok;

    for (auto*& upload : uploads)
        safe_release(upload);
    for (auto*& texture : textures)
        safe_release(texture);
    safe_release(srv_heap);
    return stats;
}

struct D3DRunStats {
    HRESULT create_factory_hr = E_FAIL;
    HRESULT create_device_hr = E_FAIL;
    HRESULT create_queue_hr = E_FAIL;
    HRESULT create_swapchain_hr = E_FAIL;
    HRESULT create_rtv_heap_hr = E_FAIL;
    HRESULT create_allocator_hr = E_FAIL;
    HRESULT create_list_hr = E_FAIL;
    HRESULT create_fence_hr = E_FAIL;
    HRESULT present_hr = E_FAIL;
    GpuTextureStats gpu_textures;
    VisibleSceneStats visible_scene;
    uint32_t frames_presented = 0;
    bool hwnd_created = false;
    bool pass = false;
};

static D3DRunStats run_d3d_window(const CorpusStats& corpus) {
    D3DRunStats stats;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* class_name = L"MetalSharpM12FreshGameWindow";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = game_window_proc;
    wc.hInstance = instance;
    wc.lpszClassName = class_name;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, class_name, L"MetalSharp M12 Fresh Proof Game", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 960, 540, nullptr, nullptr, instance, nullptr);
    stats.hwnd_created = hwnd != nullptr;
    if (!hwnd)
        return stats;
    ShowWindow(hwnd, SW_SHOW);
    pump_messages();

    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    HMODULE d3dcompiler = LoadLibraryA("d3dcompiler_47.dll");
    auto create_device = reinterpret_cast<D3D12CreateDeviceFn>(
        reinterpret_cast<void*>(d3d12 ? GetProcAddress(d3d12, "D3D12CreateDevice") : nullptr));
    auto create_factory2 = reinterpret_cast<CreateDXGIFactory2Fn>(
        reinterpret_cast<void*>(dxgi ? GetProcAddress(dxgi, "CreateDXGIFactory2") : nullptr));
    auto compile = reinterpret_cast<D3DCompileFn>(
        reinterpret_cast<void*>(d3dcompiler ? GetProcAddress(d3dcompiler, "D3DCompile") : nullptr));
    auto serialize = reinterpret_cast<SerializeRootSignatureFn>(
        reinterpret_cast<void*>(d3d12 ? GetProcAddress(d3d12, "D3D12SerializeRootSignature") : nullptr));

    IDXGIFactory4* factory = nullptr;
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGISwapChain1* swapchain1 = nullptr;
    IDXGISwapChain3* swapchain = nullptr;
    ID3D12DescriptorHeap* rtv_heap = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    HANDLE fence_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    UINT64 fence_value = 0;

    stats.create_factory_hr = create_factory2 ? create_factory2(0, IID_PPV_ARGS(&factory)) : E_FAIL;
    stats.create_device_hr = create_device ? create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_D3D12DeviceFresh,
                                                           reinterpret_cast<void**>(&device))
                                           : E_FAIL;

    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    stats.create_queue_hr = device ? device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)) : E_FAIL;

    DXGI_SWAP_CHAIN_DESC1 swap_desc = {};
    swap_desc.Width = 960;
    swap_desc.Height = 540;
    swap_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_desc.SampleDesc.Count = 1;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.BufferCount = 2;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    stats.create_swapchain_hr =
        (factory && queue) ? factory->CreateSwapChainForHwnd(queue, hwnd, &swap_desc, nullptr, nullptr, &swapchain1)
                           : E_FAIL;
    if (SUCCEEDED(stats.create_swapchain_hr) && swapchain1)
        swapchain1->QueryInterface(IID_PPV_ARGS(&swapchain));

    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {};
    rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.NumDescriptors = 2;
    stats.create_rtv_heap_hr = device ? device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap)) : E_FAIL;
    UINT rtv_increment = device ? device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) : 0;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_start =
        rtv_heap ? rtv_heap->GetCPUDescriptorHandleForHeapStart() : D3D12_CPU_DESCRIPTOR_HANDLE{};

    std::array<ID3D12Resource*, 2> buffers = {nullptr, nullptr};
    if (swapchain && rtv_heap) {
        for (UINT i = 0; i < 2; ++i) {
            if (SUCCEEDED(swapchain->GetBuffer(i, IID_PPV_ARGS(&buffers[i]))))
                device->CreateRenderTargetView(buffers[i], nullptr, offset_cpu(rtv_start, rtv_increment, i));
        }
    }

    stats.create_allocator_hr =
        device ? device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)) : E_FAIL;
    stats.create_list_hr =
        device ? device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list))
               : E_FAIL;
    if (SUCCEEDED(stats.create_list_hr) && list)
        list->Close();
    stats.create_fence_hr = device ? device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)) : E_FAIL;

    const uint32_t visible_frame_target = getenv_u32("M12_FRESH_VISIBLE_FRAMES", 300);
    VisibleSceneResources visible_scene = create_visible_scene(device, compile, serialize, visible_frame_target);
    stats.visible_scene = visible_scene.stats;

    stats.gpu_textures = exercise_corpus_gpu_textures(device, queue, allocator, list, fence, fence_event, fence_value,
                                                      corpus.texture_hashes);

    const float colors[3][4] = {{0.01f, 0.02f, 0.05f, 1.0f}, {0.01f, 0.02f, 0.05f, 1.0f}, {0.01f, 0.02f, 0.05f, 1.0f}};
    if (swapchain && allocator && list && queue && fence && fence_event && visible_scene.stats.pass) {
        for (UINT frame = 0; frame < visible_frame_target; ++frame) {
            pump_messages();
            UINT index = swapchain->GetCurrentBackBufferIndex();
            ID3D12Resource* buffer = buffers[index];
            if (!buffer)
                break;
            if (FAILED(allocator->Reset()) || FAILED(list->Reset(allocator, nullptr)))
                break;
            auto to_rtv = transition_barrier(buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
            list->ResourceBarrier(1, &to_rtv);
            auto rtv = offset_cpu(rtv_start, rtv_increment, index);
            list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            list->ClearRenderTargetView(rtv, colors[frame % 3], 0, nullptr);
            D3D12_VIEWPORT viewport = {0.0f, 0.0f, 960.0f, 540.0f, 0.0f, 1.0f};
            D3D12_RECT scissor = {0, 0, 960, 540};
            const uint32_t visible_vertices = populate_visible_loading_vertices(visible_scene, frame, corpus);
            list->RSSetViewports(1, &viewport);
            list->RSSetScissorRects(1, &scissor);
            list->SetGraphicsRootSignature(visible_scene.root_signature);
            list->SetPipelineState(visible_scene.pipeline_state);
            list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            list->IASetVertexBuffers(0, 1, &visible_scene.vertex_view);
            list->DrawInstanced(visible_vertices, 1, 0, 0);
            stats.visible_scene.draw_calls++;
            auto to_present =
                transition_barrier(buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
            list->ResourceBarrier(1, &to_present);
            if (FAILED(list->Close()))
                break;
            ID3D12CommandList* base = list;
            queue->ExecuteCommandLists(1, &base);
            fence_value++;
            if (FAILED(queue->Signal(fence, fence_value)) || !wait_for_fence(fence, fence_value, fence_event))
                break;
            stats.present_hr = swapchain->Present(0, 0);
            if (FAILED(stats.present_hr))
                break;
            stats.frames_presented++;
            Sleep(16);
        }
    }

    stats.visible_scene.quads_per_frame = visible_scene.stats.quads_per_frame;
    stats.visible_scene.vertices_per_frame = visible_scene.stats.vertices_per_frame;

    stats.pass = stats.hwnd_created && SUCCEEDED(stats.create_factory_hr) && SUCCEEDED(stats.create_device_hr) &&
                 SUCCEEDED(stats.create_queue_hr) && SUCCEEDED(stats.create_swapchain_hr) &&
                 SUCCEEDED(stats.create_rtv_heap_hr) && SUCCEEDED(stats.create_allocator_hr) &&
                 SUCCEEDED(stats.create_list_hr) && SUCCEEDED(stats.create_fence_hr) && stats.gpu_textures.pass &&
                 stats.visible_scene.pass && stats.visible_scene.draw_calls == visible_frame_target &&
                 SUCCEEDED(stats.present_hr) && stats.frames_presented == visible_frame_target;

    for (auto*& buffer : buffers)
        safe_release(buffer);
    safe_release(fence);
    safe_release(list);
    safe_release(allocator);
    safe_release(rtv_heap);
    safe_release(swapchain);
    safe_release(swapchain1);
    safe_release(queue);
    destroy_visible_scene(visible_scene);
    safe_release(device);
    safe_release(factory);
    if (fence_event)
        CloseHandle(fence_event);
    DestroyWindow(hwnd);
    pump_messages();
    return stats;
}

static std::string hr_hex(HRESULT hr) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08lx", static_cast<unsigned long>(static_cast<uint32_t>(hr)));
    return buffer;
}

int main() {
    std::string corpus_tsv = getenv_string("M12_FRESH_CORPUS_TSV");
    uint32_t target_shaders = 300;
    uint32_t target_textures = 300;
    CorpusStats corpus = load_corpus_tsv(corpus_tsv, target_shaders, target_textures);
    D3DRunStats d3d = run_d3d_window(corpus);
    bool corpus_ok = corpus.shader_files_loaded >= target_shaders && corpus.texture_files_loaded >= target_textures &&
                     corpus.failed_loads == 0;
    bool pass = corpus_ok && d3d.pass;

    std::printf("{\n");
    std::printf("  \"schema\": \"metalsharp.m12.fresh.game.v1\",\n");
    std::printf("  \"pass\": %s,\n", pass ? "true" : "false");
    std::printf("  \"corpus\": {\n");
    std::printf("    \"tsv\": \"%s\",\n", json_escape(corpus.tsv_path).c_str());
    std::printf("    \"rows_seen\": %u,\n", corpus.rows);
    std::printf("    \"shaders_seen\": %u,\n", corpus.shaders_seen);
    std::printf("    \"textures_seen\": %u,\n", corpus.textures_seen);
    std::printf("    \"shader_files_loaded\": %u,\n", corpus.shader_files_loaded);
    std::printf("    \"texture_files_loaded\": %u,\n", corpus.texture_files_loaded);
    std::printf("    \"target_shaders\": %u,\n", target_shaders);
    std::printf("    \"target_textures\": %u,\n", target_textures);
    std::printf("    \"failed_loads\": %u,\n", corpus.failed_loads);
    std::printf("    \"bytes_loaded\": %llu,\n", static_cast<unsigned long long>(corpus.bytes_loaded));
    std::printf("    \"fnv1a64\": \"%016llx\",\n", static_cast<unsigned long long>(corpus.fnv1a64));
    std::printf("    \"ok\": %s\n", corpus_ok ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"d3d12_window\": {\n");
    std::printf("    \"hwnd_created\": %s,\n", d3d.hwnd_created ? "true" : "false");
    std::printf("    \"CreateDXGIFactory2\": \"%s\",\n", hr_hex(d3d.create_factory_hr).c_str());
    std::printf("    \"D3D12CreateDevice\": \"%s\",\n", hr_hex(d3d.create_device_hr).c_str());
    std::printf("    \"CreateCommandQueue\": \"%s\",\n", hr_hex(d3d.create_queue_hr).c_str());
    std::printf("    \"CreateSwapChainForHwnd\": \"%s\",\n", hr_hex(d3d.create_swapchain_hr).c_str());
    std::printf("    \"CreateDescriptorHeap_RTV\": \"%s\",\n", hr_hex(d3d.create_rtv_heap_hr).c_str());
    std::printf("    \"CreateCommandAllocator\": \"%s\",\n", hr_hex(d3d.create_allocator_hr).c_str());
    std::printf("    \"CreateCommandList\": \"%s\",\n", hr_hex(d3d.create_list_hr).c_str());
    std::printf("    \"CreateFence\": \"%s\",\n", hr_hex(d3d.create_fence_hr).c_str());
    std::printf("    \"gpu_textures\": {\n");
    std::printf("      \"CreateDescriptorHeap_CBV_SRV_UAV\": \"%s\",\n",
                hr_hex(d3d.gpu_textures.create_descriptor_heap_hr).c_str());
    std::printf("      \"CloseCommandList\": \"%s\",\n", hr_hex(d3d.gpu_textures.close_hr).c_str());
    std::printf("      \"SignalFence\": \"%s\",\n", hr_hex(d3d.gpu_textures.signal_hr).c_str());
    std::printf("      \"textures_requested\": %u,\n", d3d.gpu_textures.textures_requested);
    std::printf("      \"textures_created\": %u,\n", d3d.gpu_textures.textures_created);
    std::printf("      \"upload_buffers_created\": %u,\n", d3d.gpu_textures.upload_buffers_created);
    std::printf("      \"srv_descriptors_created\": %u,\n", d3d.gpu_textures.srv_descriptors_created);
    std::printf("      \"copy_texture_region_commands\": %u,\n", d3d.gpu_textures.copy_texture_region_commands);
    std::printf("      \"transition_barriers\": %u,\n", d3d.gpu_textures.transition_barriers);
    std::printf("      \"upload_bytes\": %llu,\n", static_cast<unsigned long long>(d3d.gpu_textures.upload_bytes));
    std::printf("      \"fence_wait_ok\": %s,\n", d3d.gpu_textures.fence_wait_ok ? "true" : "false");
    std::printf("      \"ok\": %s\n", d3d.gpu_textures.pass ? "true" : "false");
    std::printf("    },\n");
    std::printf("    \"visible_scene\": {\n");
    std::printf("      \"D3DCompile_loaded\": %s,\n", d3d.visible_scene.d3dcompiler_loaded ? "true" : "false");
    std::printf("      \"VSMain_vs_5_0\": \"%s\",\n", hr_hex(d3d.visible_scene.compile_vs_hr).c_str());
    std::printf("      \"PSMain_ps_5_0\": \"%s\",\n", hr_hex(d3d.visible_scene.compile_ps_hr).c_str());
    std::printf("      \"D3D12SerializeRootSignature\": \"%s\",\n",
                hr_hex(d3d.visible_scene.serialize_root_hr).c_str());
    std::printf("      \"CreateRootSignature\": \"%s\",\n", hr_hex(d3d.visible_scene.create_root_hr).c_str());
    std::printf("      \"CreateGraphicsPipelineState\": \"%s\",\n", hr_hex(d3d.visible_scene.create_pso_hr).c_str());
    std::printf("      \"CreateVisibleVertexBuffer\": \"%s\",\n",
                hr_hex(d3d.visible_scene.create_vertex_buffer_hr).c_str());
    std::printf("      \"target_frames\": %u,\n", d3d.visible_scene.target_frames);
    std::printf("      \"draw_calls\": %u,\n", d3d.visible_scene.draw_calls);
    std::printf("      \"quads_per_frame\": %u,\n", d3d.visible_scene.quads_per_frame);
    std::printf("      \"vertices_per_frame\": %u,\n", d3d.visible_scene.vertices_per_frame);
    std::printf("      \"ok\": %s\n", d3d.visible_scene.pass ? "true" : "false");
    std::printf("    },\n");
    std::printf("    \"Present\": \"%s\",\n", hr_hex(d3d.present_hr).c_str());
    std::printf("    \"frames_presented\": %u,\n", d3d.frames_presented);
    std::printf("    \"ok\": %s\n", d3d.pass ? "true" : "false");
    std::printf("  },\n");
    std::printf("  \"process_exit_mode\": \"TerminateProcess_after_explicit_d3d_release_lifecycle_gate_pending\"\n");
    std::printf("}\n");
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), pass ? 0u : 1u);
    return pass ? 0 : 1;
}
