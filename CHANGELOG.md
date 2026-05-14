# Changelog

## v0.23.0 — 2026-05-14

Goldberg Steam emulator integration for DxvkMetal32 games.

### Added

- **Goldberg Steam emulator** — automatic download and deployment for Portal 2 (appid 620) and Goat Simulator (appid 265930). Caches DLLs at `~/.metalsharp/runtime/goldberg/{x86,x64}/`, deploys per-game with `steam_settings/force_steam_appid.txt`
- **`scripts/setup-goldberg-deps.sh`** — generalized setup script replacing `setup-portal2-deps.sh`. Downloads from Detanup01/gbe_fork (fallback to official GitLab build), auto-resolves game dir from Steam appmanifests
- **Rust-native Goldberg download** — `ensure_goldberg_downloaded()` in setup.rs handles download + extraction without requiring the bash script
- **Launch-time Goldberg deploy** — `deploy_goldberg_for_launch()` called in `launch_dxvk_metal32()` as safety net, auto-deploys if DLLs missing

### Changed

- **Portal 2 game_type** — fixed from `"wine_devel"` to `"dxvk_metal32"` to match actual engine routing
- **WINEDLLOVERRIDES for DxvkMetal32** — added `steamclient,steamclient64=d` to prevent Wine's Steam client DLLs from interfering with Goldberg
- **`prepare_portal_2()`** — generalized to `prepare_goldberg_game(game_dir, home, appid)` supporting both Portal 2 and Goat Simulator
- **`run_game_setup_script()`** — appids 620 and 265930 now route to `setup-goldberg-deps.sh`

## v0.22.0 — 2026-05-14

Native engine build, DXVK MoltenVK for 32-bit D3D9, engine routing overhaul.

### Added

- **Native C++ engine** — full D3D11, D3D12, DXGI, XAudio2, and XInput implementations built via CMake (`metalsharp_core`, `metalsharp_d3d11`, `metalsharp_d3d12`, `metalsharp_dxgi`, `metalsharp_audio`, `metalsharp_input` libraries). Loads PE binaries directly via native loader without Wine
- **`DxvkMetal32` engine** — DXVK d3d9.dll injected into game directory, routed through MoltenVK Vulkan→Metal. Per-game binary path handling (Portal 2: `bin/portal2.exe`, Goat Simulator: `Binaries/Win32/GoatGame-Win32-Shipping.exe`)
- **`MetalsharpWine` engine** — bare Wine launch with no DLL overrides. Used for legacy D3D9 games detected by `d3dx9_43.dll` marker
- **Native launcher** — `metalsharp_launcher` and `metalsharp_native` executables in `tools/launcher/` for running games without the Electron app
- **Bundled MoltenVK ICD** — Vulkan ICD manifest at `etc/vulkan/icd.d/MoltenVK_icd.json` in wine runtime, DxvkMetal32 uses this instead of Homebrew path
- **DXVK state cache** — `DXVK_STATE_CACHE_PATH` set per-appid under `~/.metalsharp/shader-cache/dxvk-metal32/<appid>/`
- **Shader cache per-appid** — DXMT shader cache path changed from `~/.metalsharp/shader-cache/<exename>/` to `~/.metalsharp/shader-cache/dxmt-metal/<appid>/`
- **HTTP backend uses tiny_http** — replaced Actix with tiny_http for lighter dependency footprint
- **Updater module** — `updater.rs` for self-update DMG download and install
- **Test suite expanded** — 20+ test files covering D3D11, D3D12, DXBC parsing, Metal device, audio, input, tiled resources, and phases 2–24

### Changed

- **Celeste (appid 504230) → SteamD3DMetalPerf** — was FnaX86. Steam Celeste uses Steam DRM, launches via GPTK D3DMetal
- **Portal 2 (appid 620) → DxvkMetal32** — was SteamD3DMetalPerf. Now uses DXVK d3d9→MoltenVK→Metal, no GPTK needed
- **Goat Simulator (appid 265930) → DxvkMetal32** — was SteamD3DMetalPerf. 32-bit UE3 D3D9 via DXVK
- **Undertale (appid 375520) → DxmtMetal** — mapped alongside Rain World and Subnautica BZ
- **Among Us (appid 945360), Valheim (appid 892970) → SteamBare** — Steam DRM, no extra env vars
- **Wined3d32** now sets `steamclient64,steamclient=d` in WINEDLLOVERRIDES and `SteamOverlayDisabled=1`
- **DxmtMetal WINEDLLOVERRIDES** includes `gameoverlayrenderer,gameoverlayrenderer64=d`
- **Steam launch wait** — increased to 60s with 2s polling interval for `launch_via_steam_with_env()`
- **DXVK i386-windows path** — DXVK 32-bit DLLs located at `lib/dxvk/i386-windows/` in wine runtime (not `lib/wine/i386-windows/`)

