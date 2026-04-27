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

**Milestone:** A colored triangle appears in a window via D3D11 codepaths. COMPLETE

## Phase 2 — D3D11 Coverage

Full D3D11 feature set for real games.

- [x] Shader translator: DXBC bytecode parsing and HLSL→MSL translation
- [x] All D3D11 resource types (textures, UAVs, structured buffers)
- [x] State objects (blend, rasterizer, depth stencil, sampler) → Metal equivalents
- [x] Shader resource views → MTLTexture bindings
- [x] Multiple render targets (8 color attachments, per-RT blend state)
- [x] Map/Unmap for CPU-accessible resources (staging buffers)
- [x] Dynamic constant buffer uploads
- [x] Texture loading and subresource updates (UpdateSubresource with replaceRegion)
- [x] GenerateMips implementation
- [x] CopyResource (texture-to-texture + buffer-to-buffer)
- [x] CopySubresourceRegion
- [x] ResolveSubresource (MSAA resolve)
- [x] Predication, queries (OCCLUSION, TIMESTAMP, EVENT)
- [x] Deferred context support (multithreaded rendering)

**Milestone:** Run a D3D11 game with moderate shader complexity without crashing. COMPLETE

## Phase 3 — D3D12

Direct3D 12 translation for modern games.

- [x] D3D12 device → MTLDevice
- [x] Command queue → MTLCommandQueue with proper synchronization
- [x] Command lists/allocators → MTLCommandBuffer + encoder pooling
- [x] Descriptor heaps → Metal argument buffers
- [x] Root signatures → Metal argument table layout mapping
- [x] Pipeline state objects (PSO) → MTLRenderPipelineState / MTLComputePipelineState
- [x] Resource barriers → Metal resource state tracking
- [x] Command signature (indirect drawing)

**Milestone:** Run a D3D12 game at playable framerates. COMPLETE

## Phase 4 — Runtime & Integration

Wine integration, DLL injection, system services.

- [x] Wine prefix bootstrap (auto-create and configure)
- [x] DLL override registration (d3d11=native, dxgi=native, etc.)
- [x] PE loader hook: inject MetalSharp dylibs into Wine process space
- [x] XAudio2 → CoreAudio bridge (spatial audio, streaming)
- [x] XInput → GameController framework (button/trigger/thumbstick mapping)
- [x] Window management: HWND → NSWindow/CAMetalLayer binding
- [x] DXGI output enumeration (real display modes via CoreGraphics)
- [x] Error handling and diagnostic logging (Logger with file + stderr output)
- [x] Configuration file system (TOML-like, per-game profiles)
- [x] SteamCMD integration (Windows depot download, game library scanning)
- [x] Game launcher CLI (metalsharp_launcher with Steam game download support)

**Milestone:** Launch a Windows game via `metalsharp_launcher game.exe` or `metalsharp_launcher --steam <appid>` and have it render. COMPLETE

## Phase 5 — Performance

Competitive with native ports.

- [x] Shader cache (DXBC/DXIL → compiled MSL library, persisted to disk)
- [x] Pipeline state cache (MTLRenderPipelineState serialization)
- [x] Command buffer batching and deferred submission
- [x] Memory allocation pooling (MTLBuffer recycling)
- [x] MetalFX upscaling integration (spatial upscaling, temporal AA)
- [x] MetalFX frame interpolation
- [x] Frame pacing and present timing
- [x] Multithreaded command encoding
- [x] Argument buffer optimization for descriptor heaps
- [x] GPU profiling integration (pass-level timing, draw/compute counting, frame stats)
- [x] Game-specific compatibility profiles (per-game config via metalsharp.toml)
- [x] Tiled resources (sparse textures)
- [x] Sampler feedback

**Milestone:** Run AAA titles at 60fps+ on M-series Macs with visual parity. COMPLETE

## Phase 6 — Cs:GO

**Open Steam. Launch CS:GO with MetalSharp. Play with native settings.**

- [x] DXBC bytecode parser (CS:GO ships DXBC shaders)
- [x] DXBC → MSL translation (opcode-by-opcode, starting with the most common ops)
- [x] Wine integration: auto-create prefix, register DLL overrides
- [x] PE loader hook: inject MetalSharp dylibs into Wine process space
- [x] HWND → NSWindow/CAMetalLayer binding
- [x] XAudio2 → CoreAudio (footstep audio, voice chat, spatial)
- [x] XInput → GameController (mouse/keyboard passthrough, controller support)
- [x] DXGI output enumeration (display modes, VSync, fullscreen)
- [x] Per-game compatibility profile for CS:GO
- [x] Shader cache (DXBC → compiled MSL, persisted to disk)
- [x] Frame pacing and present timing
- [x] Configuration system (metalsharp.toml)

**Milestone:** Open Steam → Launch CS:GO → MetalSharp translates D3D11 calls to Metal → Play at playable framerates on Apple Silicon.

## Phase 7 — Community

- [ ] Documentation and architecture guide
- [ ] Compatibility database (per-game testing results)
- [x] CI/CD pipeline (build + test on GitHub Actions)
- [x] Contribution guidelines (CONTRIBUTING.md)
- [ ] Discord/community forum
- [ ] Integration with existing tools (Homebrew formula, CrossOver compatibility)

## Technical Dependencies

| Component | Purpose | Options |
|-----------|---------|---------|
| Shader Translation | DXBC/DXIL → MSL | Custom DXBC parser + Apple Metal Shader Converter for DXIL |
| Wine | Win32 API translation | Wine 8.x+, custom builds |
| SteamCMD | Windows game depot download | `steamcmd +@sSteamCmdForcePlatformType windows` |
| Metal Performance | GPU profiling | Xcode Instruments, Metal HUD |
| Testing | Game compatibility | ProtonDB-like crowdsource model |
