# MetalSharp

Play Windows Steam games on macOS. One app, one click.

MetalSharp bundles a custom Wine 11.5 runtime with DXMT (D3D→Metal translation), Apple GPTK D3DMetal, DXVK, and FNA into a single Electron app with a setup wizard and one-click launch. It picks the right backend for each game automatically.

## How games run

| Backend | How it works | Used for |
|---------|-------------|----------|
| **DXMT Metal** | D3D11 calls translated directly to Metal command buffers via DXMT + winemetal.so. Per-game shader cache + MetalFX spatial upscaling. | Rain World, Schedule I, Subnautica BZ |
| **GPTK D3DMetal** | Steam DRM games launched via `steam://run/` with GPTK's D3DMetal framework loaded (WINEDLLPATH pointing to GPTK's d3d11.dll). | Portal 2, Goat Simulator, Celeste, 50+ other Steam games |
| **WineD3D OpenGL** | Wine's builtin wined3d with OpenGL backend. Only option for 32-bit games (no 32-bit Metal API on macOS). | Nidhogg 2 |
| **DXVK + MoltenVK** | D3D→Vulkan→Metal fallback (2 hops). DXVK 1.10.3 limited to Vulkan 1.1 by MoltenVK. | Future D3D9 games |
| **FNA + SDL3** | Native Mono + FNA + SDL3 Metal rendering — no Wine. | Terraria (arm64), future FNA games |

Games are auto-detected by scanning their install directory for engine markers (Unity, Unreal, FromSoftware, RE Engine, .NET/FNA). Unknown games default to GPTK D3DMetal Steam launch.

## Supported games

See [docs/GAMES-SUPPORTED.md](docs/GAMES-SUPPORTED.md) for full details including recommended settings.

| Game | Pipeline | Status |
|------|----------|--------|
| Rain World | DXMT Metal (D3D11→Metal) | Working |
| Schedule I | DXMT Metal (D3D11→Metal) | Working |
| Subnautica: Below Zero | DXMT Metal (D3D11→Metal) | Working |
| Nidhogg 2 | WineD3D OpenGL (32-bit) | Working |
| Portal 2 | GPTK D3DMetal (Steam DRM) | Working |
| Goat Simulator | GPTK D3DMetal (Steam DRM) | Working |
| Celeste | GPTK D3DMetal (Steam DRM) | Working |
| High on Life | GPTK D3DMetal (Steam DRM) | Crashes after loading screen |
| Resident Evil 4 | GPTK D3DMetal (Steam DRM) | Crashes |

Tested on Apple M4, macOS 26.

## What's new in v0.18.0

- **GPTK D3DMetal for Steam DRM games** — `SteamD3DMetalPerf` now loads GPTK's d3d11.dll via WINEDLLPATH, enabling Apple's D3DMetal for Steam-launched games
- **DXMT shader cache** — per-game persistent shader cache under `~/.metalsharp/shader-cache/<exename>/`, eliminates recompilation stutter on subsequent launches
- **MetalFX 2x spatial upscaling** — DXMT games render at half resolution, MetalFX upscales to native. Configurable via `dxmt.conf`
- **7 games confirmed working** — Rain World, Schedule I, Subnautica BZ, Nidhogg 2, Portal 2, Goat Simulator, Celeste
- **Bundle 2** — pre-built shims (SDL3, FNA3D, FMOD stubs, CSteamworks) and DXMT config in `metalsharp_bundle2.tar.zst`
- **Library merge fix** — wine-steam installed games now appear in library even if Steam API doesn't report them

## Install

Download the DMG from [Releases](https://github.com/aaf2tbz/metalsharp/releases), drag to Applications, launch. The setup wizard handles everything.

## Setup

First launch walks through a setup wizard:

1. **Install dependencies** — extracts the MetalSharp runtime bundle (one Touch ID prompt). Falls back to `brew install` for Rosetta 2 and Xcode CLI tools.
2. **Launch Steam** — MetalSharp starts Windows Steam via Wine. Log in through the Steam window.
3. **Done** — your library loads. Install and play games.

### Installing games

1. Click **Install** on any game — MetalSharp starts Windows Steam
2. In the Steam window, find the game and click Install
3. MetalSharp detects the new installation via filesystem watch
4. Click **Play** — MetalSharp applies game-specific config and launches

## Building from source

```bash
git clone https://github.com/aaf2tbz/metalsharp.git
cd metalsharp

# Build the Rust backend
cd app/src-rust && cargo build --release && cd ../..

# Build the Electron app
cd app && npm install && npm run build

# Run in dev mode
npm run start
```

### Building a DMG

```bash
# Download bundles from GitHub releases
./tools/dmg/create-bundles.sh

# Build and package
cd app && npm run build:all && npx electron-builder --mac dmg --arm64
```

## Tech stack

| Layer | Technology |
|-------|-----------|
| Desktop app | Electron + TypeScript |
| Backend | Rust HTTP server (Actix) |
| Wine runtime | MetalSharp Wine 11.5 (from-source, gnutls TLS, DXMT builtins, 7 custom patches) |
| D3D→Metal | DXMT v0.80+10 (LLVM 15 + Metal toolchain) |
| D3D→Metal (Steam) | Apple GPTK D3DMetal via WINEDLLPATH |
| D3D→Vulkan | DXVK 1.10.3 |
| Vulkan→Metal | MoltenVK (via Homebrew) |
| XNA/FNA | FNA + SDL3 + Mono |
| Bundles | Two zstd-compressed archives (runtime + shims/config) |

## Architecture docs

- [docs/launch-architecture.md](docs/launch-architecture.md) — Engine dispatch, launch paths, process lifecycle
- [docs/wine-architecture.md](docs/wine-architecture.md) — Wine runtime layout, WoW64, env vars
- [docs/dxmt-vulkan-architecture.md](docs/dxmt-vulkan-architecture.md) — DXMT Metal and DXVK pipeline details
- [docs/GAMES-SUPPORTED.md](docs/GAMES-SUPPORTED.md) — Full game compatibility, settings, launch methods

```
app/
├── src-rust/           Rust backend — HTTP API, game launch, Steam, installer
│   └── src/
│       ├── main.rs     HTTP router
│       ├── launch.rs   Game launch (engine detection, per-game routing)
│       ├── steam.rs    Steam process management, steamapps watcher
│       ├── setup.rs    Per-game setup (shim builds, DLL staging)
│       ├── installer.rs Dependency installer (Wine, DXVK, Mono, Rosetta)
│       └── scan.rs     Game library scanner
├── src/
│   ├── main/           Electron main process
│   └── renderer/       Electron renderer — UI, library browser, setup wizard
└── bundles/            Pre-packaged deps

scripts/                Per-game setup and launch scripts
configs/                Mono DLL maps for FNA games
docs/                   Architecture and game compatibility docs
```

## Bundled dependencies

| Bundle | Contents |
|--------|----------|
| `metalsharp_bundle.tar.zst` | MetalSharp Wine 11.5 with DXMT Metal D3D11/D3D12 builtins, gnutls, wined3d, DXVK 1.10.3, Mono x86 + arm64 |
| `metalsharp_bundle2.tar.zst` | Pre-built shims (SDL3, FNA3D, FMOD stubs, CSteamworks, steam_api), DXMT config (MetalFX, framerate) |
| `SteamSetup.exe` | Windows Steam installer |

## License

MIT
