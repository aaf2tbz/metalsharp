# DXMT & Vulkan Architecture

MetalSharp uses two D3D translation pipelines to render Windows games on macOS Metal: **DXMT** (D3D→Metal, direct) and **DXVK + MoltenVK** (D3D→Vulkan→Metal, indirect). DXMT is the primary path. The DXVK pipeline is a fallback for games that need Vulkan intermediaries.

## Pipeline overview

```
┌─────────────────────────────────────────────────────────┐
│                    Game (Windows PE)                     │
│                  d3d11.dll / dxgi.dll                    │
└──────────┬──────────────────────────┬───────────────────┘
           │                          │
     ┌─────▼─────┐            ┌──────▼──────┐
     │   DXMT    │            │    DXVK     │
     │  v0.80+10 │            │   1.10.3    │
     └─────┬─────┘            └──────┬──────┘
           │                          │
     ┌─────▼─────┐            ┌──────▼──────┐
     │winemetal  │            │  MoltenVK   │
     │   .so     │            │  Vulkan     │
     └─────┬─────┘            └──────┬──────┘
           │                          │
     ┌─────▼──────────────────────────▼─────┐
     │           Apple Metal.framework      │
     │              MTLDevice               │
     └──────────────────────────────────────┘
```

## DXMT — Direct D3D to Metal

DXMT (DX Metal Translation) is the primary graphics pipeline. It translates Direct3D 11 calls directly to Metal command buffers with no Vulkan intermediary. Built from source with LLVM 15 and Apple's Metal toolchain.

### How DXMT works

```
Game creates D3D11 device
  → loads d3d11.dll (DXMT PE DLL, 5.4MB i386 / 5.1MB x86_64)
    → DXMT d3d11.dll creates a Metal device (MTLDevice)
      → D3D11 calls generate Metal command buffers via winemetal.so
        → Command buffers submitted to Metal command queue
          → Metal framework executes on GPU
```

The PE DLL (`d3d11.dll`) is the D3D11 API surface — it implements ID3D11Device, ID3D11DeviceContext, etc. The heavy lifting happens in `winemetal.so` (31MB, Mach-O x86_64), which is the unix-side bridge that creates and manages Metal resources.

### DXMT components

| File | Architecture | Size | Role |
|------|-------------|------|------|
| `d3d11.dll` | PE32 (i386) | 5.4MB | D3D11 device, context, resources for 32-bit games |
| `dxgi.dll` | PE32 (i386) | 1.8MB | DXGI factory, adapter, swap chain for 32-bit games |
| `d3d10core.dll` | PE32 (i386) | 1.4MB | D3D10 core — some games create D3D10 devices |
| `winemetal.dll` | PE32 (i386) | 68KB | PE→unix bridge for winemetal.so |
| `d3d11.dll` | PE32+ (x86_64) | 5.1MB | D3D11 for 64-bit games |
| `dxgi.dll` | PE32+ (x86_64) | 1.8MB | DXGI for 64-bit games |
| `d3d10core.dll` | PE32+ (x86_64) | 1.5MB | D3D10 core for 64-bit |
| `nvapi64.dll` | PE32+ (x86_64) | 1.8KB | NVIDIA API shim |
| `nvngx.dll` | PE32+ (x86_64) | 1.5MB | NGX (DLSS) shim — no-op on macOS |
| `winemetal.dll` | PE32+ (x86_64) | 72KB | PE→unix bridge for 64-bit |
| `winemetal.so` | Mach-O x86_64 | 31MB | Metal command buffer bridge (unix side) |

### winemetal.so — the unix bridge

`winemetal.so` is the only unix (.so) binary in DXMT. There is no i386-unix version and none can be built on macOS. This works because:

1. All unix .so modules in MetalSharp Wine are x86_64
2. Wine's WoW64 layer handles the 32-bit PE → 64-bit unix .so transition
3. When a 32-bit game's `d3d11.dll` calls into `winemetal.dll`, the PE bridge forwards to `winemetal.so` (x86_64), and WoW64 handles the bit-width transition

The DYLD path must include DXMT's unix directory for the dynamic linker to find `winemetal.so`:
```
DYLD_FALLBACK_LIBRARY_PATH=...:$MS_LIB/dxmt/x86_64-unix:...
```

### DXMT i386 builtins

For 32-bit games, the DXMT i386 PE DLLs are installed directly into Wine's `i386-windows/` directory, replacing Wine's original builtins:

```
i386-windows/
  d3d11.dll       → DXMT (5.4MB, was Wine builtin ~2MB)
  dxgi.dll        → DXMT (1.8MB)
  d3d10core.dll   → DXMT (1.4MB)
  winemetal.dll   → DXMT bridge (68KB)
  (originals saved as .bak)
```

This is necessary because Wine's WoW64 implementation has a bug where `WINEDLLOVERRIDES=native` fails for 32-bit processes on macOS — the DLL search path resolves as null. By replacing the builtins directly, Wine loads DXMT without needing the override mechanism.

