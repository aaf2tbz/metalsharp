<div align="center">

# M E T A L S H A R P

**Run Windows Steam games and Windows programs on macOS with Metal.**

<a href="https://github.com/aaf2tbz/metalsharp/actions"><img src="https://img.shields.io/github/actions/workflow/status/aaf2tbz/metalsharp/ci.yml?branch=main&style=for-the-badge" alt="CI"></a>
<a href="https://github.com/aaf2tbz/metalsharp/releases"><img src="https://img.shields.io/github/v/release/aaf2tbz/metalsharp?style=for-the-badge" alt="Release"></a>
<a href="https://github.com/aaf2tbz/metalsharp/discussions"><img src="https://img.shields.io/github/discussions/aaf2tbz/metalsharp?style=for-the-badge" alt="Discussions"></a>
<a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg?style=for-the-badge" alt="MIT"></a>

**Beta 6**

</div>

---

MetalSharp is an ARM64 app for Apple Silicon that bundles its own Wine runtime, DXMT Metal graphics support, Windows Steam setup, game detection, runtime bottles, logs, and launch diagnostics. Linux `.deb` packaging exists for Debian-based systems.

## Runtime

MetalSharp keeps state under `~/.metalsharp/`:

```text
~/.metalsharp/
├── runtime/wine/   MetalSharp Wine, DXMT, D3D/DXGI DLLs, shims
├── runtime/redist/ Optional user-supplied redistributables
├── prefix-steam/   Shared Windows Steam prefix and live Steam session
├── bottles/        Runtime bottle manifests, prefixes, logs, assets
├── games/          Prepared game payloads
├── sharp-library/  Imported Windows apps
├── shader-cache/   Per-game graphics caches
├── configs/        Pipeline/runtime rules
├── cache/          Steam/update cache
├── logs/           App, launch, crash, setup, migration logs
└── crashes/        Preserved crash reports
```

`prefix-steam` remains the shared Wine Steam prefix. Wine Steam stays alive as the background client/session owner for Steam games.

`bottles` is the runtime authority layer. Installer and Sharp Library bottles use their own prefixes. Steam game bottles use ids like `steam_620` and preflight the actual shared Steam prefix. Env-dependent Steam routes launch the selected game executable directly through the chosen MTSP pipeline with the bottle prefix, route env, and `SteamAppId`/`SteamGameId`, while Wine Steam remains alive for Steamworks connectivity.

## Launching

When Play is clicked, MetalSharp resolves a pipeline, syncs the bottle/runtime record, checks required components/assets, prepares DLLs/cache/env/logs, ensures Wine Steam is ready when needed, then launches through direct Wine/MTSP, Wine Steam, native macOS Steam, or native Mono/FNA.

| Mode | Purpose |
|---|---|
| **M9** | D3D9 / 32-bit capable DXMT-family route |
| **M10** | D3D10 to Metal |
| **M11** | D3D11 to Metal |
| **M12** | D3D12 to Metal |
| **M32** | 32-bit Wine fallback |
| **Steam** | Windows Steam in Wine |
| **MacOS Steam** | Native macOS Steam |
| **Wine** | Plain Wine custom-app fallback |
| **Native macOS** | Native Mono/FNA/XNA path |

## Sharp Library

Sharp Library manages Windows apps, demos, launchers, and installers. Use **Install Windows Program** for standalone `.exe` files, MSI packages, launcher bootstrapper apps, .NET installers, WebView apps, Java launchers, and game installers.

Installer bottles classify the program, apply known launcher recipes where possible, launch it with the right profile, keep per-bottle logs, scan for installed app candidates, and import detected apps back into Sharp Library with their `bottle_id`.

## Diagnostics and Migration

Use Logs and Runtime Doctor when something fails. Doctor checks common blockers like Wine Mono, Gecko, .NET, VC runtime, DirectX June 2010, WebView2, core fonts, runtime profile, app detection, launch state, and redistributable assets.

Upgrades preserve `setup.json`, Steam settings/cache, `prefix-steam`, `games`, `sharp-library`, `bottles`, and Steam game compatdata.

## Download

Get the latest DMG from [Releases](https://github.com/aaf2tbz/metalsharp/releases), drag MetalSharp into `/Applications`, and open it.

Linux builds:

```bash
cd app
npm run deb
npm run deb:docker
```

## Requirements

- macOS DMG: Apple Silicon Mac, macOS 14 or newer
- Linux `.deb`: x64 Debian/Ubuntu with `tar`, `curl`, `zstd`, and Wine
- About 2 GB free space

## Help

- [How to Use MetalSharp](docs/how-to-use-metalsharp.md)
- [Launch Architecture](docs/launch-architecture.md)
- [Wine Architecture](docs/wine-architecture.md)
- [Supported Games](docs/GAMES-SUPPORTED.md)
- [Releases](https://github.com/aaf2tbz/metalsharp/releases)
- [Discussions](https://github.com/aaf2tbz/metalsharp/discussions)
- [Issues](https://github.com/aaf2tbz/metalsharp/issues)

## License

MIT