### Removed

- **`Engine::FnaX86` active routing** — Celeste no longer uses FnaX86 path (FnaX86 enum variant still exists but no game maps to it)
- **Actix dependency** — replaced by tiny_http

## v0.18.0 — 2026-05-12

Performance optimizations, GPTK D3DMetal integration for Steam DRM games, 7 games confirmed working.

### Added

- **GPTK D3DMetal for Steam DRM games** — `SteamD3DMetalPerf` now sets `WINEDLLPATH` to include GPTK's `x86_64-windows/` dir and `DYLD_FALLBACK_LIBRARY_PATH` to include GPTK's `x86_64-unix/`, loading Apple's D3DMetal d3d11.dll instead of WineD3D for Steam-launched games
- **Per-game DXMT shader cache** — `DXMT_SHADER_CACHE_PATH` set to `~/.metalsharp/shader-cache/<exename>/` for persistent shader caching, eliminating recompilation stutter on subsequent launches
- **MetalFX 2x spatial upscaling** — `DXMT_METALFX_SPATIAL_SWAPCHAIN=1` env var + `d3d11.metalSpatialUpscaleFactor = 2.0` in `dxmt.conf`, games render at half resolution with MetalFX upscaling to native
- **DXMT config file** — `~/.metalsharp/runtime/wine/etc/dxmt.conf` with MetalFX 2x, 60fps cap (`d3d11.preferredMaxFrameRate`), feature level 12_1
- **MetalFX env vars on DxmtMetal and DxmtMetal12** — shader cache path, config file, and MetalFX swapchain flag added to both D3D11 and D3D12 launch paths
- **`metalsharp_bundle2.tar.zst`** — pre-built shims (SDL3, FNA3D, FMOD stubs, CSteamworks, steam_api) and DXMT config, extracted alongside main bundle by installer
- **Portal 2 (appid 620)** — explicitly mapped to `SteamD3DMetalPerf`, was auto-detected as `SteamMetalfx` (inert D3DM env vars)
- **Goat Simulator (appid 265930)** — mapped to `SteamD3DMetalPerf`, 32-bit UE3 D3D9 via Wine's 32-bit stack
- **Celeste (appid 504230)** — mapped to `SteamD3DMetalPerf` (was `FnaX86`). Steam Celeste exe references XNA Framework, not FNA managed DLLs. Works via Steam DRM + GPTK D3DMetal
- **Subnautica BZ (appid 848450)** — added to `DxmtMetal` explicit map
- **Schedule I (appid 3164500)** — moved from `DxmtMetal12` to `DxmtMetal` (D3D11 only, no D3D12 needed)
- **High on Life (appid 1583230)** — mapped to `SteamD3DMetalPerf` with GPTK D3DMetal
- **Games Supported doc** — `docs/GAMES-SUPPORTED.md` with full game compatibility, launch methods, recommended settings
- **Library merge** — wine-steam installed games merged into owned games list in `library()`, fixes games not appearing if Steam API doesn't report them

### Changed

- **Auto-detection: `.pak` files route to `SteamD3DMetalPerf`** — was `SteamMetalfx` (D3DMetal env vars). Source engine games get GPTK D3DMetal now
- **`SteamD3DMetalPerf` uses GPTK D3DMetal** — `WINEDLLPATH` prepended with GPTK's DLL dir so Wine loads Apple's d3d11.dll. Previous D3DM_* env vars were inert (no D3DMetal framework in our Wine builtins)
- **Installer extracts bundle2** — `install_metalsharp_bundle()` now extracts `metalsharp_bundle2.tar.zst` after main bundle, installing shims and DXMT config
- **CI downloads all .tar.zst** — GitHub Actions downloads all archives from `bundles` release including new bundle2

### Removed

- `SteamDxmtMetal` engine variant — Steam DRM + DXMT injection via env vars didn't propagate to Steam's child process

