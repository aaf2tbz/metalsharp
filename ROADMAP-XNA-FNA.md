# MetalSharp Roadmap: XNA/FNA/.NET Game Support

## Why This Matters

A massive chunk of indie games (Celeste, Stardew Valley, Terraria, Hollow Knight,
Bastion, Fez, TowerFall, etc.) use XNA, FNA, or MonoGame. All of these ultimately
render through either D3D9 (original XNA) or OpenGL/D3D11/Metal (FNA/MonoGame).

Supporting this ecosystem means MetalSharp can run thousands of games, not just
handfuls of native D3D11 titles.

## The Rendering Stacks

### XNA 4.0 Games (e.g., Celeste)
```
Game.exe (.NET/Mono)
    → Microsoft.Xna.Framework.dll (GAC, Wine provides via Mono)
        → D3D9 device calls (d3d9.dll)
            → D3D9 shader bytecode (SM2.0/SM3.0 inside .xnb files)
```

### FNA Games (e.g., TowerFall, recent Celeste ports)
```
Game.exe (.NET/Mono)
    → FNA.dll
        → FNA3D (native C library)
            → Renderer: SDL_GPU / OpenGL / D3D11 / Metal (pluggable)
            → Shaders: D3D9 Effect bytecode → MojoShader → target backend
```

### MonoGame Games
```
Game.exe (.NET/Mono or .NET Core)
    → MonoGame.Framework.dll
        → DirectX backend → D3D11
        → OpenGL backend → OpenGL
        → DesktopGL backend → SDL2 + OpenGL
```

## Strategy: Two-Pronged Approach

### Path A: D3D9 → Metal Bridge (for XNA + older games)
### Path B: FNA3D Metal Backend (for FNA games — already exists upstream)

**Path A is our primary work. Path B exists in FNA3D already, we just need
to ensure our Wine environment loads the right FNA3D backend.**

---

## Phase 1: D3D9 → Metal Translation Layer

### Goal
Run XNA games (like Celeste) through Wine by intercepting D3D9 calls and
translating them to Metal, using the same PE↔unix bridge architecture as D3D11.

### Architecture
```
XNA Game (.NET)
    → Mono (Wine)
        → XNA Framework (D3D9 calls)
            → d3d9.dll (our PE shim)
                → d3d9.so (our unix .so)
                    → Metal (MTLDevice, MTLCommandQueue, etc.)
```

### New Files

| File | Purpose |
|------|---------|
| `src/wine/d3d9_pe.cpp` | PE D3D9 shim — IDirect3D9, IDirect3DDevice9 COM interfaces |
| `src/wine/d3d9_unix.mm` | Metal backend for D3D9 translation |
| `src/wine/d3d9_unix.h` | Param structs and func IDs for D3D9 unix bridge |

### Key D3D9 Interfaces to Implement

**IDirect3D9** (factory)
- CreateDevice → creates IDirect3DDevice9
- GetAdapterDisplayMode, GetDeviceCaps, CheckDeviceFormat

**IDirect3DDevice9** (device)
- CreateVertexBuffer, CreateIndexBuffer → MTLBuffer
- CreateTexture, CreateCubeTexture → MTLTexture
- CreateRenderTarget, CreateDepthStencilSurface → MTLTexture
- CreatePixelShader, CreateVertexShader → MTLFunction (via MojoShader)
- CreateVertexDeclaration → MTLVertexDescriptor
- SetVertexDeclaration, SetStreamSource, SetIndices
- SetVertexShader, SetPixelShader, SetVertexShaderConstantF, SetPixelShaderConstantF
- SetTexture, SetSamplerState
- BeginScene, EndScene, BeginStateBlock, EndStateBlock
- DrawPrimitive, DrawIndexedPrimitive
- Clear, Present
- SetRenderTarget, SetDepthStencilSurface
- SetViewport, SetScissorRect

**IDirect3DVertexDeclaration9** → MTLVertexDescriptor mapping
**IDirect3DVertexBuffer9** → MTLBuffer wrapper
**IDirect3DIndexBuffer9** → MTLBuffer wrapper
**IDirect3DTexture9** → MTLTexture wrapper
**IDirect3DPixelShader9** / **IDirect3DVertexShader9** → MojoShader compiled functions

