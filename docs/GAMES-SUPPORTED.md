# Games Supported — MetalSharp v0.17.0

## Rendering Engines

| Engine | Method | How It Works |
|---|---|---|
| **DxmtMetal** | Direct exe via Wine | DXMT D3D11/DXGI Metal-native. DLLs injected into game dir. Per-game shader cache + MetalFX upscaling. No GPTK needed. |
| **Wined3d32** | Direct exe via Wine | WineD3D OpenGL for 32-bit games. No Metal on macOS for 32-bit. |
| **SteamD3DMetalPerf** | `steam://run/` via Wine Steam | Steam DRM games. Launches through Wine Steam client with D3DM perf env vars. Uses our Wine runtime (MoltenVK, freetype, gnutls). |
| **SteamMetalfx** | `steam://run/` via Wine Steam | Steam DRM games with D3DMetal MetalFX env vars. |
| **SteamBare** | `steam://run/` via Wine Steam | Steam DRM games. No extra env vars. |
| **FnaArm64** | Direct exe via .NET ARM64 | FNA/XNA games. Native ARM64 .NET runtime. |
| **FnaX86** | Direct exe via .NET x86 | FNA/XNA games. x86 .NET via Wine. |

---

## Confirmed Working

These games have been tested and confirmed running on MetalSharp.

### Rain World
- **AppID:** 312520
- **Engine:** DxmtMetal (D3D11 → Metal via DXMT)
- **Launch:** Direct exe. DXMT injects `d3d11.dll`, `dxgi.dll`, `d3d10core.dll`, `winemetal.dll` into game dir. Per-game shader cache at `~/.metalsharp/shader-cache/`. MetalFX 2x spatial upscaling enabled.
- **Recommended Settings:** Default preset / Medium settings, V-Sync ON

### Schedule I
- **AppID:** 3164500
- **Engine:** DxmtMetal (D3D11 → Metal via DXMT)
- **Launch:** Direct exe. Same DXMT injection as Rain World. Per-game shader cache + MetalFX upscaling.
- **Recommended Settings:** Low preset, SSAO enabled, V-Sync ON, God Rays OFF

### Subnautica: Below Zero
- **AppID:** 848450
- **Engine:** DxmtMetal (D3D11 → Metal via DXMT)
- **Launch:** Direct exe. Same DXMT injection. Per-game shader cache + MetalFX upscaling.
- **Recommended Settings:** Low preset, V-Sync ON, FXAA

### Nidhogg 2
- **AppID:** 535520
- **Engine:** Wined3d32 (D3D11 → OpenGL via WineD3D)
- **Launch:** Direct exe via Wine. No DLL injection — uses Wine's builtin WineD3D. 32-bit game, no Metal API available on macOS.
- **Recommended Settings:** High settings, V-Sync ON

### Portal 2
- **AppID:** 620
- **Engine:** SteamD3DMetalPerf (via Wine Steam)
- **Launch:** `steam://run/620` through Wine Steam client. Uses our Wine runtime with DYLD paths for MoltenVK/freetype/gnutls. D3DM async commit + multithreaded interface enabled.
- **Recommended Settings:** Low/Medium settings, V-Sync ON

---

## In Progress

### Resident Evil 4 (2023)
- **AppID:** 2050650
- **Engine:** SteamD3DMetalPerf (mapped)
- **Status:** Blocked by Steam DRM. Game requires `steam://run/` launch. Direct exe fails (missing `steam_api64.dll`). DXMT injection through Steam launch path not yet implemented. Game's own D3D12 mode forced to D3D11 via `local_config.ini` (chmod 444 to prevent overwrite).

---

## Auto-Detection Fallback

Games not explicitly mapped are auto-detected by scanning the game directory:

| Detection | Engine |
|---|---|
| `UnityPlayer.dll` or `GameAssembly.dll` present | SteamD3DMetalPerf |
| `engine/` + `binaries/` dirs (Unreal Engine) | SteamMetalfx |
| `.pak` files (Source/Unreal) | SteamD3DMetalPerf |
| `engine/` + `content/` dirs | SteamMetalfx |
| `.bdt` / `.bhd` files (Dark Souls / FromSoft) | SteamMetalfx |
| `re_chunk_` or RE config files | SteamD3DMetalPerf |
| `d3dx9_43.dll` (legacy D3D9) | MetalsharpWine |
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
- `DXMT_SHADER_CACHE_PATH` — per-game cache dir under `~/.metalsharp/shader-cache/<exename>/`
- `DXMT_CONFIG_FILE` — points to `dxmt.conf`
- `DXMT_METALFX_SPATIAL_SWAPCHAIN=1` — enables MetalFX spatial upscaling

---

## Notes

- First launch of any DxmtMetal game will stutter while the shader cache builds. Subsequent launches are smoother.
- All games use the shared Wine prefix at `~/.metalsharp/prefix-steam/`.
- Steam library on external SSD should be mapped as F: drive in Wine prefix.
- Shader caches are per-game and persist across launches. Safe to delete `~/.metalsharp/shader-cache/` to force rebuild.