### Known Issues

- **High on Life**: Crashes after loading screen with GPTK D3DMetal. UE4 compatibility limitation
- **RE4**: Crashes with GPTK D3DMetal. Needs investigation
- **DXVK 32-bit**: Feature level 0 — all D3D feature levels unsupported through Wine's Vulkan WoW64 thunks
- **Steam auto-updates overwrite steamwebhelper wrapper** — must re-deploy after updates

## v0.17.0 — 2026-05-11

Beta 3. DXMT Metal-native D3D11 rendering, eliminating the GPTK dependency for 64-bit games. Wine 11.5 from source with 7 custom patches. Single all-in-one runtime bundle.

### Added

- **DXMT Metal-native D3D11** — DXMT D3D11/D3D12/DXGI registered as Wine builtins for 64-bit games. Renders through Metal directly, no GPTK or Vulkan needed
- **MetalSharp Wine 11.5 from source** — built with 7 custom patches:
  - Patch A: mscompatdb rules engine loader
  - Patch B: Apple GPTK bridge compatibility
  - Patch C: Graphics backend routing
  - Patch D: CEF GL context creation (`MS_FWD_COMPAT_GL_CTX`)
  - Patch E: Runtime root configuration
  - Patch F: `macdrv_functions` struct export with `visibility("default")` — allows DXMT to call Wine macdrv functions via `dlsym`
  - Patch G: `RTLD_GLOBAL` unix lib loading — makes `macdrv_functions` symbols visible to DXMT
- **`client_cocoa_view` struct compatibility** — added `macdrv_view` field to `macdrv_win_data` and `macdrv_window_get_content_view()` helper for DXMT swapchain creation
- **All-in-one runtime bundle** — single `metalsharp_bundle.tar.zst` (899MB) containing Wine 11.5 runtime, DXVK 1.10.3, Mono x86 + arm64
- **`Engine::DxmtMetal` launch pipeline** — 64-bit games use DXMT Metal with `dxgi,d3d11,d3d10core=n,b` overrides
- **`Engine::Wined3d32` launch pipeline** — 32-bit games use WineD3D OpenGL (no 32-bit Metal API on macOS)
- **Steam CEF fix** — `-cef-single-process` flag and `MS_FWD_COMPAT_GL_CTX=1` env var for Steam web helper rendering
- **`deploy_steamwebhelper_wrapper()`** — renames `steamwebhelper.exe` to prevent CEF GPU crashes
- **Rain World (appid 312520) — RUNNING via DXMT Metal** — Feature Level 11_1 on Apple M4, no GPTK
- **Nidhogg 2 (appid 535520) — RUNNING via WineD3D OpenGL** — 32-bit D3D11 via WineD3D

### Changed

- **GPTK dependency eliminated for 64-bit games** — DXMT Metal replaces GPTK for D3D11 rendering. Four patches removed the need:
  1. `winebuild --builtin` on DXMT PE DLLs
  2. `macdrv_functions` struct export
  3. `RTLD_GLOBAL` in ntdll loader
  4. `client_cocoa_view` struct compatibility
- **Wine runtime upgraded from 11.0 to 11.5** — from-source build with custom patches
- **Single bundle replaces 5 separate archives** — `metalsharp_bundle.tar.zst` replaces `wine.tar.zst`, `dxvk.tar.zst`, `mono-x86.tar.zst`, `mono-arm64.tar.zst`
- **Installer uses `install_metalsharp_bundle()`** — extracts all-in-one archive in one step instead of 3 separate steps
- **Rain World no longer needs GPTK prefix** — uses DXMT Metal engine directly
- **Nidhogg 2 uses WineD3D** — no longer copies DXMT i386 DLLs (no 32-bit Metal)
- **`DYLD_FALLBACK_LIBRARY_PATH` set on all Wine launches** — resolves gnutls/MoltenVK/freetype at runtime
- **Steam launch flags updated** — added `-cef-single-process`, `-noverifyfiles`, `-no-dwrite`

### Removed

- `Engine::GptkWine` — replaced by `Engine::DxmtMetal` and `Engine::Wined3d32`
- Separate `wine.tar.zst`, `dxvk.tar.zst`, `mono-x86.tar.zst`, `mono-arm64.tar.zst` bundle references
- `macdrv_d3dmetal.c` — removed from Wine build, replaced by DXMT integration

