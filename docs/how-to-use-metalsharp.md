# How to Use MetalSharp

## Install

1. Download the latest MetalSharp DMG from [GitHub Releases](https://github.com/aaf2tbz/metalsharp/releases).
2. Drag MetalSharp into `/Applications`.
3. Open it. If macOS blocks the unsigned app, go to **System Settings -> Privacy & Security** and choose **Open Anyway**.
4. Run setup from inside MetalSharp so it can install the Wine runtime.
5. Start Wine Steam, sign in, and download a Windows game.

## Steam Games

After a game is installed, MetalSharp detects the Wine Steam library and creates a Steam game bottle such as `steam_620`.

The bottle is the launch-authoritative runtime record. It checks the selected profile, runtime assets, redistributables, DLL expectations, and logs before launch.

For env-dependent routes such as M9, M10, M11, M12, M32, and Wine fallback, MetalSharp keeps Wine Steam alive in the background, then launches the game executable directly through the selected bottle-aware MTSP pipeline. The game process receives the prepared prefix, route environment, cache paths, and Steam identity variables (`SteamAppId` and `SteamGameId`) so Steamworks can bind back to the running Wine Steam client.

Plain **Steam** mode still uses Wine Steam itself. If Wine Steam is not detectable after startup, MetalSharp now fails the launch clearly instead of hanging behind the renderer timeout.

Click **Play** from the Library page. Use the launch mode dropdown when you want to force a route:

| Mode | Use |
|---|---|
| M9 | D3D9 / 32-bit capable DXMT-family route |
| M10 | D3D10 to Metal |
| M11 | D3D11 to Metal |
| M12 | D3D12 to Metal |
| M32 | 32-bit Wine fallback |
| Steam | Windows Steam in Wine |
| MacOS Steam | Native macOS Steam |
| Wine | Plain Wine fallback |
| FNA/Mono/XNA | Windows XNA/FNA games through MetalSharp's Mono runtime |

Use **Runtime Doctor** on a game card when a game needs VC runtime, DirectX, .NET, WebView2, fonts, or other launch assets.

## Sharp Library

Sharp Library is for Windows apps, demos, launchers, installers, and non-Steam programs.

Use **Install Windows Program** to select an `.exe` or `.msi`. MetalSharp may import it directly, or create an installer bottle, classify the installer, apply a known launcher recipe when one matches, launch it with the right profile, then scan for installed app candidates.

Apps imported from a bottle keep their `bottle_id`, launch through that bottle, and write per-bottle logs.

## Logs and Settings

Use **Logs** when something fails. The page has drawer sections for live logs, crash reports, and recent log files.

Use **Settings** to manage Steam API sync, backend restart, cache cleanup, and runtime maintenance.

## Useful Docs

- [Current MetalSharp README](../README.md)
- [Launch Architecture](launch-architecture.md)
- [Compatdata Architecture](compatdata-architecture.md)
- [Launcher Runtime](launcher-runtime.md)
- [Redistributable Runtime](redistributable-runtime.md)
- [Darwin Sync Map](darwin-sync-map.md)
- [Steam Compatibility Tool Surface](steam-compatibility-tool-surface.md)
- [Vendor Trust Kit](vendor-trust-kit.md)
- [Proof Targets](proof-targets.md)
- [Supported Games](GAMES-SUPPORTED.md)
- [Wine Architecture](wine-architecture.md)