### Shader Translation: D3D9 SM2.0/SM3.0 → MSL

D3D9 shaders use SM2.0/SM3.0 bytecode (not DXBC — older, simpler format).

**Option 1: MojoShader (recommended)**
- FNA3D already uses MojoShader for this exact purpose
- MojoShader parses D3D9 effect bytecode and can output GLSL
- We can use MojoShader's GLSL output → translate to MSL (minimal changes)
- MojoShader is BSD licensed, C library, easy to integrate
- URL: https://github.com/icculus/mojoshader

**Option 2: Direct SM→MSL translator**
- Write our own SM2.0/SM3.0 bytecode parser
- Emit MSL directly
- More work but no dependency

**Recommendation: Use MojoShader.** It's battle-tested by FNA and handles all
the weird edge cases of SM2.0/SM3.0 bytecode. We integrate it as a subproject.

### MojoShader Integration Plan
1. Add MojoShader as a git submodule or vendored copy
2. Build as a static library linked into our unix .so
3. On CreateVertexShader/CreatePixelShader: pass bytecode to MojoShader
4. MojoShader outputs GLSL → minimal text transform to MSL
5. Compile MSL with newLibraryWithSource (same as our MSL path)

### .XNB Content Pipeline
XNA games store compiled assets as .xnb files. Effect shaders (.xnb) contain
D3D9 shader bytecode with XNA headers. We need to:
1. Parse .xnb container format (5-byte header: "XNBw" + version + flags + size)
2. Read reader type string (e.g., "Microsoft.Xna.Framework.Content.EffectReader")
3. Extract D3D9 effect bytecode from the .xnb payload
4. Pass to MojoShader for translation

This happens inside the game's .NET code (XNA ContentManager), not in our DLL.
Our D3D9 shim intercepts the Create*Shader calls with the already-extracted bytecode.

### D3D9 State Management
D3D9 is a state-machine API (unlike D3D11 which is more explicit). We need:

```
struct D3D9State {
    // Render states (over 200 possible)
    DWORD render_states[256];
    
    // Texture stage states (8 stages × ~30 states)
    DWORD texture_stage_states[8][32];
    
    // Sampler states (8+ samplers × ~13 states)
    DWORD sampler_states[16][13];
    
    // Current bindings
    MTLBuffer* vertex_buffers[16];
    MTLBuffer* index_buffer;
    MTLTexture* textures[16];
    MTLSamplerState* samplers[16];
    
    // Viewport, scissor
    D3DVIEWPORT9 viewport;
    RECT scissor_rect;
    
    // Render targets
    MTLTexture* render_targets[4];
    MTLTexture* depth_stencil;
};
```

State blocks (BeginStateBlock/EndStateBlock/CaptureState) need to snapshot
and restore this state.

### D3D9 Fixed-Function Pipeline
XNA uses the programmable pipeline, but some older games use fixed-function.
We can handle this by:
1. Generating MSL shaders that replicate the fixed-function behavior
2. Keying on the current render state combination
3. Caching the generated shaders

For XNA games, this is less critical — XNA forces the programmable pipeline.

### D3D9 Swap Chain → CAMetalLayer
Same approach as D3D11: CreateDevice creates our unix swap chain, Present
commits the command buffer and presents the drawable.

---

## Phase 2: FNA/FNA3D Support

### Why This Is Easier
FNA3D already has a Metal backend (written by Ethan Lee). The challenge is
getting Wine to load the right native library.

### Approach
1. Build FNA3D's Metal backend for macOS
2. Install as `libFNA3D.dylib` in the game directory
3. FNA's .NET code calls into this native library via P/Invoke
4. The Metal backend renders directly — no D3D translation needed

### Wine Considerations
- Wine's Mono runtime handles .NET P/Invoke to native libraries
- We need to ensure `libFNA3D.dylib` is loadable from the Wine prefix
- May need to set `LD_LIBRARY_PATH` or use `dllmap` config

### XNA → FNA Migration (Alternative Path)
Instead of building D3D9 support, we could:
1. Replace XNA assemblies with FNA assemblies in the game directory
2. FNA uses SDL2 + FNA3D (Metal backend) instead of D3D9
3. This is what porters actually do when porting XNA games to Mac/Linux

