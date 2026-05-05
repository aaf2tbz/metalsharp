# MetalSharp

Play your Steam Windows games on macOS. One app, one click.

MetalSharp is a macOS app that combines the tools that make Windows gaming on Mac work — Wine, Apple's Game Porting Toolkit, DXVK, MoltenVK, FNA — into a single Electron app with a setup wizard, automatic game configuration, and one-click launch. It figures out the best way to run each game so you don't have to.

## What it does

- **Browses your Steam library** — shows every game you own via the Steam Web API
- **Runs Windows Steam via MetalSharp Wine** — installs games through Steam's own interface with full DRM support
- **Auto-configures the runtime** — each game gets the right Wine build, graphics pipeline, and audio bridge
- **Launches with one click** — detects the game engine and picks the best available method

## How games run

MetalSharp combines multiple working methods and adds new ones for games that don't cooperate with a single approach:

**MetalSharp Wine + Steam DRM** — Our from-source Wine 11.0 build with gnutls TLS, a custom rules engine, and MoltenVK runs the Windows Steam client natively. Games install through Steam's interface and launch via `steam://run/` for full DRM authentication. This handles most modern 64-bit games (Elden Ring, RE4, Ghostrunner, DREDGE, Sons of the Forest, Among Us).

**D3DMetal performance tuning** — For games running through MetalSharp Wine, MetalSharp applies Apple D3DMetal performance flags (async GPU commit, multithreaded D3D, skip render barriers, NaN safety) to squeeze out better frame rates.

**MetalFX upscaling** — Demanding games like Elden Ring get Apple MetalFX spatial upscaling on top of the D3DMetal pipeline.

**FNA + SDL3 Metal** — XNA/FNA games (Celeste, Terraria) run natively via Mono + FNA + SDL3. No Wine, no translation layers. FNA replaces XNA, SDL3 handles windowing/input/audio, Metal handles rendering. MetalSharp builds the native shims (CSteamworks with 609 alias flags, FMOD stubs, gdiplus stubs) automatically.

**Apple Game Porting Toolkit** — Games like Rain World use Apple's GPTK for D3D→Metal translation via D3DMetal.

**DXVK + MoltenVK** — 32-bit D3D11 games (Nidhogg 2) that can't use D3DMetal run through a patched DXVK→Vulkan→MoltenVK→Metal chain.

**Wine Devel + Goldberg** — Games like Portal 2 run via Wine Devel's built-in wined3d with Goldberg emulator for offline Steam auth.

**Engine auto-detection** — Unknown games are fingerprinted by scanning their directory for engine markers (Unity, Unreal, FromSoftware, RE Engine, .NET/FNA). MetalSharp routes them to the right pipeline automatically.

## Supported Games

| Game | Engine | Pipeline | Status |
|------|--------|----------|--------|
| Celeste | XNA/FNA | Mono x86_64 + SDL3 Metal + FMOD | Working |
| Terraria | XNA/FNA | Mono arm64 + SDL3 Metal + FAudio | Working |
| Rain World | Unity Mono | GPTK Wine + D3DMetal | Working |
| Nidhogg 2 | GameMaker | MetalSharp Wine + DXVK | Working |
| Among Us | Unity IL2CPP | MetalSharp Wine + Steam DRM | Working |
| Portal 2 | Source Engine | Wine Devel + Goldberg emulator | Working |
| Ghostrunner | Unreal Engine 4 | MetalSharp Wine + Steam DRM | Working |
| Resident Evil 4 | RE Engine | MetalSharp Wine + Steam DRM | Working |
| Elden Ring | FromSoftware Engine | MetalSharp Wine + MetalFX + Steam DRM | Working |
| DREDGE | Unity | MetalSharp Wine + D3DMetal Perf + Steam DRM | Working |
| Sons of the Forest | Unity HDRP | MetalSharp Wine + D3DMetal Perf + Steam DRM | Working |

Tested on Apple M4, macOS 26.

## Install

### DMG

1. Download from [Releases](https://github.com/aaf2tbz/metalsharp/releases)
2. Drag to Applications, launch
3. Setup wizard handles the rest

### From source

```bash
git clone https://github.com/aaf2tbz/metalsharp.git
cd metalsharp
./install.sh
```

### Development

```bash
cd metalsharp/app
npm install
npm run build        # TypeScript + renderer assets
npm run start        # launch Electron app

cd metalsharp/app/src-rust
cargo build --release  # Rust backend
```

### Build a DMG

```bash
# 1. Bundle dependencies (compresses Wine, GPTK, Mono, DXVK, SteamSetup.exe)
./tools/dmg/create-bundles.sh

# 2. Build the DMG
cd metalsharp/app
npm run build
npx electron-builder --mac dmg
```

## Setup

First launch walks through a 4-step wizard:

1. **Install** — two buttons. "Install Homebrew" opens Terminal for the brew installer. "Install Dependencies" runs everything else automatically (MetalSharp Wine, GPTK, Rosetta 2, Xcode CLI, Mono, DXVK, Windows Steam). Bundled archives are extracted with a system auth prompt (Touch ID / password); missing deps fall back to `brew install`. Live progress bar and log throughout.
2. **Steam** — set a device name, optionally enter your Steam Web API key, then click "Launch Steam". MetalSharp starts Windows Steam via MetalSharp Wine. Log in through the Steam window.
3. **Done** — library loads, download and play.

### Installing games

1. Click **Install** on any game in your library — MetalSharp starts Windows Steam
2. In the Steam window, find the game and click Install
3. MetalSharp detects the installation automatically and shows it as installed
4. Click **Play** — MetalSharp applies game-specific patches and launches

## Tech Stack

| Component | What it does |
|-----------|--------------|
| Rust backend | HTTP API, game launch, Steam integration, background dep installer |
| Electron app | Desktop UI, setup wizard, library browser, Steam controls |
| MetalSharp Wine | From-source Wine 11.0 with gnutls, mscompatdb rules engine, MoltenVK |
| Apple GPTK | D3D11/12 → Metal via D3DMetal (for games that need it) |
| DXVK 1.10.3 | D3D9/11 → Vulkan translation (32-bit games) |
| MoltenVK | Vulkan → Metal (DXVK pipeline) |
| FNA + SDL3 | XNA compatibility, native Metal rendering (Celeste, Terraria) |
| Goldberg | Steam API emulation for offline play (Portal 2) |
| Wine Devel | Standalone Wine for games that need it |
| zstd | Compressed bundle extraction for DMG-packaged dependencies |

## Bundled Dependencies

The DMG includes all dependencies pre-packaged:

| Bundle | What it provides |
|--------|------------------|
| `wine.tar.zst` | MetalSharp Wine 11.0 (from-source) with gnutls, rules engine, MoltenVK, Mono |
| `gptk.tar.zst` | Game Porting Toolkit.app with D3DMetal |
| `mono-x86.tar.zst` | x86_64 Mono 6.12.0 runtime (Celeste, Rosetta games) |
| `mono-arm64.tar.zst` | arm64 Mono 6.14.1 runtime (Terraria, native games) |
| `moltenvk.tar.zst` | MoltenVK Vulkan→Metal translation layer |
| `dxvk.tar.zst` | DXVK 1.10.3 D3D→Vulkan DLLs |
| `SteamSetup.exe` | Windows Steam installer |

No internet required during install — everything extracts from the DMG with a single Touch ID prompt.

## License

MIT
