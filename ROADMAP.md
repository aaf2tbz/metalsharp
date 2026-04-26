# MetalSharp Roadmap

Direct3D → Metal translation layer. Single-hop, no Vulkan middleman.

## Phase 1 — Triangle

Get a hardcoded triangle rendering on screen through D3D11 codepaths calling Metal.

- [x] Metal device initialization (MTLDevice + MTLCommandQueue)
- [x] D3D11 device + immediate context (COM interfaces, ref counting)
- [x] DXGI swap chain → CAMetalLayer
- [x] Vertex buffer → MTLBuffer allocation
- [x] DXGI format → Metal pixel format translation
- [x] Shader translator: embed a hardcoded MSL triangle shader, compile with newLibraryWithSource
- [x] Input layout → MTLVertexDescriptor mapping
- [x] Render pipeline state: translate D3D11 blend/rasterizer/depth state to MTLRenderPipelineDescriptor
- [x] Draw call encoding: flushRenderState() → MTLRenderCommandEncoder → drawPrimitives
- [x] End-to-end test: D3D11CreateDevice → create buffer → set shaders → draw → present triangle

**Milestone:** A colored triangle appears in a window via D3D11 codepaths. ✅ COMPLETE

## Phase 2 — D3D11 Coverage

Full D3D11 feature set for real games.

- [ ] Shader translator: DXBC bytecode parsing and HLSL→MSL translation
- [ ] All D3D11 resource types (textures, UAVs, structured buffers)
- [ ] State objects (blend, rasterizer, depth stencil, sampler) → Metal equivalents
- [ ] Shader resource views → MTLTexture bindings
- [ ] Multiple render targets
- [ ] Map/Unmap for CPU-accessible resources (staging buffers)
- [ ] Dynamic constant buffer uploads
- [ ] Texture loading and subresource updates
- [ ] GenerateMips implementation
- [ ] CopyResource, ResolveSubresource
- [ ] Predication, queries (OCCLUSION, TIMESTAMP)
- [ ] Deferred context support (multithreaded rendering)

**Milestone:** Run a D3D11 game with moderate shader complexity without crashing.

## Phase 3 — D3D12

Direct3D 12 translation for modern games.

- [ ] D3D12 device → MTLDevice
- [ ] Command queue → MTLCommandQueue with proper synchronization
- [ ] Command lists/allocators → MTLCommandBuffer + encoder pooling
- [ ] Descriptor heaps → Metal argument buffers
- [ ] Root signatures → Metal argument table layout mapping
- [ ] Pipeline state objects (PSO) → MTLRenderPipelineState / MTLComputePipelineState
- [ ] Resource barriers → Metal resource state tracking
- [ ] Command signature (indirect drawing)
- [ ] Tiled resources (sparse textures)
- [ ] Sampler feedback

**Milestone:** Run a D3D12 game at playable framerates.

## Phase 4 — Runtime & Integration

Wine integration, DLL injection, system services.

- [ ] Wine prefix bootstrap (auto-create and configure)
- [ ] DLL override registration (d3d11=native, dxgi=native, etc.)
- [ ] PE loader hook: inject MetalSharp dylibs into Wine process space
- [ ] XAudio2 → CoreAudio bridge (spatial audio, streaming)
- [ ] XInput → GameController framework (rumble, triggers, gyro)
- [ ] Window management: HWND → NSWindow/CAMetalLayer binding
- [ ] DXGI output enumeration (display modes, VSync)
- [ ] Error handling and diagnostic logging
- [ ] Configuration file system (per-game overrides, shader caches)

**Milestone:** Launch a Windows game via `metalsharp_launcher game.exe` and have it render.

## Phase 5 — Performance

Competitive with native ports.

- [ ] Shader cache (DXBC/DXIL → compiled MSL library, persisted to disk)
- [ ] Pipeline state cache (MTLRenderPipelineState serialization)
- [ ] Command buffer batching and deferred submission
- [ ] Memory allocation pooling (MTLBuffer recycling)
- [ ] MetalFX upscaling integration (spatial upscaling, temporal AA)
- [ ] MetalFX frame interpolation
- [ ] Frame pacing and present timing
- [ ] Multithreaded command encoding
- [ ] Argument buffer optimization for descriptor heaps
- [ ] GPU profiling integration (Metal System Trace)
- [ ] Game-specific compatibility profiles

**Milestone:** Run AAA titles at 60fps+ on M-series Macs with visual parity.

## Phase 6 — Community

- [ ] Documentation and architecture guide
- [ ] Compatibility database (per-game testing results)
- [ ] CI/CD pipeline (build + test on GitHub Actions)
- [ ] Contribution guidelines
- [ ] Discord/community forum
- [ ] Integration with existing tools (Homebrew formula, CrossOver compatibility)

## Technical Dependencies

| Component | Purpose | Options |
|-----------|---------|---------|
| Shader Translation | DXBC/DXIL → MSL | Apple Metal Shader Converter (DXIL), custom DXBC parser, or SPIRV-Cross via DXVK's SPIR-V output |
| Wine | Win32 API translation | Wine 8.x+, custom builds |
| Metal Performance | GPU profiling | Xcode Instruments, Metal HUD |
| Testing | Game compatibility | ProtonDB-like crowdsource model |

## Key References

- DXVK: https://github.com/doitsujin/dxvk (D3D→Vulkan, architecture reference)
- MoltenVK: https://github.com/KhronosGroup/MoltenVK (Vulkan→Metal, SPIR-V→MSL)
- Apple GPTK: https://developer.apple.com/games/game-porting-toolkit/
- Wine: https://gitlab.winehq.org/wine/wine
- D3D11 spec: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/
- D3D12 spec: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/
- Metal spec: https://developer.apple.com/documentation/metal
