# Changelog

## v0.15.0 — 2026-05-05

### Added

- From-source MetalSharp Wine 11.0 build — built from external runtime's open-source Wine patches with gnutls TLS, custom mscompatdb rules engine, MoltenVK, and freetype
- Clean-room mscompatdb.so — custom rules engine that loads game patches from `share/metalsharp/rules.json`, zero proprietary code
- gnutls support — Wine can now complete TLS handshakes for Steam login
- 168 compatibility rules — Steam WebHelper GPU fix, game-specific env vars, dll overrides, and command line patches
- Single `wine.tar.zst` bundle — replaces the old Wine Devel + GPTK overlay assembly with a single pre-built runtime

### Changed

- MetalSharp Wine installed from bundled `wine.tar.zst` instead of assembling from Wine Devel + GPTK overlays
- All Wine commands now set `DYLD_FALLBACK_LIBRARY_PATH` to the Wine lib directory for gnutls/MoltenVK/freetype resolution
- Game setup scripts updated to use MetalSharp Wine instead of external runtime
- Removed DXVK DLL removal from `prepare_metalsharp_game()` — MetalSharp Wine handles D3D natively
- Removed `runtime-bundle.tar.zst` from bundle list — no longer needed

### Removed

- external runtime dependency — MetalSharp now uses its own from-source Wine build
- `find_gptk_wine_path()` — no longer needed for Wine runtime assembly

## v0.8.0 — 2025-05-03

### Added

- Engine auto-detection — unknown games are fingerprinted by scanning game directory for engine markers
- DREDGE support (App ID 1562430) — Unity, auto-detected as SteamD3DMetalPerf
- Sons of the Forest support (App ID 1326470) — Unity HDRP, auto-detected as SteamD3DMetalPerf
- `detect_engine_from_dir()` — fingerprinter detects Unity, Unreal Engine, FromSoftware, RE Engine, .NET/FNA from file patterns
- `get_engine_for_appid()` — engine enum routes 100+ known games to the correct pipeline
- Five distinct launch tiers: FNA arm64, FNA x86, GPTK, DXVK+MoltenVK, Steam with D3DMetal perf tuning, Steam with MetalFX upscaling

### Changed

- Unknown games default to Steam + D3DMetal performance tuning (async commit, multithreaded, skip barriers, NaN safety)

## v0.7.0 — 2025-05-03

### Added

- Elden Ring support (App ID 1245620) with MetalFX upscaling and D3DMetal performance tuning
- D3DMetal performance env vars: async GPU commit, multithreaded D3D, skip render barriers, NaN safety

## v0.6.0 — 2025-05-03

### Breaking change: new install flow

Games are now installed through the Windows Steam client running under MetalSharp Wine. Full Steam DRM support.

- Install: click Install → Steam opens → install from Steam's interface
- Play: MetalSharp detects the game and launches via `steam://run/` with per-game patches

### Added

- Windows Steam integration — MetalSharp Wine runs the full Windows Steam client
- Steam DRM support — Among Us, Ghostrunner, RE4 launch with real Steam auth
- Resident Evil 4 support (App ID 2050650)
- Uninstall button — removes game files and appmanifest from Wine Steam prefix
- Force-kill stop button — kills all Wine processes for the game

## v0.4.0 — 2025-04-13

- Initial public release
- 7 supported games (Terraria, Celeste, Rain World, Nidhogg 2, Among Us, Portal 2, Ghostrunner)
- Setup wizard with dependency installation
- Per-game auto-configuration
- Electron UI with library browser
