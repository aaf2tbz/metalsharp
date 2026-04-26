# MetalSharp

Direct3D → Metal translation layer. Run Windows games natively on macOS with Apple Silicon.

**No VM. No Vulkan middleman. Single-hop D3D → Metal.**

## Why

The current stack for running D3D games on macOS chains four translation layers:

```
Game → D3D → DXVK → Vulkan → MoltenVK → Metal → GPU
```

MetalSharp cuts it to two:

```
Game → D3D → MetalSharp → Metal → GPU
```

Fewer hops means less overhead, lower latency, and fewer bugs from translation mismatches.

## Status

**Phase 1 complete** — a colored triangle renders through D3D11 codepaths calling Metal directly.

| What | Status |
|------|--------|
| Metal device init (MTLDevice + MTLCommandQueue) | Done |
| D3D11 device + immediate context (COM interfaces) | Done |
| DXGI swap chain → CAMetalLayer | Done |
| Vertex buffer → MTLBuffer allocation | Done |
| DXGI format → MTLPixelFormat translation | Done |
| MSL shader compilation via newLibraryWithSource | Done |
| Render pipeline state creation | Done |
| Draw call encoding → Metal command buffer | Done |
| Triangle test (end-to-end) | Passing |

See [ROADMAP.md](ROADMAP.md) for the full development plan.

## Architecture

```
Windows Game (.exe)
        │
        ▼
   Wine (Win32 API translation)
        │
        ▼
   MetalSharp DLL shims (d3d11, d3d12, dxgi)
        │
        ▼
   MetalSharp Core
        ├── Device      → MTLDevice
        ├── Command     → MTLCommandQueue / MTLCommandBuffer
        ├── Pipeline    → MTLRenderPipelineState
        ├── Shader      → DXBC/DXIL → MSL translation
        ├── Resources   → MTLBuffer, MTLTexture, MTLSamplerState
        └── Framebuffer → MTLRenderPassDescriptor
        │
        ▼
   Apple Metal → Apple Silicon GPU
```

## Build

Requires macOS with Xcode command line tools and CMake 3.24+.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build build
```

### Run tests

```bash
cd build && ctest --output-on-failure
```

### Build targets

| Target | Output | Purpose |
|--------|--------|---------|
| `metalsharp_core` | static lib | Metal backend, shared infrastructure |
| `metalsharp_d3d11` | `d3d11.dylib` | D3D11 API shim |
| `metalsharp_d3d12` | `d3d12.dylib` | D3D12 API shim (stub) |
| `metalsharp_dxgi` | `dxgi.dylib` | DXGI swap chain & adapter |
| `metalsharp_audio` | `xaudio2_9.dylib` | XAudio2 → CoreAudio (stub) |
| `metalsharp_input` | `xinput1_4.dylib` | XInput → GameController (stub) |
| `metalsharp_launcher` | executable | Game launcher & Wine prefix manager |

## Project structure

```
src/
├── d3d/d3d11/          D3D11 device, context, resources, state objects
├── d3d/d3d12/          D3D12 stubs
├── dxgi/               DXGI factory, adapter, swap chain
├── metal/              Metal backend wrappers
│   ├── device/         MTLDevice
│   ├── command/        MTLCommandQueue, MTLCommandBuffer
│   ├── pipeline/       Pipeline state creation
│   └── shader/         HLSL → MSL shader translation
├── audio/              XAudio2 → CoreAudio
└── input/              XInput → GameController
include/
├── metalsharp/         Internal headers
├── d3d/                D3D COM interface definitions
└── dxgi/               DXGI COM interface definitions
tests/                  Test suite
tools/launcher/         CLI launcher
```

## How it works (Phase 1)

Currently, shaders are passed as MSL source strings through the D3D11 `CreateVertexShader` / `CreatePixelShader` APIs. The `ShaderTranslator` compiles them with `MTLDevice.newLibraryWithSource`. The `D3D11DeviceContext` caches a `MTLRenderPipelineState` and encodes draw commands into a `MTLCommandBuffer` when `Draw()` is called.

The next phase will add real DXBC/DXIL bytecode parsing so actual game shaders work.

## Requirements

- macOS 13+ (Ventura or later)
- Apple Silicon Mac (M1/M2/M3/M4) or Intel Mac with Metal support
- Xcode Command Line Tools
- CMake 3.24+
- C++20 compatible compiler

## License

MIT

## References

- [DXVK](https://github.com/doitsujin/dxvk) — D3D → Vulkan, architecture reference
- [MoltenVK](https://github.com/KhronosGroup/MoltenVK) — Vulkan → Metal, SPIR-V → MSL
- [Apple Game Porting Toolkit](https://developer.apple.com/games/game-porting-toolkit/) — Apple's official D3D → Metal layer
- [Wine](https://gitlab.winehq.org/wine/wine) — Win32 API translation
- [D3D11 API](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/) — Microsoft D3D11 reference
- [Metal API](https://developer.apple.com/documentation/metal) — Apple Metal reference
