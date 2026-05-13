# DXMT & Vulkan Architecture

MetalSharp uses two D3D translation pipelines: **DXMT** (D3D→Metal, direct) and **DXVK + MoltenVK** (D3D→Vulkan→Metal, indirect). DXMT is the primary path.

## Pipeline Overview

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

DXMT translates Direct3D 11/12 calls directly to Metal command buffers. No Vulkan intermediary.

### How It Works

```
Game creates D3D11 device
  → loads d3d11.dll (DXMT PE DLL, copied to game dir by Rust backend)
    → DXMT d3d11.dll creates MTLDevice via winemetal.so
      → D3D11 calls generate Metal command buffers
        → Command buffers submitted to Metal queue
          → Metal framework executes on GPU
```

### DXMT Components

| File | Architecture | Role |
|------|-------------|------|
| `d3d11.dll` | PE32+ (x86_64) | D3D11 device, context, resources |
| `dxgi.dll` | PE32+ (x86_64) | DXGI factory, adapter, swap chain |
| `d3d10core.dll` | PE32+ (x86_64) | D3D10 core — some games need it |
| `d3d12.dll` | PE32+ (x86_64) | D3D12 (for DxmtMetal12 engine) |
| `winemetal.dll` | PE32+ (x86_64) | PE→unix bridge |
| `winemetal.so` | Mach-O x86_64 | Metal command buffer bridge (47MB) |

### winemetal.so — The Unix Bridge

Single unix binary. No i386-unix version exists (Metal has no 32-bit macOS API). Works for both 32-bit and 64-bit PE clients:
- Exports `__wine_unix_call_funcs` for 64-bit
- Exports `__wine_unix_call_wow64_funcs` with full `thunk32_*` implementations for 32-bit
- Wine's ntdll loader looks for unix .so in `x86_64-unix/` using `current_machine` (host arch), not PE target machine

### 64-bit DLL Injection

The Rust backend copies DXMT PE DLLs into the game directory on every launch. This is needed because:
- 64-bit Wine builtins (`x86_64-windows/d3d11.dll`) are Wine's D3DMetal — Steam needs these
- DXMT must be loaded from game dir with `WINEDLLOVERRIDES=n,b`
- `winebuild --builtin` post-processing ensures proper Wine builtin marking

### MetalFX Spatial Upscaling

Enabled via `DXMT_METALFX_SPATIAL_SWAPCHAIN=1` env var. DXMT checks `supportsFXSpatialScaler()` on the MTLDevice. Config in `dxmt.conf`:
- `d3d11.metalSpatialUpscaleFactor = 2.0` — game renders at half resolution, MetalFX upscales to native
- Reduces GPU workload significantly on supported hardware (Apple M-series)

### Shader Cache

DXMT has two cache systems:

**Shader cache** (`DXMT_SHADER_CACHE_PATH`):
- Stores compiled Metal shaders in `.db` files (SQLite-based via WMT::CacheWriter/CacheReader)
- Per-game under `~/.metalsharp/shader-cache/<exename>/`
- File format: `shaders_<metal_version>.db` (e.g., `shaders_320.db`)
- `DXMT_SHADER_CACHE=0` disables
- Path must start with `/` — if set, DXMT uses it directly (no exe name subdirectory appended)

**Metal PSO cache** (automatic):
- Set in `dxgi.cpp` `InitializeMetalCachePath()` → `dxmt/<exename>/com.apple.metal`
- `DXMT_USE_DEFAULT_METAL_CACHE=1` skips custom path

### DXMT Config

File: `~/.metalsharp/runtime/wine/etc/dxmt.conf` (set via `DXMT_CONFIG_FILE` env var):

```
d3d11.metalSpatialUpscaleFactor = 2.0
d3d11.preferredMaxFrameRate = 60
d3d11.maxFeatureLevel = 12_1
```

All config options documented in DXMT source: `dxmt-src/docs/CUSTOMIZATION.md`

## DXVK + MoltenVK — Vulkan Pipeline

Fallback path. D3D → Vulkan → Metal. Extra translation hop but wider compatibility.

```
Game D3D11 calls
  → DXVK d3d11.dll (D3D → Vulkan)
    → MoltenVK (Vulkan → Metal)
      → Metal framework
```

### Why DXVK 1.10.3 (not 2.x)

DXVK 2.x requires Vulkan 1.3. MoltenVK supports Vulkan 1.1 (some 1.2). DXVK 1.10.3 is the last version compatible.

### DXVK Layout

```
~/.metalsharp/runtime/dxvk-1.10.3/
├── x32/   (32-bit: d3d11, d3d10core, d3d9, dxgi)
└── x64/   (64-bit: d3d11, d3d10core, d3d9, dxgi)
```

Not placed in Wine's builtin dirs. Copied into game directories when needed.

### MoltenVK

Installed via Homebrew (`brew install moltenvk`). Not bundled.

Env for DXVK pipeline:
```
VK_ICD_FILENAMES=/opt/homebrew/share/vulkan/icd.d/MoltenVK_icd.json
MVK_PRESENT_MODE=1
DXVK_FRAME_RATE=60
DXVK_ASYNC=1
```

## Comparison

| Aspect | DXMT | DXVK + MoltenVK |
|--------|------|-----------------|
| Translation | D3D → Metal (1 hop) | D3D → Vulkan → Metal (2 hops) |
| Latency | Lower | Higher |
| Shader path | DXBC/DXIL → MSL direct | DXBC → SPIR-V → MSL |
| Metal features | Direct access (Metal 3+) | Limited to MoltenVK surface |
| 32-bit support | Via WoW64 thunks | Via WINEDLLOVERRIDES |
| Vulkan needed | No | Yes |

## Current Usage

| Game | Pipeline | Why |
|------|----------|-----|
| Rain World | DXMT Metal | 64-bit D3D11, primary path |
| Schedule I | DXMT Metal | 64-bit D3D11, Unity |
| Subnautica BZ | DXMT Metal | 64-bit D3D11 |
| Nidhogg 2 | WineD3D OpenGL | 32-bit, no 32-bit Metal API |
| Portal 2 | Wine D3DMetal | Steam DRM, Wine builtins |
| RE4 | Wine D3DMetal | Steam DRM, blocked on DXMT injection |
