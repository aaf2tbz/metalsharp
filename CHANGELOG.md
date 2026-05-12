# Changelog

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

- external runtime dependency
- `runtime-bundle.tar.zst` from bundles

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
