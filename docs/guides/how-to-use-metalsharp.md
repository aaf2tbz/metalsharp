# How to Use MetalSharp

## Install

1. Download the latest MetalSharp DMG from [GitHub Releases](https://github.com/aaf2tbz/metalsharp/releases).
2. Drag MetalSharp into `/Applications`.
3. Open it. If macOS blocks the unsigned app, go to **System Settings → Privacy & Security** and choose **Open Anyway**.
4. Run setup from inside MetalSharp — it will install Homebrew dependencies, the Wine runtime, and VC++ 2015-2022 redistributables (x64 and x86).
5. Start Wine Steam, sign in, and download a Windows game.

## Steam Games

After a game is installed, MetalSharp detects the Wine Steam library and creates a Steam game bottle such as `steam_620`.

The bottle is the launch-authoritative runtime record. It checks the selected profile, runtime assets, redistributables, DLL expectations, and logs before launch.

For routes such as M12, M11, M10, M9, and Mono/FNA, MetalSharp keeps Wine Steam alive in the background when Steamworks ownership/session state is needed, then launches the game executable through the selected bottle-aware MTSP pipeline. The game process receives the prepared prefix or native Mono/FNA environment, cache paths, and Steam identity variables (`SteamAppId` and `SteamGameId`) so Steamworks can bind back to the running Wine Steam client where applicable.

Internal Steam, Wine, macOS Steam, M32, and raw DXMT routes still exist for diagnostics, compatibility records, and backend fallback behavior, but they are not normal route selector choices. If Wine Steam is not detectable after startup, MetalSharp fails the launch clearly instead of hanging behind the renderer timeout.

Click **Play** from the Library page. Use the launch mode dropdown when you want to force a route:

| Mode | Use |
|---|---|
| M12 | D3D12 to Metal |
| M11 | D3D11 to Metal |
| M10 | D3D10 to Metal |
| M9 | D3D9 through the DXMT launch/cache family |
| Mono/FNA | Windows XNA/FNA games through MetalSharp's native Mono runtime, staged FNA/XNA assemblies, native dylibs, FMOD/FAudio/FNA3D shims, and Steamworks shim support |

Use **Runtime Doctor** on a game card when a game needs VC runtime, DirectX, .NET, WebView2, fonts, or other launch assets.

### Goldberg Steam Emulator

The Goldberg toggle enables offline play for supported games without Wine Steam running. Toggle it on from the game card — MetalSharp saves the original Steam DLLs as `.orig` and deploys the emulator with the correct appid. Toggle off to restore the originals.

Supported games include Portal 2 (appid 620) and others that work without Steam DRM validation.

## Sharp Library

Sharp Library is for Windows apps, demos, launchers, installers, and non-Steam programs.

Use **Install Windows Program** to select an `.exe` or `.msi`. MetalSharp may import it directly, or create an installer bottle, classify the installer, apply a known launcher recipe when one matches, launch it with the right profile, then scan for installed app candidates.

Apps imported from a bottle keep their `bottle_id`, launch through that bottle, and write per-bottle logs.

## Logs and Settings

Use **Logs** when something fails. The page has drawer sections for live logs, crash reports, and recent log files.

Use **Settings** to manage Steam API sync, backend restart, cache cleanup, and runtime maintenance.

### Uninstall

Settings includes a **Danger Zone** section at the bottom with an **Uninstall MetalSharp** button. This removes all Wine prefixes, bottles, Steam, runtime, caches, and settings, then moves the app to Trash.

## Useful Docs

- [Current MetalSharp README](../../README.md)
- [Launch Architecture](../architecture/launch-architecture.md)
- [Compatdata Architecture](../runtime/compatdata-architecture.md)
- [Launcher Runtime](../runtime/launcher-runtime.md)
- [Redistributable Runtime](../runtime/redistributable-runtime.md)
- [Darwin Sync Map](../runtime/darwin-sync-map.md)
- [Steam Compatibility Tool Surface](../runtime/steam-compatibility-tool-surface.md)
- [Vendor Trust Kit](../runtime/vendor-trust-kit.md)
- [Proof Targets](../compatibility/proof-targets.md)
- [Supported Games](../compatibility/GAMES-SUPPORTED.md)
- [Wine Architecture](../runtime/wine-architecture.md)
