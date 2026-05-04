# Changelog

## v0.6.0 — 2025-05-03

### Breaking change: new install flow

Games are now installed through the **Windows Steam client** running under external runtime Wine. MetalSharp no longer uses SteamCMD for game downloads. This gives full Steam DRM support and eliminates credential management.

- Install: click Install → Steam opens → install from Steam's interface
- Play: MetalSharp detects the game and launches via `steam://run/` with per-game patches

### Added

- **Windows Steam integration** — external runtime Wine runs the full Windows Steam client
- **Steam DRM support** — Among Us, Ghostrunner, RE4 launch with real Steam auth
- **Resident Evil 4** support (App ID 2050650)
- **Uninstall button** — removes game files and appmanifest from Wine Steam prefix
- **Force-kill stop button** — kills all Wine processes for the game, no more lingering processes
- **Setup wizard step 4** — installs Windows Steam via external runtime, polls for completion
- `resolve_game_dir()` — finds games in either `~/.metalsharp/games/` or Wine Steam prefix
- `kill_game()` — kills all processes matching a game's directory across all prefixes
- `uninstall_game()` — removes game files + appmanifest from Wine Steam prefix

### Changed

- external runtime is now a **required dependency** (was optional)
- SteamCMD removed from required deps (no longer used for installs)
- GPTK and Mono are now optional (only needed for specific games)
- `wine start steam://run/{appid}` instead of `wine steam://run/{appid}` — fixes protocol handler
- `prepare_game()` searches Wine Steam prefix for game dirs, not just local games dir
- `launch_auto()` uses `resolve_game_dir()` for all game paths
- `launch_dxvk_wine()` and `launch_wine_devel()` use explicit appid for stable prefix names
- Frontend always routes through `/game/launch-auto` — backend decides launch method
- Install button starts Steam and tells user to install from there, polls for completion

### Fixed

- `steam://run/` protocol handler — must use `wine start` not pass URL as direct arg
- Wine prefix derivation for games in Steam prefix — was using directory name instead of appid
- Lingering Wine processes after game close — stop button now force-kills full process tree

## v0.4.0 — 2025-04-13

- Initial public release
- 7 supported games
- Setup wizard with dependency installation
- SteamCMD-based game downloads
- Per-game auto-configuration
- Electron UI with library browser
