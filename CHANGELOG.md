# Changelog

## v0.46.0 — 2026-06-11

Install hardening, backend lifecycle, Lucide icons, uninstall, and compatibility updates.

### Added

- **VC++ 2015-2022 runtime setup step** — new step in the setup wizard with x64 and x86 install cards. Downloads from Microsoft, runs via Wine `/install`, idempotent.
- **Kingdom Hearts HD 1.5+2.5 ReMIX** (appid 2552430) and **HD 2.8** (appid 2552440) — M11 pipeline rules, preferred EXE names, process patterns.
- **Uninstall MetalSharp** — Settings → Danger Zone button. Confirms, kills processes, `rm -rf ~/.metalsharp/`, shows success dialog, detached shell trashes `.app` from `/Applications`.
- **Lucide icons** — 26 inline SVGs replaced with tree-shaken `unplugin-icons` + `@iconify-json/lucide` components across 6 Vue files.
- **Dev-mode backend auto-restart** — `ensureRunning()` auto-restarts in dev mode so binary swaps work without manual restart.

### Changed

- **Backend lifecycle** — `killProcess()` sends SIGTERM then SIGKILL on quit. Production `ensureRunning()` no longer auto-restarts — only `start()` spawns. `cleanup()` is async and awaited in `before-quit`.
- **Goldberg emulator hardened** — install idempotency checks only core DLLs; `steam_interfaces.txt` regenerated if empty/incomplete; diagnostic logging on missing runtime.
- **Sharp Library Steam routing** — `SteamSetup.exe` routed to `steam::install_steam()` (correct `~/.metalsharp/prefix-steam/`); `Steam.exe` routed to `steam::launch_wine_steam()`.
- **GPTK prefix seeding** — macOS `ditto` replaces file-by-file copy for ~2GB/9000+ files; post-wineboot validation.
- **What's New modal** — fixed 640px width with compositing layer isolation and enlarged Close button tap target.

### Fixed

- **Dosdevice symlink guards** — snapshot/restore all dosdevice links around wineboot; post-check verifies `c:→drive_c` survived.
- **Wineboot migration window wait** — blocks completion until Steam self-update windows close.
- **External drive Z: drive** — `Z:\Volumes\...` fallback for external Steam libraries with idempotent `z:→/` dosdevice symlink.
- **Install wizard hardening** — Celeste `steam_api` fallback paths; Steam installer crash suppression.
- **Goldberg appid at toggle time** — `ensure_steam_emu_if_active()` repairs DLLs only; `ensure_real_steam_dlls()` deploys real Steam DLLs for normal launches.
- **Library grid row isolation** — bottle dropdown expands current row only.
- **CI test race** — unique migration preserve temp dir per thread.
- **CodeQL alerts** — resolve 3 `cpp/integer-multiplication-cast-to-long` in `CoreAudioBackend.cpp`.

## v0.45.5 — 2026-06-10

Migration prefix external drive support.

### Fixed

- **External Steam library migration** — migration handles Steam libraries on external drives by discovering their volume roots and creating correct dosdevice symlinks.
- **Wineboot on Steam prefix** — migration runs `wineboot -u` on the Steam prefix after update to register new Wine DLLs and registry entries.

## v0.45.0 — 2026-06-08

Post-update migration wizard, FNA Mono hardening.

### Added

- **Post-update migration wizard** — when the updater completes, MetalSharp re-launches into a migration view that preserves user data (settings, bottles, compatdata, Sharp Library apps) while installing the refreshed runtime.
- **VC++ and DirectX redistributable preflight** — runtime doctor checks and installs common redistributables.

### Fixed

- **FNA/Mono config templates** — ships Mono config templates in app bundle, guards against empty config files.
- **Setup wizard welcome page** — updated descriptions to match actual feature set.

## v0.44.0 — 2026-06-04

EAC toggle, kernel translation IPC bridge.

### Added

- **EAC offline toggle** — per-game `_winhttp.dll` deployment for offline EAC bypass with toggle UI in game cards.
- **Kernel translation IPC** — native macOS IPC bridge for Wine kernel32 translation layer.

## v0.40.0 — 2026-05-28

DXMT D3D12 support, M12 pipeline.

### Added

- **D3D12 to Metal via DXMT** — M12 pipeline for D3D12 games using DXMT's Metal backend.
- **Per-game shader and pipeline cache** — persistent cache dirs under `~/.metalsharp/shader-cache/` and `~/.metalsharp/pipeline-cache/`.

## v0.38.0 — 2026-05-24

Bottle manifest system, runtime doctor.

### Added

- **Runtime bottles** — bottle manifests under `~/.metalsharp/bottles/` with per-bottle prefixes, profiles, component state, and launch logs.
- **Runtime doctor** — per-bottle diagnostics for profile, compatibility, redistributables, and component repair.

## v0.35.0 — 2026-05-20

Linux packaging, anti-cheat evidence.

### Added

- **Linux DEB packages** — Debian package build and publish to GHCR via CI.
- **Anti-cheat evidence endpoints** — Steam anti-cheat evidence, module probe, and delta audit for protected launcher handoff analysis.

## v0.33.0 — 2026-05-19

Beta 7. Runtime bottles, installer profiles, migration wizard.

### Added

- **Installer bottle support** — Sharp Library classifies `.exe`/`.msi` installers, launches in bottle-aware Wine prefixes, scans for installed apps.
- **Migration wizard** — preserves user settings, Steam metadata, Sharp Library apps, and bottle settings across updates.
- **Runtime profile routing** — explicit bottle profiles for M9/M10/M11/M12/M32/Steam/Wine/installer flows.

## v0.24.0 — 2026-05-14

Auto-updater fix, Metal CI.

### Fixed

- **Auto-updater never ran install script** — `electron.remote` was undefined; replaced with `app:quit` IPC channel.

## v0.22.0 — 2026-05-14

Native C++ engine, DXVK MoltenVK for 32-bit D3D9.

### Added

- **Native C++ engine** — D3D11, D3D12, DXGI, XAudio2, XInput implementations via CMake.
- **DxvkMetal32 engine** — DXVK d3d9.dll through MoltenVK Vulkan→Metal for 32-bit games.
- **HTTP backend uses tiny_http** — replaced Actix.

## v0.18.0 — 2026-05-12

GPTK D3DMetal integration, MetalFX upscaling, 7 games confirmed.

### Added

- **GPTK D3DMetal for Steam DRM games** — `WINEDLLPATH` + `DYLD_FALLBACK_LIBRARY_PATH` for Apple's D3DMetal.
- **MetalFX 2x spatial upscaling** — half-resolution rendering with MetalFX upscaling to native.
- **DXMT config file** — `dxmt.conf` with MetalFX, 60fps cap, feature level 12_1.

## v0.17.0 — 2026-05-11

Beta 3. DXMT Metal-native D3D11, Wine 11.5 from source.

### Added

- **DXMT Metal-native D3D11** — renders through Metal directly, no GPTK or Vulkan needed.
- **MetalSharp Wine 11.5 from source** — built with 7 custom patches for DXMT integration.

## v0.6.0 — 2025-05-03

Windows Steam integration, DRM support.

### Added

- **Windows Steam** — MetalSharp Wine runs full Steam client with DRM support.
- **Uninstall button** — removes game files and appmanifest.

## v0.4.0 — 2025-04-13

Initial public release. 7 supported games, setup wizard, per-game auto-configuration, Electron UI.
