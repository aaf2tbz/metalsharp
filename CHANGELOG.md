# Changelog

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