**Pros:** Much less work — FNA3D Metal already exists
**Cons:** Not all XNA games are compatible with FNA drop-in replacement
         (Celeste works with FNA, but others may have edge cases)

**Recommendation:** Support both paths. Phase 1 (D3D9 bridge) for maximum
compatibility. Phase 2 (FNA replacement) as a simpler alternative.

---

## Phase 3: MonoGame Support

MonoGame has three desktop backends:
- **WindowsDX** → uses SharpDX → D3D11 (our existing bridge handles this!)
- **DesktopGL** → uses SDL2 + OpenGL
- **Platform** → platform-specific

For WindowsDX MonoGame games, our D3D11 bridge should work with minimal changes.
The main addition is SharpDX's D3D11 usage patterns (which may differ from native).

---

## Implementation Order

### Sprint 1: D3D9 Shell + MojoShader Integration
- [ ] Create d3d9_pe.cpp with IDirect3D9/CreateDevice stubs
- [ ] Create d3d9_unix.h with param structs
- [ ] Create d3d9_unix.mm with Metal device init
- [ ] Integrate MojoShader as submodule
- [ ] Build MojoShader as static lib
- [ ] Wire CreateVertexShader/CreatePixelShader through MojoShader → MSL

### Sprint 2: D3D9 Resource Management
- [ ] Implement CreateVertexBuffer/IndexBuffer → MTLBuffer
- [ ] Implement CreateTexture → MTLTexture
- [ ] Implement CreateRenderTarget/CreateDepthStencilSurface
- [ ] Implement CreateVertexDeclaration → MTLVertexDescriptor
- [ ] Implement Set* bindings and state tracking

### Sprint 3: D3D9 Rendering
- [ ] Implement DrawPrimitive/DrawIndexedPrimitive
- [ ] Implement Clear/Present
- [ ] Implement swap chain → CAMetalLayer
- [ ] Implement state management (render states, sampler states)
- [ ] Implement shader constant binding

### Sprint 4: XNA Test — Celeste
- [ ] Ensure Wine Mono runs Celeste.exe
- [ ] Verify XNA framework loads
- [ ] Test shader creation (MojoShader path)
- [ ] Test rendering pipeline end-to-end
- [ ] Debug and iterate

### Sprint 5: FNA3D Metal Backend
- [ ] Build FNA3D with Metal backend
- [ ] Test FNA game with native FNA3D
- [ ] Create XNA→FNA migration tool/docs
- [ ] Test Celeste with FNA replacement

### Sprint 6: MonoGame Support
- [ ] Test MonoGame WindowsDX game through D3D11 bridge
- [ ] Handle SharpDX-specific patterns
- [ ] Test MonoGame DesktopGL through SDL2/OpenGL path

---

## Key Dependencies

| Library | Purpose | License | Size |
|---------|---------|---------|------|
| MojoShader | D3D9 shader bytecode → GLSL/MSL | zlib/BSD | ~30K LOC |
| SDL2/SDL3 | Window/input for FNA | zlib | Already available |
| FNA3D | Graphics backend for FNA games | zlib | ~15K LOC |
| Wine Mono | .NET runtime | MIT | Wine provides |

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| MojoShader GLSL→MSL gaps | Shaders fail to compile | MojoShader is battle-tested by FNA; add MSL patches as needed |
| XNA .xnb format edge cases | Content fails to load | Most games use standard XNA content; debug per-game |
| D3D9 state machine complexity | Wrong render output | Start with XNA games (limited state usage), expand gradually |
| Wine Mono compatibility | .NET game crashes | Wine Mono is mature; most XNA games work |
| Fixed-function pipeline | Extra rendering path | Defer to later; XNA uses programmable pipeline |

## Success Metrics

- [ ] Celeste launches and renders through MetalSharp (D3D9 path)
- [ ] At least 3 XNA games render correctly
- [ ] FNA games run via native FNA3D Metal backend
- [ ] MonoGame WindowsDX games run through D3D11 bridge
- [ ] Frame rate within 90% of native macOS port where available
