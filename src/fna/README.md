# FNA Integration for MetalSharp

Native macOS game support via FNA (XNA reimplementation) + SDL3 + Metal.

## Components

- `FNA/` — FNA C# library (git clone of FNA-XNA/FNA, net4.0 build for Mono)
- `FNA3D/` — FNA3D native graphics library (git clone, built with SDL3 backend → Metal)
- `shims/` — Native library shims for running Windows .NET games on macOS

## Shims

### CSteamworks Shim (`csteamworks_shim.c`)

Bridges old Steamworks.NET v10 P/Invoke calls (via `CSteamworks.dll`) to modern
macOS `libsteam_api.dylib`. Handles:

- `RestartAppIfNecessary` → returns false (skip Steam relaunch)
- `Init` → wraps `SteamAPI_InitFlat` to match old `bool Init()` signature
- `ISteamApps_*` / `ISteamUserStats_*` → manages interface pointers internally
  (old CSteamworks.dll hid these; modern Steam API requires explicit passing)

Build: `./build_csteamworks.sh` (outputs `libCSteamworks.dylib`)

### FMOD Stubs (`fmod_stub.c`, `fmodstudio_stub.c`)

No-op stubs for FMOD Studio 1.10.x audio engine. FMOD 1.10 has no arm64 macOS
builds, so these stubs allow the game to run silently without audio.

Build: `./build_fmod_stubs.sh` (outputs `libfmod.dylib`, `libfmodstudio.dylib`)

## Game Setup (Celeste example)

1. Install game via steamcmd: `steamcmd +login USER PASS +@sSteamCmdForcePlatformType windows +app_update 504230 validate +quit`
2. Copy Celeste.exe + Content/ to game dir
3. Build FNA as net4.0, copy as all XNA assembly names
4. Build FNA3D with SDL3 backend
5. Copy SDL3, CSteamworks shim, FMOD stubs, steam_appid.txt to game dir
6. Launch: `METAL_DEVICE_WRAPPER_TYPE=0 DYLD_LIBRARY_PATH=. mono Celeste.exe`

## Requirements

- Mono 6.x+
- SDL3 (homebrew)
- Steam client running (for Steamworks)