64-bit builtins are **not** replaced. Steam itself is 64-bit and needs Wine's original D3DMetal builtins to render its UI. For 64-bit games that need DXMT, the `metalsharp-wine` wrapper copies DXMT's x86_64 PE DLLs into the game directory and uses `WINEDLLOVERRIDES`, which works correctly for 64-bit processes.

### Backend selection

The `metalsharp-wine` wrapper dispatches based on `MS_BACKEND`:

| `MS_BACKEND` | What happens |
|---------------|-------------|
| (unset/default) | Runs Wine with builtins as-is. DXMT i386 builtins are already in place for 32-bit. 64-bit uses Wine's D3DMetal. |
| `dxmt` | Copies DXMT PE DLLs into `MS_GAME_DIR`, sets `WINEDLLOVERRIDES=d3d11,dxgi,d3d10core=native`. Used for 64-bit games that need DXMT specifically. |
| `dxmt_dxvk` | Same as `dxmt` but adds DXVK env vars: `DXVK_FRAME_RATE=60`, `MVK_PRESENT_MODE=1`, `DXVK_ASYNC=1`. |
| `dxvk` | Sets `WINEDLLOVERRIDES=d3d9=native`. For D3D9 games that should use DXVK instead of wined3d. |
| `gptk` | Bypasses MetalSharp Wine entirely — execs Apple's GPTK wine64 directly. |

## DXVK + MoltenVK — Vulkan intermediary pipeline

DXVK translates Direct3D 9/10/11 calls to Vulkan. MoltenVK translates Vulkan calls to Metal. This is the fallback path — it adds a Vulkan layer between D3D and Metal, which introduces overhead but supports games that don't work with direct D3D→Metal translation.

### Pipeline

```
Game D3D11 calls
  → DXVK d3d11.dll (translates D3D → Vulkan)
    → MoltenVK libMoltenVK.dylib (translates Vulkan → Metal)
      → Metal framework
```

### Why DXVK 1.10.3 (not 2.x)

DXVK 2.x requires Vulkan 1.3 features. MoltenVK on macOS supports Vulkan 1.1 (some 1.2). DXVK 1.10.3 is the last version that works reliably with MoltenVK's Vulkan 1.1 support.

### DXVK layout

```
~/.metalsharp/runtime/dxvk-1.10.3/
├── x32/
│   ├── d3d11.dll      (3.5MB, D3D11 → Vulkan)
│   ├── d3d10core.dll  (D3D10 → Vulkan)
│   ├── d3d9.dll       (D3D9 → Vulkan)
│   └── dxgi.dll       (DXGI → Vulkan)
└── x64/
    ├── d3d11.dll
    ├── d3d10core.dll
    ├── d3d9.dll
    └── dxgi.dll
```

DXVK DLLs are **not** placed in Wine's builtin directories. They're copied into individual game directories by the setup scripts when needed, with `WINEDLLOVERRIDES` set to load them as native DLLs.

### MoltenVK

MoltenVK is installed via Homebrew (`brew install moltenvk`), not bundled. It provides:
- Vulkan instance/device creation → MTLDevice
- Vulkan command buffers → Metal command buffers
- Vulkan image views → Metal textures
- Vulkan pipeline objects → Metal render pipelines

Environment for the DXVK pipeline:
```
VK_ICD_FILENAMES=/opt/homebrew/share/vulkan/icd.d/MoltenVK_icd.json
MVK_PRESENT_MODE=1         # FIFO (vsync)
DXVK_FRAME_RATE=60         # Frame rate limiter
DXVK_ASYNC=1               # Async pipeline compilation
```

## Comparison: DXMT vs DXVK+MoltenVK

| Aspect | DXMT | DXVK + MoltenVK |
|--------|------|-----------------|
| Translation hops | D3D → Metal (1 hop) | D3D → Vulkan → Metal (2 hops) |
| Latency | Lower | Higher (extra translation layer) |
| Compatibility | Works for most D3D11 games | Wider D3D9/10/11 coverage |
| Vulkan requirement | No | Yes (MoltenVK needed) |
| DXVK version | N/A | 1.10.3 (Vulkan 1.1 limit) |
| 32-bit support | Via i386 builtins (no override needed) | Via WINEDLLOVERRIDES (works for 32-bit with DXVK) |
| Shader compilation | DXBC/DXIL → MSL directly | DXBC → SPIR-V → MSL |
| Metal feature access | Direct (Metal 3+) | Limited to what MoltenVK exposes |

## When each pipeline is used

| Game | Pipeline | Why |
|------|----------|-----|
| Nidhogg 2 | DXMT builtins | 32-bit D3D11 game, DXMT handles it directly |
| Undertale | Wine wined3d | Simple D3D9 game, no DXVK/DXMT needed |
| Rain World | Apple GPTK | Works best with Apple's D3DMetal |
| RE4, Among Us, etc. | Wine D3DMetal (64-bit builtins) | 64-bit games, Steam DRM, Wine's built-in D3DMetal works |
| Future D3D9 games needing DXVK | DXVK + MoltenVK | DXVK's D3D9 support is more complete than wined3d |

The default path for unknown games is Wine's built-in D3DMetal (64-bit) or DXMT builtins (32-bit). DXVK+MoltenVK is only used when a game specifically needs it.
