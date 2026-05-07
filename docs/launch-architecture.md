# Launch Architecture

MetalSharp's game launch system is a two-layer dispatch: the Rust backend (`launch.rs`) determines *what* to run and *how*, then the `metalsharp-wine` wrapper handles the low-level Wine invocation with the correct graphics backend.

## Flow

```
User clicks "Play"
  → Electron renderer → IPC → Rust backend
    → launch_auto(appid)
      → resolve engine: appid map → fallback to directory scan
      → dispatch to launch function
        → spawn process with env vars
          → process exec's metalsharp-wine (or mono, or GPTK wine64)
            → wrapper sets up Wine env + selects backend via MS_BACKEND
              → game runs
```

## Engine types

`launch.rs` defines 7 engine types. Each maps to a distinct launch path:

| Engine | When used | Launch function |
|--------|-----------|-----------------|
| `FnaArm64` | Terraria (native Mono arm64 + FNA) | `launch_fna_arm64()` |
| `FnaX86` | Celeste (Rosetta Mono x86 + FNA + FMOD) | `launch_fna_x86()` |
| `GptkWine` | Rain World (Apple GPTK D3D→Metal) | `launch_gptk()` |
| `MetalsharpWine` | Nidhogg 2, Undertale (DXMT/wined3d builtins) | `launch_metalsharp_wine()` |
| `SteamBare` | RE4, Among Us, Ghostrunner (Steam DRM launch) | `launch_via_steam()` |
| `SteamMetalfx` | Elden Ring (Steam + MetalFX upscaling) | `launch_via_steam_with_env()` |
| `SteamD3DMetalPerf` | DREDGE, Sons of the Forest (Steam + D3DMetal perf flags) | `launch_via_steam_with_env()` |

## Engine resolution

### 1. Hardcoded app ID map

~100 app IDs are mapped directly to engine types in `get_engine_for_appid()`. This is the fastest path — no filesystem access needed.

```rust
match appid {
    105600 => FnaArm64,           // Terraria
    504230 => FnaX86,             // Celeste
    312520 => GptkWine,           // Rain World
    535520 => MetalsharpWine,     // Nidhogg 2
    391540 => MetalsharpWine,     // Undertale
    2050650 => SteamBare,         // RE4
    945360 => SteamBare,          // Among Us
    1245620 => SteamMetalfx,      // Elden Ring
    ...
}
```

### 2. Directory-based detection

Unknown games get auto-detected by scanning their install directory. `detect_engine_from_dir()` looks for engine fingerprint files:

| Marker | Detected as |
|--------|-------------|
| `UnityPlayer.dll`, `Assembly-CSharp.dll` | Unity → `SteamD3DMetalPerf` |
| `*.pak` files, `Engine/Binaries/Win64/` | Unreal Engine → `SteamD3DMetalPerf` |
| `*.bdt`, `*.bhd`, `dinput8.dll` | FromSoftware → `SteamMetalfx` |
| `re_chunk_*.pak` | RE Engine → `SteamBare` |
| `FNA.dll`, `MonoGame.Framework.dll` | FNA → `FnaX86` |
| `d3dx9_43.dll` | DirectX 9 → `MetalsharpWine` |
| None of the above | Default → `SteamD3DMetalPerf` |

## Launch paths in detail

### Steam DRM launch (`SteamBare`, `SteamMetalfx`, `SteamD3DMetalPerf`)

Games with Steam DRM must launch through the Steam client. MetalSharp starts Windows Steam if needed, waits up to 60 seconds for it to become ready, then invokes `steam://run/{appid}` via Wine.

```
metalsharp-wine steam://run/2050650
  → Wine opens Steam protocol URL
    → Steam authenticates DRM
      → Steam launches the game executable
```

For `SteamMetalfx` and `SteamD3DMetalPerf`, additional env vars are injected:

```rust
// SteamMetalfx (Elden Ring)
METALFX_ENABLED=1

// SteamD3DMetalPerf (DREDGE, Sons of the Forest)
D3DMetalAsyncCommit=1
D3DMetalMultithreadedD3D=1
D3DMetalSkipRenderBarriers=1
D3DMetalNanSafety=1
```

