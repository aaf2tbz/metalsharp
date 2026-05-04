# Changelog

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

Games are now installed through the Windows Steam client running under external runtime Wine. Full Steam DRM support.

- Install: click Install → Steam opens → install from Steam's interface
- Play: MetalSharp detects the game and launches via `steam://run/` with per-game patches

### Added

- Windows Steam integration — external runtime Wine runs the full Windows Steam client
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
