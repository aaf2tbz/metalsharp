# MetalSharp

Play Windows Steam games on macOS. One app, one click.

MetalSharp bundles a custom Wine runtime with DXMT (D3D→Metal translation), DXVK, FNA, and Apple GPTK support into a single Electron app with a setup wizard and one-click launch. It picks the right backend for each game automatically.

## How games run

| Backend | How it works | Used for |
|---------|-------------|----------|
| **DXMT builtins** | DXMT D3D11/DXGI DLLs baked into the Wine runtime's `i386-windows/` — no override configuration needed | 32-bit D3D11 games (Nidhogg 2) |
| **Wine D3DMetal** | Wine's built-in wined3d → D3DMetal pipeline | Simple D3D9 games (Undertale) |
| **Steam DRM** | Windows Steam runs under MetalSharp Wine — games install and launch through Steam's own interface with full DRM auth | 64-bit Steam games (RE4, Among Us, Ghostrunner, Elden Ring, DREDGE, Sons of the Forest) |
| **Apple GPTK** | Apple's Game Porting Toolkit D3D→Metal translation | Games needing GPTK (Rain World) |
| **DXVK 1.10.3** | D3D9/11 → Vulkan → MoltenVK → Metal fallback | Games that need Vulkan intermediaries |
| **FNA + SDL3** | Native Mono + FNA + SDL3 Metal rendering — no Wine | XNA/FNA games (Celeste, Terraria) |

Games are auto-detected by scanning their install directory for engine markers (Unity, Unreal, FromSoftware, RE Engine, .NET/FNA). Unknown games default to Steam DRM launch.

## Supported games

| Game | Pipeline | Status |
|------|----------|--------|
| Resident Evil 4 | MetalSharp Wine + Steam DRM | Working |
| Among Us | MetalSharp Wine + Steam DRM | Working |
| Ghostrunner | MetalSharp Wine + Steam DRM | Working |
| Elden Ring | MetalSharp Wine + MetalFX + Steam DRM | Working |
| DREDGE | MetalSharp Wine + D3DMetal Perf + Steam DRM | Working |
| Sons of the Forest | MetalSharp Wine + D3DMetal Perf + Steam DRM | Working |
| Rain World | GPTK Wine + D3DMetal | Working |
| Nidhogg 2 | MetalSharp Wine + DXMT builtins | Working |
| Undertale | MetalSharp Wine + wined3d | Working |
| Celeste | Mono x86_64 + FNA + SDL3 Metal + FMOD | Working |
| Terraria | Mono arm64 + FNA + SDL3 Metal + FAudio | Working |
| Portal 2 | Wine Devel + Goldberg | Working |

Tested on Apple M4, macOS 26.

## Install

Download the DMG from [Releases](https://github.com/aaf2tbz/metalsharp/releases), drag to Applications, launch. The setup wizard handles everything.

## Setup

First launch walks through a setup wizard:

1. **Install dependencies** — extracts MetalSharp Wine, DXVK, Mono runtimes from the bundled archives (one Touch ID prompt). Falls back to `brew install` for Rosetta 2 and Xcode CLI tools.
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

### Building the native engine (optional, future)

The CMake build produces MetalSharp's native D3D→Metal translation layer — standalone DLL replacements that don't need Wine. Not yet used by the app.

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build .
ctest --output-on-failure
```

## Tech stack

| Layer | Technology |
|-------|-----------|
| Desktop app | Electron + TypeScript |
| Backend | Rust HTTP server (Actix) |
| Wine runtime | MetalSharp Wine 11.0 (from-source, gnutls TLS, DXMT builtins) |
| D3D→Metal | DXMT v0.80+10 (LLVM 15 + Metal toolchain) |
| D3D→Vulkan | DXVK 1.10.3 |
| Vulkan→Metal | MoltenVK (via Homebrew) |
| XNA/FNA | FNA + SDL3 + Mono |
| Apple D3D | Game Porting Toolkit D3DMetal |
| Bundles | zstd-compressed archives, extracted at install |

## Architecture

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
│   │   ├── index.ts    App lifecycle, backend management, steamapps watcher
│   │   └── preload.ts  IPC bridge to renderer
│   └── renderer/       Electron renderer — UI, library browser, setup wizard
└── bundles/            Pre-packaged deps (wine.tar.zst, dxvk.tar.zst, etc.)

src/                    Native engine (C++/ObjC++, CMake build)
├── metal/              Metal backend — device, shaders, command buffers
├── d3d/                D3D11/D3D12 implementation backed by Metal
├── dxgi/               DXGI factory/adapter/swap chain
├── audio/              XAudio2 → CoreAudio bridge
├── input/              XInput → GameController bridge
├── wine/               Wine PE/Unix integration, shader patches
├── runtime/            Game detection, DRM detection, PE loading
├── loader/             Native PE loader (future: run without Wine)
├── fna/                FNA integration (shims, configs, launchers)
└── win32/              Win32 API shims (kernel32, user32, network)

include/metalsharp/     Public headers for the native engine
tests/                  CMake test suite
tools/                  Build scripts, launcher, DMG packaging
scripts/                Per-game setup and launch scripts
configs/                Mono DLL maps for FNA games
```

## Bundled dependencies

The DMG ships with all runtimes pre-packaged. No internet required during install.

| Bundle | Contents |
|--------|----------|
| `wine.tar.zst` | MetalSharp Wine 11.0 with DXMT i386 builtins, gnutls, wined3d |
| `dxvk.tar.zst` | DXVK 1.10.3 x32 + x64 DLLs |
| `mono-x86.tar.zst` | x86_64 Mono 6.12.0 (Celeste, Rosetta games) |
| `mono-arm64.tar.zst` | arm64 Mono 6.14.1 (Terraria, native games) |
| `SteamSetup.exe` | Windows Steam installer |

## License

MIT
