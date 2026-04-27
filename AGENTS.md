# AGENTS.md — MetalSharp

## Project

MetalSharp is a Direct3D-to-Metal translation layer that runs Windows games natively on macOS using Apple's Metal GPU framework. No VM, no Vulkan middleman. Single-hop D3D→Metal.

## Architecture

```
Windows Game (.exe)
        │
        ▼
   Wine (Win32 API translation, PE loader)
        │
        ▼
   MetalSharp DLL shims (d3d11.dll, d3d12.dll, dxgi.dll)
        │
        ▼
   MetalSharp Core (D3D→Metal API translation)
        │
        ├── Device (MTLDevice wrapper)
        ├── Command (MTLCommandQueue/Buffer wrappers)
        ├── Pipeline (MTLRenderPipelineState, MTLComputePipelineState)
        ├── Shader (HLSL/DXIL→MSL translation)
        ├── Resources (Buffer, Texture, Sampler → MTLBuffer, MTLTexture, MTLSamplerState)
        └── Framebuffer (MTLRenderPassDescriptor wrappers)
        │
        ▼
   Apple Metal Framework → Apple Silicon GPU
```

## Translation Chain (vs Current Stack)

```
Current:    Game → D3D → DXVK → Vulkan → MoltenVK → Metal → GPU  (4 hops)
MetalSharp: Game → D3D → metalsharp → Metal → GPU                (2 hops)
```

## Directory Layout

```
src/
├── d3d/
│   ├── d3d11/          D3D11 device, context, resources, state objects
│   └── d3d12/          D3D12 device, command queue, lists, heaps
├── dxgi/               DXGI factory, adapter, swap chain
├── metal/
│   ├── device/         MTLDevice wrapper
│   ├── command/        MTLCommandQueue, MTLCommandBuffer wrappers
│   ├── pipeline/       Pipeline state creation & management
│   └── shader/         HLSL→MSL shader translation
├── runtime/            Wine prefix management, DLL injection
├── audio/              XAudio2→CoreAudio bridge
└── input/              XInput→GameController bridge
include/
├── metalsharp/         MetalSharp internal headers
├── d3d/                D3D interface definitions
└── dxgi/               DXGI interface definitions
tools/
└── launcher/           metalsharp_launcher CLI
tests/                  Test suite
```

## Build & Run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build build
./build/metalsharp_launcher --prefix ~/.metalsharp/prefix game.exe
```

## Build Targets

| Target | Output | Purpose |
|--------|--------|---------|
| `metalsharp_core` | static lib | Metal backend, shared infrastructure |
| `metalsharp_d3d11` | `d3d11.dll` | D3D11 API shim |
| `metalsharp_d3d12` | `d3d12.dll` | D3D12 API shim |
| `metalsharp_dxgi` | `dxgi.dll` | DXGI swap chain & adapter |
| `metalsharp_audio` | `xaudio2_9.dll` | XAudio2→CoreAudio |
| `metalsharp_input` | `xinput1_4.dll` | XInput→GameController |
| `metalsharp_launcher` | executable | Game launcher & Wine prefix manager |

## Languages & Frameworks

- C++20, Objective-C++, Objective-C (Metal is ObjC API)
- CMake 3.24+
- Apple Metal framework
- Wine headers (for Win32 types and PE loading)
- No Vulkan dependency — that's the entire point

## Conventions

- `.cpp` files for portable C++ logic
- `.mm` files for Objective-C++ Metal API calls only
- D3D shim classes inherit from the D3D COM interface, delegate to Metal backend
- COM-style reference counting (AddRef/Release/QueryInterface)
- All Metal objects wrapped in C++ RAII classes with `.mm` implementation files
- No exceptions — use return codes matching D3D HRESULT patterns

## Development Phases

### Phase 1 — Triangle (complete)
- [x] Metal device initialization
- [x] Basic D3D11 device + immediate context
- [x] DXGI swap chain → CAMetalLayer
- [x] Vertex buffer → MTLBuffer
- [x] Simple vertex/pixel shader → MSL
- [x] Render a triangle from a D3D11 codepath

### Phase 2 — D3D11 Coverage (complete)
- [x] Full D3D11 resource types (textures, UAVs)
- [x] State objects (blend, rasterizer, depth stencil, sampler)
- [x] Input layouts → MTLVertexDescriptor
- [x] Shader resource views → MTLTexture bindings
- [x] Multiple render targets
- [x] Map/Unmap for CPU-accessible resources

### Phase 3 — D3D12 (complete)
- [x] Command queue → MTLCommandQueue
- [x] Command lists → MTLCommandBuffer + encoders
- [x] Descriptor heaps → argument buffers
- [x] Root signatures → Metal argument table mapping
- [x] Pipeline state objects

### Phase 4 — Runtime & Integration (complete)
- [x] Wine prefix bootstrap
- [x] DLL injection / override configuration
- [x] Audio bridge (XAudio2 → CoreAudio)
- [x] Input bridge (XInput → GameController)
- [x] Window management (HWND → NSWindow/CAMetalLayer)
- [x] DXGI output enumeration (real display modes)
- [x] Diagnostic logging
- [x] Configuration system (TOML-like, per-game profiles)
- [x] SteamCMD integration
- [x] Game launcher CLI
- [x] Game compatibility testing

### Phase 5 — Performance (complete)
- [x] Shader caching (DXBC→MSL compilation cache, persisted to disk)
- [x] Pipeline state cache (descriptor hash, LRU eviction)
- [x] Command buffer batching
- [x] Memory allocation pooling (MTLBuffer recycling)
- [x] MetalFX upscaling integration
- [x] Frame pacing

## Key References

- Apple Metal API: https://developer.apple.com/documentation/metal
- Apple Game Porting Toolkit: https://developer.apple.com/games/game-porting-toolkit/
- DXVK (D3D→Vulkan reference): https://github.com/doitsujin/dxvk
- Wine: https://gitlab.winehq.org/wine/wine
- MoltenVK (Vulkan→Metal reference): https://github.com/KhronosGroup/MoltenVK
- D3D11 API: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/
- D3D12 API: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/
