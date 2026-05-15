# Launch Architecture

MetalSharp's game launch system: the Rust backend (`launch.rs`) determines *what* to run and *how*, then the `metalsharp-wine` wrapper handles the low-level Wine invocation.

## Flow

```
User clicks "Play"
  → Electron renderer → IPC → Rust backend
    → launch_auto(appid) or launch_with_method(appid, method)
      → resolve engine: appid map → fallback to directory scan
      → dispatch to launch function
        → spawn process with env vars
          → process exec's metalsharp-wine (or mono)
            → wrapper sets up Wine env + selects backend
              → game runs
```

## Engine Types

`launch.rs` defines 10 engine types:

| Engine | When used | Launch method |
|--------|-----------|---------------|
| `DxmtMetal` | Rain World, Schedule I, Subnautica BZ, Undertale (64-bit D3D11) | Direct exe. DXMT DLLs injected into game dir. Shader cache + MetalFX. |
| `DxmtMetal12` | Future D3D12 games (RE4, Schedule I during setup) | Direct exe. Also injects d3d12.dll. |
| `Wined3d32` | Nidhogg 2 (32-bit games needing OpenGL) | Direct exe via Wine. No DLL injection — Wine builtin WineD3D. |
| `DxvkMetal32` | Portal 2, Goat Simulator (32-bit D3D9 games) | Direct exe via Wine. DXVK d3d9.dll injected into game dir. MoltenVK Vulkan→Metal. |
| `MetalsharpWine` | Legacy D3D9 games (`d3dx9_43.dll` marker) | Direct exe via bare Wine. No DLL overrides. |
| `SteamD3DMetalPerf` | Celeste, RE4, High on Life, ~70 Steam DRM games | `steam://run/` via Wine Steam with GPTK D3DMetal (WINEDLLPATH + DYLD pointing to GPTK). |
| `SteamMetalfx` | Elden Ring, Sekiro (Steam DRM + D3DMetal MetalFX) | `steam://run/` via Wine Steam with MetalFX env vars. |
| `SteamBare` | Among Us, Valheim (Steam DRM, no extras) | `steam://run/` via Wine Steam. No extra env vars. |
| `FnaArm64` | Terraria (native ARM64 .NET + FNA) | Native Mono ARM64. No Wine. |
| `FnaX86` | (reserved, not currently mapped) | Rosetta Mono x86. No Wine. |

## Engine Resolution

### 1. Hardcoded app ID map

~100 app IDs mapped directly in `get_engine_for_appid()`:

```rust
match appid {
    105600 => FnaArm64,            // Terraria
    504230 => SteamD3DMetalPerf,   // Celeste (was FnaX86)
    312520 | 375520 | 848450 => DxmtMetal, // Rain World, Undertale, Subnautica BZ
    3164500 => DxmtMetal,          // Schedule I
    535520 => Wined3d32,           // Nidhogg 2
    620 | 265930 => DxvkMetal32,   // Portal 2, Goat Simulator
    2050650 | 1583230 => SteamD3DMetalPerf, // RE4, High on Life
    391540 => SteamBare,           // (Steam DRM, no extras)
    945360 | 1139900 => SteamBare, // Among Us, etc.
    1245620 => SteamMetalfx,       // Elden Ring
    ...
}
```

### 2. Directory-based auto-detection

Unknown games get auto-detected by `detect_engine_from_dir()`:

| Marker | Detected as |
|--------|-------------|
| `.NET managed DLLs` (`*_Data/Managed/` dir, no native PE DLLs) | `FnaArm64` |
| `UnityPlayer.dll`, `GameAssembly.dll` | Unity → `SteamD3DMetalPerf` |
| `engine/` + `binaries/` dirs | Unreal Engine → `SteamMetalfx` |
| `.pak` files | Source/Unreal → `SteamD3DMetalPerf` |
| `engine/` + `content/` dirs | Unreal → `SteamMetalfx` |
| `.bdt` / `.bhd` files | FromSoftware → `SteamMetalfx` |
| `re_chunk_*` or RE config files | RE Engine → `SteamD3DMetalPerf` |
| `d3dx9_43.dll` | DirectX 9 → `MetalsharpWine` |
| `steam_api64.dll` or `steam_api.dll` | `SteamD3DMetalPerf` |
| Default fallback | `SteamD3DMetalPerf` |

## Launch Paths in Detail

### DxmtMetal (Rain World, Schedule I, Subnautica BZ, Undertale)

Direct exe launch. Copies DXMT PE DLLs into game directory, sets up shader cache and MetalFX:

```
metalsharp-wine "Schedule I.exe"
  → DYLD_FALLBACK_LIBRARY_PATH includes lib/wine/x86_64-unix + lib/dxmt/x86_64-unix
  → WINEDLLOVERRIDES="dxgi,d3d11,d3d10core,winemetal=n,b;gameoverlayrenderer,gameoverlayrenderer64=d"
  → DXMT_SHADER_CACHE_PATH=~/.metalsharp/shader-cache/dxmt-metal/<appid>/
  → DXMT_CONFIG_FILE=~/.metalsharp/runtime/wine/etc/dxmt.conf
  → DXMT_METALFX_SPATIAL_SWAPCHAIN=1
  → DLLs copied: d3d11.dll, dxgi.dll, d3d10core.dll, winemetal.dll
  → Wine loads DXMT d3d11.dll → Metal rendering
```

### DxvkMetal32 (Portal 2, Goat Simulator)

Direct exe via Wine with DXVK d3d9.dll injected into game's binary directory. Routes through MoltenVK:

