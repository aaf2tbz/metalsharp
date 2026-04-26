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

**Phase 1 + Phase 2 complete.** Full D3D11 API coverage with DXBC shader translation.

| What | Status |
|------|--------|
| Metal device init (MTLDevice + MTLCommandQueue) | Done |
| D3D11 device + immediate context (COM interfaces) | Done |
| DXGI swap chain → CAMetalLayer | Done |
| Vertex/index/constant buffers → MTLBuffer | Done |
| Texture 1D/2D/3D creation with mipmaps, MSAA, initial data | Done |
| DXGI format → MTLPixelFormat translation | Done |
| MSL shader compilation via newLibraryWithSource | Done |
| **DXBC bytecode parser + DXBC→MSL translator** | Done |
| State objects (blend, rasterizer, depth stencil, sampler) | Done |
| 8 render targets with per-RT blend state | Done |
| Input layouts → MTLVertexDescriptor | Done |
| Shader resource views, render target views, depth stencil views | Done |
| Unordered access views (texture + buffer) | Done |
| Draw calls: Draw, DrawIndexed, DrawInstanced, DrawIndexedInstanced | Done |
| Map/Unmap, UpdateSubresource, CopyResource, CopySubresourceRegion | Done |
| GenerateMips, ResolveSubresource (MSAA) | Done |
| Queries (occlusion, timestamp, event) + predication | Done |
| Compute shader state management | Done |
| 6/6 tests passing on CI (macOS) | Passing |

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
        ├── Pipeline    → MTLRenderPipelineState (MRT, per-RT blend)
        ├── Shader      → DXBC bytecode → MSL → Metal library
        ├── Resources   → MTLBuffer, MTLTexture (1D/2D/3D), MTLSamplerState
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
| `metalsharp_core` | static lib | Metal backend, DXBC parser, MSL translator |
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
├── metal/
│   ├── device/         MTLDevice wrapper
│   ├── command/        MTLCommandQueue, MTLCommandBuffer
│   ├── pipeline/       Pipeline state creation & management
│   ├── shader/         DXBC parser, DXBC→MSL translator, MSL compiler
│   ├── Buffer.mm       MTLBuffer wrapper
│   ├── Texture.mm      MTLTexture 1D/2D/3D wrapper
│   ├── Sampler.mm      MTLSamplerState + format translation
│   └── Framebuffer.mm  MTLRenderPassDescriptor wrapper
├── audio/              XAudio2 → CoreAudio (stub)
└── input/              XInput → GameController (stub)
include/
├── metalsharp/         Internal headers (PipelineState, DXBCParser, DXBCtoMSL, etc.)
├── d3d/                D3D11 COM interface definitions
└── dxgi/               DXGI COM interface definitions
tests/                  6 tests: metal_device, format_translation, d3d11_device,
                        triangle, phase2, dxbc
tools/launcher/         CLI launcher
```

## How it works

Shaders can be provided two ways:

1. **MSL source strings** — passed through `CreateVertexShader`/`CreatePixelShader`, compiled with `newLibraryWithSource`
2. **DXBC bytecode** — `translateDXBC()` parses the DXBC container (SHDR/SHEX chunks, input/output signatures), extracts SM5.0 bytecode tokens, and generates MSL source which is then compiled to a Metal library

The `D3D11DeviceContext` caches a `MTLRenderPipelineState` built from current state (shaders, blend, rasterizer, depth) and encodes draw commands into a `MTLCommandBuffer` when `Draw()` or `DrawIndexed()` is called. Pipeline state is rebuilt automatically when blend state, render targets, or shaders change.

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