### Known Issues

- **DXVK 32-bit: feature level 0** — all D3D feature levels return unsupported through Wine's Vulkan WoW64 thunks. 32-bit games requiring DXVK won't work until this is fixed
- **Steam auto-updates overwrite steamwebhelper wrapper** — must re-deploy after Steam updates
- **`__wine_unix_call` abort** appears in Steam log — non-fatal

## v0.16.0 — 2026-05-07

Major runtime overhaul. Removes the mscompatdb rules engine in favor of direct engine routing. DXMT builtins replace DLL overrides for 32-bit games.

### Added

- **DXMT i386 builtins** — DXMT D3D11, DXGI, D3D10Core, winemetal DLLs baked into `i386-windows/` so 32-bit games work without DLL override configuration
- **DXMT v0.80+10 built from source** — LLVM 15 + Metal toolchain, produces Metal-backed D3D11/DXGI for Wine
- **Live game sync** — filesystem watch on the Steam steamapps directory sends `steamapps:changed` IPC to the renderer when new games are installed
- **`/steam/watch-steamapps` endpoint** — polls for new appmanifest files in the Wine Steam prefix
- **Backend cleanup on quit** — Electron `before-quit` handler kills the Rust bridge process, `window-all-closed` stops backend and watchers

### Changed

- **mscompatdb removed** — the `mscompatdb.so` rules engine is disabled. Game routing now uses `launch.rs` engine mappings + the `metalsharp-wine` wrapper with `MS_BACKEND` env var
- **Nidhogg 2 and Undertale use MetalsharpWine engine** — previously used separate DxvkMetalsharpWine/DxvkWine engines
- **DXVK updated from 2.4 to 1.10.3** — compatible with MoltenVK's Vulkan 1.1 support
- **Installer drops Wine Devel and MoltenVK steps** — these are installed via Homebrew if needed, not bundled
- **Steam stop kills wineserver + wineloader** — not just Steam.exe
- **wine.tar.zst re-uploaded** — 282MB bundle with DXMT i386 builtins included
- **dxvk.tar.zst re-uploaded** — 5MB DXVK 1.10.3

### Removed

- `DxvkMetalsharpWine`, `DxvkWine`, `WineDevel` engine variants from `launch.rs`
- `gptk.tar.zst` and `moltenvk.tar.zst` from bundle list
- `mscompatdb.so` — renamed to `.disabled`

## v0.15.1 — 2026-05-06

Patch release — bug fixes for Steam launch and setup wizard.

## v0.15.0 — 2026-05-05

### Added

- From-source MetalSharp Wine 11.0 — gnutls TLS, mscompatdb rules engine, MoltenVK, freetype
- Clean-room mscompatdb.so — loads game patches from `share/metalsharp/rules.json`
- 168 compatibility rules — Steam WebHelper GPU fix, game-specific env vars, DLL overrides, command line patches
- Single `wine.tar.zst` bundle — replaces Wine Devel + GPTK overlay assembly

### Changed

- All Wine commands set `DYLD_FALLBACK_LIBRARY_PATH` for gnutls/MoltenVK/freetype resolution
- Game setup scripts updated to use MetalSharp Wine

### Removed

- CrossOver dependency
- `crossover.tar.zst` from bundles

## v0.8.0 — 2025-05-03

### Added

- Engine auto-detection — fingerprints game directories for Unity, Unreal, FromSoftware, RE Engine, .NET/FNA
- DREDGE and Sons of the Forest support via D3DMetal performance tuning
- Five launch tiers: FNA arm64, FNA x86, GPTK, DXVK+MoltenVK, Steam + D3DMetal

## v0.7.0 — 2025-05-03

### Added

- Elden Ring support with MetalFX upscaling and D3DMetal performance env vars

## v0.6.0 — 2025-05-03

### Breaking change: new install flow

Games installed through Windows Steam under MetalSharp Wine. Full DRM support.

### Added

- Windows Steam integration — MetalSharp Wine runs full Steam client
- Steam DRM support — Among Us, Ghostrunner, RE4 with real Steam auth
- Uninstall button — removes game files and appmanifest
- Force-kill stop button

## v0.4.0 — 2025-04-13

- Initial public release
- 7 supported games
- Setup wizard with dependency installation
- Per-game auto-configuration
- Electron UI with library browser
