# Games Supported — MetalSharp v0.22.0

## Rendering Engines

| Engine | Method | How It Works |
|---|---|---|
| **DxmtMetal** | Direct exe via Wine | DXMT D3D11/DXGI Metal-native. DLLs injected into game dir. Per-game shader cache + MetalFX upscaling. No GPTK needed. |
| **DxmtMetal12** | Direct exe via Wine | DXMT D3D12/D3D11/DXGI Metal-native. Same as DxmtMetal with additional d3d12.dll injection. |
| **DxvkMetal32** | Direct exe via Wine | DXVK d3d9→MoltenVK→Metal for 32-bit D3D9 games. d3d9.dll injected into game's binary dir. Per-game state cache. |
| **Wined3d32** | Direct exe via Wine | WineD3D OpenGL for 32-bit games. No Metal on macOS for 32-bit D3D11. |
| **MetalsharpWine** | Direct exe via Wine | Bare Wine launch. No DLL overrides. For legacy D3D9 games. |
| **SteamD3DMetalPerf** | `steam://run/` via Wine Steam | Steam DRM games. Launches through Wine Steam with GPTK's D3DMetal loaded via WINEDLLPATH. D3DM perf env vars. |
| **SteamMetalfx** | `steam://run/` via Wine Steam | Steam DRM games with D3DMetal MetalFX env vars. |
| **SteamBare** | `steam://run/` via Wine Steam | Steam DRM games. No extra env vars. |
| **FnaArm64** | Direct exe via .NET ARM64 | FNA/XNA games. Native ARM64 .NET runtime. |

---

## Confirmed Working

These games have been tested and confirmed running on MetalSharp.

### Rain World
- **AppID:** 312520
- **Engine:** DxmtMetal (D3D11 → Metal via DXMT)
- **Launch:** Direct exe. DXMT injects `d3d11.dll`, `dxgi.dll`, `d3d10core.dll`, `winemetal.dll` into game dir. Per-appid shader cache at `~/.metalsharp/shader-cache/dxmt-metal/312520/`. MetalFX 2x spatial upscaling enabled.
- **Recommended Settings:** Default preset / Medium settings, V-Sync ON

### Schedule I
- **AppID:** 3164500
- **Engine:** DxmtMetal (D3D11 → Metal via DXMT)
- **Launch:** Direct exe. Same DXMT injection as Rain World. Per-appid shader cache + MetalFX upscaling.
- **Recommended Settings:** Low preset, SSAO enabled, V-Sync ON, God Rays OFF

### Subnautica: Below Zero
- **AppID:** 848450
- **Engine:** DxmtMetal (D3D11 → Metal via DXMT)
- **Launch:** Direct exe. Same DXMT injection. Per-appid shader cache + MetalFX upscaling.
- **Recommended Settings:** Low preset, V-Sync ON, FXAA

### Undertale
- **AppID:** 375520
- **Engine:** DxmtMetal (D3D11 → Metal via DXMT)
- **Launch:** Direct exe. Same DXMT injection. Per-appid shader cache + MetalFX upscaling.
- **Recommended Settings:** Default

### Portal 2
- **AppID:** 620
- **Engine:** DxvkMetal32 (D3D9 → Vulkan → Metal via DXVK + MoltenVK)
- **Launch:** Direct exe via Wine. DXVK `d3d9.dll` injected into `bin/` dir. MoltenVK bundled in wine runtime. VK_ICD_FILENAMES set to bundled ICD. Per-appid state cache at `~/.metalsharp/shader-cache/dxvk-metal32/620/`.
- **Recommended Settings:** Low/Medium settings, V-Sync ON

### Goat Simulator
- **AppID:** 265930
- **Engine:** DxvkMetal32 (D3D9 → Vulkan → Metal via DXVK + MoltenVK)
- **Launch:** Direct exe via Wine. DXVK `d3d9.dll` injected into `Binaries/Win32/`. Working dir set to `Binaries/Win32/`. Same MoltenVK setup as Portal 2.
- **Recommended Settings:** Low settings, V-Sync ON

### Nidhogg 2
- **AppID:** 535520
- **Engine:** Wined3d32 (D3D11 → OpenGL via WineD3D)
- **Launch:** Direct exe via Wine. No DLL injection — uses Wine's builtin WineD3D. 32-bit game, no Metal API available on macOS. Steam overlay disabled.
- **Recommended Settings:** High settings, V-Sync ON

