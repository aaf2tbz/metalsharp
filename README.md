# MetalSharp

**Play Windows Steam games on macOS. No GPTK, no external runtime, no commercial Wine fork.**

MetalSharp is a standalone D3D→Metal translation layer that runs Windows games natively through Apple's Metal API. It bundles a custom Wine 11.5 runtime with DXMT (D3D→Metal), DXVK + MoltenVK, Goldberg Steam emulator, and FNA into a single app — one-click install, one-click play.

> **This is MetalSharp Beta 4.** The core 64-bit Metal-native path is stable. 32-bit Vulkan translation is still under development.

## How it works

| Backend | How it works | Used for |
|---------|-------------|----------|
| **DXMT Metal** | D3D11 calls translated directly to Metal command buffers via DXMT + winemetal.so. Per-game shader cache + MetalFX spatial upscaling. | Rain World, Schedule I, Subnautica BZ |
| **DXVK + MoltenVK (32-bit)** | D3D9→Vulkan→Metal for 32-bit games. DXVK 1.10.3 d3d9.dll injected into game dir, MoltenVK translates Vulkan to Metal. | Portal 2, Goat Simulator |
| **WineD3D OpenGL** | Wine's builtin wined3d with OpenGL backend. For 32-bit games that don't work through DXVK. | Nidhogg 2 |
| **SteamD3DMetalPerf** | Steam DRM games launched via `steam://run/` with GPTK's D3DMetal framework loaded (WINEDLLPATH pointing to GPTK's d3d11.dll). | Celeste, High on Life, RE4, 50+ other Steam games |
| **SteamMetalfx** | Steam DRM games with D3DMetal MetalFX env vars. | Elden Ring, Sekiro |
| **SteamBare** | Steam DRM games with no extra env vars. | Among Us, Valheim |
| **FNA + SDL3** | Native Mono + FNA + SDL3 rendering — no Wine. | Terraria (arm64) |

Games are auto-detected by scanning their install directory for engine markers (Unity, Unreal, FromSoftware, RE Engine, .NET/FNA, D3D9 DLLs). Unknown games default to SteamD3DMetalPerf.

## Supported games

See [docs/GAMES-SUPPORTED.md](docs/GAMES-SUPPORTED.md) for full details including recommended settings.

| Game | Pipeline | Status |
|------|----------|--------|
| Rain World | DXMT Metal (D3D11→Metal) | Working |
| Schedule I | DXMT Metal (D3D11→Metal) | Working |
| Subnautica: Below Zero | DXMT Metal (D3D11→Metal) | Working |
| Portal 2 | DXVK MoltenVK (D3D9→Vulkan→Metal) | Working |
| Goat Simulator | DXVK MoltenVK (D3D9→Vulkan→Metal) | Working |
| Nidhogg 2 | WineD3D OpenGL | Working |
| Celeste | GPTK D3DMetal (Steam DRM) | Working |
| High on Life | GPTK D3DMetal (Steam DRM) | Crashes after loading screen |
| Resident Evil 4 | GPTK D3DMetal (Steam DRM) | Crashes |

Tested on Apple M4, macOS 26.

## What's new in Beta 4

- **Goldberg Steam emulator** — automatic download and deployment for supported DxvkMetal32 titles. No Steam login required for Portal 2, Goat Simulator, and future 32-bit games.
- **Auto-updater fixed** — the in-app updater now correctly closes MetalSharp and runs the install script with an admin password prompt. Previously it silently failed.
- **Metal CI** — every push to Metal/D3D/DXGI code now runs adapter validation, shader translation tests, and the full test suite on Apple Silicon runners.
- **mscompatdb 591 rules** — auto-detects and routes games to the correct rendering engine.

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

## What to expect

The 64-bit Metal-native path (DXMT) is stable and tested across multiple titles. 32-bit Vulkan translation (DXVK + MoltenVK) is still under active development — some titles work, but compatibility is not yet at parity. MetalFX upscaling infrastructure is in place but not yet exposed in the UI.

If you hit issues, open an issue on this repo with the game name, your Mac model, and the crash log from `~/.metalsharp/logs/`.

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
| Backend | Rust HTTP server (tiny_http) |
| Wine runtime | MetalSharp Wine 11.5 (from-source, gnutls TLS, DXMT builtins, 7 custom patches) |
| D3D→Metal | DXMT v0.80+10 (LLVM 15 + Metal toolchain) |
| D3D→Metal (Steam) | Apple GPTK D3DMetal via WINEDLLPATH |
| D3D9→Vulkan→Metal | DXVK 1.10.3 + MoltenVK |
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

src/                    C++ native engine (D3D11/D3D12/DXGI/XAudio2/XInput Metal implementations)
include/                C++ headers
scripts/                Per-game setup and launch scripts
configs/                Mono DLL maps for FNA games
docs/                   Architecture and game compatibility docs
tools/
├── dmg/                DMG packaging scripts
└── launcher/           Native launcher (C++ MetalSharp binary, Wine prefix management)
```

## Bundled dependencies

| Bundle | Contents |
|--------|----------|
| `metalsharp_bundle.tar.zst` | MetalSharp Wine 11.5 with DXMT Metal D3D11/D3D12 builtins, DXVK 1.10.3, Mono x86 + arm64, MoltenVK ICD |
| `metalsharp_bundle2.tar.zst` | Pre-built shims (SDL3, FNA3D, FMOD stubs, CSteamworks, steam_api), DXMT config (MetalFX, framerate) |
| `SteamSetup.exe` | Windows Steam installer |

## License

MIT