```
# Portal 2
metalsharp-wine portal2.exe
  → working dir: game root
  → d3d9.dll copied to game bin/ dir from lib/dxvk/i386-windows/
  → WINEDLLOVERRIDES="d3d9=n,b;gameoverlayrenderer,gameoverlayrenderer64=d"
  → VK_ICD_FILENAMES=<ms_root>/etc/vulkan/icd.d/MoltenVK_icd.json
  → DXVK_STATE_CACHE_PATH=~/.metalsharp/shader-cache/dxvk-metal32/<appid>/
  → DYLD_FALLBACK_LIBRARY_PATH=<ms_root>/lib/wine/x86_64-unix

# Goat Simulator
metalsharp-wine GoatGame-Win32-Shipping.exe
  → working dir: Binaries/Win32/
  → d3d9.dll copied to Binaries/Win32/
  → same env vars
```

Per-appid binary path handling in `launch_dxvk_metal32()`:
- appid 620 (Portal 2): exe=`portal2.exe`, d3d9.dll → `bin/d3d9.dll`
- appid 265930 (Goat Simulator): exe=`GoatGame-Win32-Shipping.exe`, d3d9.dll → `Binaries/Win32/d3d9.dll`
- Other: d3d9.dll → game root

### Wined3d32 (Nidhogg 2)

Direct exe via Wine with minimal overrides. No Metal — uses OpenGL:

```
metalsharp-wine "Nidhogg 2.exe"
  → WINEDLLOVERRIDES="dxgi,d3d11=b;gameoverlayrenderer,gameoverlayrenderer64=d;steamclient64,steamclient=d"
  → SteamOverlayDisabled=1
  → No DXMT DLLs copied
  → Wine builtin WineD3D handles D3D11 → OpenGL
```

### MetalsharpWine (Legacy D3D9)

Bare Wine launch with no DLL overrides. Used for games with `d3dx9_43.dll` marker:

```
metalsharp-wine "game.exe"
  → DYLD_FALLBACK_LIBRARY_PATH=<ms_root>/lib/wine/x86_64-unix
  → No WINEDLLOVERRIDES
```

### SteamD3DMetalPerf (Celeste, RE4, High on Life, etc.)

Launches through Wine Steam client with GPTK D3DMetal loaded via WINEDLLPATH:

```
1. Check if Wine Steam running → start if needed (up to 60s wait with 2s polling)
2. metalsharp-wine steam://run/{appid}
   → WINEDLLPATH includes GPTK x86_64-windows + MS wine x86_64-windows
   → DYLD_FALLBACK_LIBRARY_PATH includes GPTK x86_64-unix + MS wine x86_64-unix
   → D3DM_ENABLE_ASYNC_COMMIT=1
   → D3DM_MULTITHREADED_INTERFACE_ENABLE=1
   → D3DM_IGNORE_D3D11_RENDER_BARRIERS=1
   → D3DM_SAMPLE_NAN_TO_ZERO=1
   → D3DM_FLUSH_POS_INF_TO_NAN=1
```

### SteamMetalfx (Elden Ring, Sekiro)

Same as SteamD3DMetalPerf but with additional MetalFX env vars:

```
metalsharp-wine steam://run/{appid}
   → D3DM_ENABLE_METALFX=1
   → D3DM_ENABLE_ASYNC_COMMIT=1
   → D3DM_MULTITHREADED_INTERFACE_ENABLE=1
   → D3DM_IGNORE_D3D11_RENDER_BARRIERS=1
   → D3DM_SAMPLE_NAN_TO_ZERO=1
   → D3DM_FLUSH_POS_INF_TO_NAN=1
   → MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE=1
   → MVK_ALLOW_METAL_FENCES=1
```

### FNA (Terraria)

No Wine. Native .NET runtime:

```
# Terraria (ARM64 native)
mono TerrariaLauncher.exe → FNA + SDL3 + Metal + FAudio
```

## Process Lifecycle

### Killing a game

`kill_game(appid)` searches for processes matching the game directory path:
1. `pgrep -a -f <game_dir>` — find processes in game dir
2. `kill -9 <pid>` — direct kill each match
3. `pkill -9 -f UnityCrashHandler` — cleanup stragglers

`kill(pid)` does simpler cleanup:
1. `kill -9 {pid}` — direct kill
2. `pkill -9 -P {pid}` — child processes
3. `pkill -9 -f UnityCrashHandler` — cleanup

### Stopping Steam

`stop_wine_steam()` in `steam.rs`:
1. `pkill -9 -f Steam.exe/steam.exe/steamservice.exe/steamwebhelper.exe/winedevice.exe`
2. Wait 2s
3. `pkill -9 -f wineserver`
4. `pkill -9 -f wineloader`
5. Wait 2s
6. If still running: `killall -9` for each target

## Game Directory Resolution

`resolve_game_dir(appid)` in `setup.rs`:
1. `~/.metalsharp/games/{appid}/` — MetalSharp's local copy (if `.metalsharp_prepared` marker exists)
2. Steam `common/` — parse `appmanifest_{appid}.acf` for `installdir`, look in `steamapps/common/{install_dir}/`
3. Search both macOS native Steam paths and Wine Steam prefix paths
4. Fallback: return `~/.metalsharp/games/{appid}/` if it exists

## Shader Cache

Shader caches are organized by engine type and appid:

| Engine | Path | Format |
|--------|------|--------|
| DxmtMetal / DxmtMetal12 | `~/.metalsharp/shader-cache/dxmt-metal/<appid>/` | DXMT shader `.db` files |
| DxvkMetal32 | `~/.metalsharp/shader-cache/dxvk-metal32/<appid>/` | DXVK state cache |

- Each DXMT game gets its own shader `.db` files (Metal version-specific)
- First launch builds cold cache (stutters expected)
- Subsequent launches reuse cached shaders
- Safe to delete entire `shader-cache/` to force rebuild
