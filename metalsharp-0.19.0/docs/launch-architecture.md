# Launch Architecture

MetalSharp's game launch system: the Rust backend (`launch.rs`) determines *what* to run and *how*, then the `metalsharp-wine` wrapper handles the low-level Wine invocation.

## Flow

```
User clicks "Play"
  → Electron renderer → IPC → Rust backend
    → launch_auto(appid)
      → resolve engine: appid map → fallback to directory scan
      → dispatch to launch function
        → spawn process with env vars
          → process exec's metalsharp-wine (or mono)
            → wrapper sets up Wine env + selects backend
              → game runs
```

## Engine Types

`launch.rs` defines 8 engine types:

| Engine | When used | Launch method |
|--------|-----------|---------------|
| `DxmtMetal` | Rain World, Schedule I, Subnautica BZ (64-bit D3D11) | Direct exe. DXMT DLLs injected into game dir. Shader cache + MetalFX. |
| `DxmtMetal12` | Future D3D12 games | Direct exe. Also injects d3d12.dll. Currently unused. |
| `Wined3d32` | Nidhogg 2 (32-bit games) | Direct exe via Wine. No DLL injection — Wine builtin WineD3D. |
| `SteamD3DMetalPerf` | Portal 2, RE4, ~50 other Steam DRM games | `steam://run/` via Wine Steam with D3DM perf env vars. |
| `SteamMetalfx` | Elden Ring, Sekiro (Steam DRM + D3DMetal MetalFX) | `steam://run/` via Wine Steam with MetalFX env vars. |
| `SteamBare` | Among Us, SteamVR (Steam DRM, no extras) | `steam://run/` via Wine Steam. No extra env vars. |
| `FnaArm64` | Terraria (native ARM64 .NET + FNA) | Native Mono ARM64. No Wine. |
| `FnaX86` | Celeste (x86 .NET + FNA + FMOD) | Rosetta Mono x86. No Wine. |

## Engine Resolution

### 1. Hardcoded app ID map

~100 app IDs mapped directly in `get_engine_for_appid()`:

```rust
match appid {
    105600 => FnaArm64,           // Terraria
    504230 => FnaX86,             // Celeste
    312520 | 848450 => DxmtMetal, // Rain World, Subnautica BZ
    3164500 => DxmtMetal,         // Schedule I
    535520 | 391540 => Wined3d32, // Nidhogg 2
    620 => SteamD3DMetalPerf,     // Portal 2
    2050650 => SteamD3DMetalPerf, // RE4
    1245620 => SteamMetalfx,      // Elden Ring
    ...
}
```

### 2. Directory-based auto-detection

Unknown games get auto-detected by `detect_engine_from_dir()`:

| Marker | Detected as |
|--------|-------------|
| `UnityPlayer.dll`, `GameAssembly.dll` | Unity → `SteamD3DMetalPerf` |
| `engine/` + `binaries/` dirs | Unreal Engine → `SteamMetalfx` |
| `.pak` files | Source/Unreal → `SteamD3DMetalPerf` |
| `engine/` + `content/` dirs | Unreal → `SteamMetalfx` |
| `.bdt` / `.bhd` files | FromSoftware → `SteamMetalfx` |
| `re_chunk_*` or RE config files | RE Engine → `SteamD3DMetalPerf` |
| `d3dx9_43.dll` | DirectX 9 → `MetalsharpWine` |
| Default fallback | `SteamD3DMetalPerf` |

## Launch Paths in Detail

### DxmtMetal (Rain World, Schedule I, Subnautica BZ)

Direct exe launch. Copies DXMT PE DLLs into game directory, sets up shader cache and MetalFX:

```
metalsharp-wine "Schedule I.exe"
  → DYLD_FALLBACK_LIBRARY_PATH includes dxmt/x86_64-unix
  → WINEDLLOVERRIDES="dxgi,d3d11,d3d10core,winemetal=n,b"
  → DXMT_SHADER_CACHE_PATH=~/.metalsharp/shader-cache/Schedule I.exe/
  → DXMT_CONFIG_FILE=~/.metalsharp/runtime/wine/etc/dxmt.conf
  → DXMT_METALFX_SPATIAL_SWAPCHAIN=1
  → DLLs copied: d3d11.dll, dxgi.dll, d3d10core.dll, winemetal.dll
  → Wine loads DXMT d3d11.dll → Metal rendering
```

### Wined3d32 (Nidhogg 2)

Direct exe via Wine with minimal overrides. No Metal — uses OpenGL:

```
metalsharp-wine "Nidhogg 2.exe"
  → WINEDLLOVERRIDES="dxgi,d3d11=b;gameoverlayrenderer,gameoverlayrenderer64=d"
  → No DXMT DLLs copied
  → Wine builtin WineD3D handles D3D11 → OpenGL
```

### SteamD3DMetalPerf (Portal 2, RE4, etc.)

Launches through Wine Steam client with performance env vars:

```
1. Check if Wine Steam running → start if needed (up to 60s wait)
2. metalsharp-wine steam://run/{appid}
  → D3DM_ENABLE_ASYNC_COMMIT=1
  → D3DM_MULTITHREADED_INTERFACE_ENABLE=1
  → D3DM_IGNORE_D3D11_RENDER_BARRIERS=1
  → D3DM_SAMPLE_NAN_TO_ZERO=1
  → D3DM_FLUSH_POS_INF_TO_NAN=1
  → MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE=1
  → MVK_ALLOW_METAL_FENCES=1
```

### FNA (Terraria, Celeste)

No Wine. Native .NET runtime:

```
# Terraria (ARM64 native)
mono TerrariaLauncher.exe → FNA + SDL3 + Metal + FAudio

# Celeste (x86 via Rosetta)
arch -x86_64 mono Celeste.exe → FNA + SDL3 + FMOD stubs
```

## Process Lifecycle

### Killing a game

`kill_game(appid)` does multi-pass cleanup:
1. `kill -9 {pid}` — direct kill
2. `pkill -9 -P {pid}` — child processes
3. `pkill -9 -f "metalsharp.*{appid}"` — stragglers
4. Optional: `pkill -9 -f "{exe_name}"` — name-based

### Stopping Steam

`stop_wine_steam()` in `steam.rs`:
1. `killall Steam.exe steam.exe`
2. `wineserver -k`
3. `killall wineloader wineserver`
4. Retry loop

## Game Directory Resolution

1. `~/.metalsharp/games/{appid}/` — MetalSharp's local copy (FNA games)
2. Steam `common/` — `{prefix}/drive_c/Program Files (x86)/Steam/steamapps/common/{install_dir}/`
3. Appmanifest parsing — `appmanifest_{appid}.acf` contains `InstallDir`

## Shader Cache

DXMT shader cache is per-game under `~/.metalsharp/shader-cache/<exename>/`:
- Each game gets its own `shaders_320.db` (Metal 3.2.0 shaders)
- First launch builds cold cache (stutters expected)
- Subsequent launches reuse cached shaders
- Safe to delete entire `shader-cache/` to force rebuild