### Celeste
- **AppID:** 504230
- **Engine:** SteamD3DMetalPerf (via Wine Steam + GPTK D3DMetal)
- **Launch:** `steam://run/504230` through Wine Steam client. WINEDLLPATH prepended with GPTK's d3d11.dll path. D3DM async commit + multithreaded interface enabled. Previously used FnaX86 — now uses Steam DRM + GPTK D3DMetal.
- **Recommended Settings:** Default

### Terraria
- **AppID:** 105600
- **Engine:** FnaArm64 (Native .NET ARM64 + FNA + SDL3)
- **Launch:** Native Mono ARM64. No Wine. FNA + SDL3 + Metal rendering. macOS Steam libraries (libsteam_api.dylib, SDL3, FAudio, FNA3D) copied from macOS Terraria install. Custom launcher (`TerrariaLauncher.exe`) built from source.
- **Recommended Settings:** Default

---

## In Progress

### Resident Evil 4 (2023)
- **AppID:** 2050650
- **Engine:** SteamD3DMetalPerf (mapped)
- **Status:** Crashes with GPTK D3DMetal. Launches via `steam://run/` with D3DM perf env vars. UE4 compatibility limitation under investigation.

### High on Life
- **AppID:** 1583230
- **Engine:** SteamD3DMetalPerf (mapped)
- **Status:** Crashes after loading screen with GPTK D3DMetal. Squanch Games UE4 compatibility issue.

---

## Auto-Detection Fallback

Games not explicitly mapped are auto-detected by scanning the game directory:

| Detection | Engine |
|---|---|
| `.NET managed DLLs` (`*_Data/Managed/` dir, no native PE DLLs) | FnaArm64 |
| `UnityPlayer.dll` or `GameAssembly.dll` present | SteamD3DMetalPerf |
| `engine/` + `binaries/` dirs (Unreal Engine) | SteamMetalfx |
| `.pak` files (Source/Unreal) | SteamD3DMetalPerf |
| `engine/` + `content/` dirs | SteamMetalfx |
| `.bdt` / `.bhd` files (Dark Souls / FromSoft) | SteamMetalfx |
| `re_chunk_` or RE config files | SteamD3DMetalPerf |
| `d3dx9_43.dll` (legacy D3D9) | MetalsharpWine |
| `steam_api64.dll` or `steam_api.dll` | SteamD3DMetalPerf |
| Default fallback | SteamD3DMetalPerf |

---

## DXMT Configuration

Config file: `~/.metalsharp/runtime/wine/etc/dxmt.conf`

```
d3d11.metalSpatialUpscaleFactor = 2.0
d3d11.preferredMaxFrameRate = 60
d3d11.maxFeatureLevel = 12_1
```

Environment variables set for DxmtMetal games:
- `DXMT_SHADER_CACHE_PATH` — per-appid cache dir under `~/.metalsharp/shader-cache/dxmt-metal/<appid>/`
- `DXMT_CONFIG_FILE` — points to `dxmt.conf`
- `DXMT_METALFX_SPATIAL_SWAPCHAIN=1` — enables MetalFX spatial upscaling

## DXVK Configuration

Environment variables set for DxvkMetal32 games:
- `VK_ICD_FILENAMES` — points to bundled MoltenVK ICD at `~/.metalsharp/runtime/wine/etc/vulkan/icd.d/MoltenVK_icd.json`
- `DXVK_STATE_CACHE_PATH` — per-appid cache dir under `~/.metalsharp/shader-cache/dxvk-metal32/<appid>/`
- `WINEDLLOVERRIDES=d3d9=n,b` — loads DXVK d3d9 from game dir

---

## Notes

- First launch of any DxmtMetal game will stutter while the shader cache builds. Subsequent launches are smoother.
- All games use the shared Wine prefix at `~/.metalsharp/prefix-steam/` unless game-specific setup creates a separate one.
- Steam library on external SSD should be mapped as F: drive in Wine prefix.
- Shader caches are per-appid and persist across launches. Safe to delete `~/.metalsharp/shader-cache/` to force rebuild.
- Portal 2 and Goat Simulator require DXVK + MoltenVK — no GPTK needed for these.
- Celeste no longer uses the FNA path — it launches through Steam DRM + GPTK D3DMetal.