### MetalsharpWine (Nidhogg 2, Undertale)

Launches via the `metalsharp-wine` wrapper with `MS_BACKEND` set. The wrapper injects DXMT DLLs into the game directory and sets `WINEDLLOVERRIDES=d3d11,dxgi,d3d10core=native`.

```
MS_BACKEND=dxmt MS_GAME_DIR=~/.metalsharp/games/535520 metalsharp-wine "Nidhogg 2.exe"
  → wrapper copies DXMT i386 DLLs to game dir
    → sets WINEDLLOVERRIDES for native d3d11/dxgi
      → Wine loads DXMT's d3d11.dll
        → DXMT → winemetal.so → Metal
```

For Undertale (a simple D3D9 game), no backend is needed — Wine's built-in wined3d handles it directly.

### GPTK launch (Rain World)

Bypasses MetalSharp Wine entirely. Uses Apple's Game Porting Toolkit Wine binary directly:

```
arch -x86_64 /Applications/Game Porting Toolkit.app/.../wine64 <game.exe>
  → GPTK's D3DMetal.framework handles D3D→Metal translation
```

GPTK has its own D3DMetal implementation separate from DXMT. It's used for games that work better with Apple's translation layer.

### FNA launch (Celeste, Terraria)

No Wine at all. Native Mono + FNA + SDL3:

```
# Celeste (x86_64 via Rosetta)
arch -x86_64 mono Celeste.exe
  → FNA (XNA reimplementation) + SDL3 (windowing/input) + Metal (rendering)
  → FMOD 1.10 stubs for audio (x86_64 dylibs)

# Terraria (arm64 native)
mono TerrariaLauncher.exe
  → FNA + SDL3 + Metal + FAudio (real audio via arm64 libFAudio.dylib)
```

## Process lifecycle

### Starting a game

1. Electron sends `launch-game` IPC with app ID
2. Rust calls `launch_auto(appid)` → returns `(pid, method_name)`
3. Rust spawns the process in a background thread
4. PID is tracked for kill/uninstall operations

### Killing a game

`kill_game(appid)` does a multi-pass cleanup:
1. `kill -9 {pid}` — direct kill
2. `pkill -9 -P {pid}` — kill child processes (Unity crash handlers, etc.)
3. `pkill -9 -f "metalsharp.*{appid}"` — catch any stragglers
4. Optional: `pkill -9 -f "{exe_name}"` — name-based cleanup

### Stopping Steam

`stop_wine_steam()` in `steam.rs` does aggressive cleanup:
1. `killall Steam.exe steam.exe` — kill Steam processes
2. `wineserver -k` — tell wineserver to terminate
3. `killall wineloader wineserver` — force kill remaining Wine processes
4. Retry loop with `sleep 1` between passes

## Game directory resolution

Games are found in this priority order:

1. `~/.metalsharp/games/{appid}/` — MetalSharp's local copy (FNA games)
2. Steam `common/` directory — `{prefix}/drive_c/Program Files (x86)/Steam/steamapps/common/{install_dir}/`
3. Appmanifest parsing — `appmanifest_{appid}.acf` contains `InstallDir` key

For Steam DRM games, MetalSharp reads the appmanifest to find the install directory, then scans for the game executable.

## Setup scripts

Some games need pre-launch preparation (shim builds, DLL staging, config generation). `run_game_setup_script()` in `launch.rs` dispatches to per-game shell scripts in `~/.metalsharp/scripts/`:

| Script | What it does |
|--------|-------------|
| `setup-celeste-deps.sh` | Builds CSteamworks shim (x86), FMOD stubs, copies FNA/SDL3 dylibs |
| `setup-terraria-deps.sh` | Builds FAudio, gdiplus stubs, generates mono config |
| `setup-rainworld-deps.sh` | Copies GPTK D3DMetal dylibs into game directory |
| `setup-nidhogg2-deps.sh` | Stages DXVK/DXMT DLLs for 32-bit D3D11 |
| `setup-amongus-deps.sh` | Minimal setup — game works via Steam DRM |

Scripts are only run once — they check for the existence of their output files before doing work.
