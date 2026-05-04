# MetalSharp

Run Windows games on macOS. One app, no fuss.

MetalSharp wraps the tools that make Windows gaming on Mac possible — Wine, CrossOver, DXVK, MoltenVK, Apple's Game Porting Toolkit, FNA, Mono — into a single Electron app with a setup wizard, automatic game configuration, and one-click launch. No manual Wine prefix management. No hunting for DLLs. No editing registry keys.

## What it does

1. **Browses your Steam library** — shows every game you own via the Steam Web API
2. **Runs Windows Steam via CrossOver** — installs games through Steam's own interface with full DRM support
3. **Auto-configures the runtime** — each game gets the right Wine build, graphics pipeline, and audio bridge automatically
4. **Launches with one click** — the backend detects the game and picks the correct runtime (CrossOver, Wine Devel, GPTK, or FNA/Mono)

## Supported Games

| Game | Engine | Pipeline | Status |
|------|--------|----------|--------|
| **Celeste** | XNA/FNA | Mono x86_64 + SDL3 Metal + FMOD | Working |
| **Terraria** | XNA/FNA | Mono arm64 + SDL3 Metal + FAudio | Working |
| **Rain World** | Unity Mono | GPTK Wine + D3DMetal | Working |
| **Nidhogg 2** | GameMaker | Wine Devel + DXVK + MoltenVK | Working |
| **Among Us** | Unity IL2CPP | CrossOver Wine + Steam DRM | Working |
| **Portal 2** | Source Engine | Wine Devel + Goldberg emulator | Working |
| **Ghostrunner** | Unreal Engine 4 | CrossOver Wine + Steam DRM | Working |
| **Resident Evil 4** | RE Engine | CrossOver Wine + Steam DRM | Working |
| **Elden Ring** | FromSoftware Engine | CrossOver Wine + MetalFX + Steam DRM | Working (FPS drops in intensive areas) |
| **DREDGE** | Unity | CrossOver Wine + D3DMetal Perf + Steam DRM | Working |
| **Sons of the Forest** | Unity HDRP | CrossOver Wine + D3DMetal Perf + Steam DRM | Working |

Tested on Apple M4, macOS 26.

## How games run

MetalSharp picks the right existing toolchain per game:

**Steam DRM games** (Among Us, Ghostrunner, RE4, Elden Ring) — CrossOver Wine runs the Windows Steam client natively. Games install through Steam's interface and launch via `steam://run/` for full DRM authentication. No hacks, no emulators — just Steam running under Wine.

**XNA/FNA games** (Celeste, Terraria) — Run natively via Mono + FNA + SDL3. No Wine, no translation layers. FNA replaces Microsoft's XNA framework, SDL3 handles windowing/input/audio, and Metal handles rendering.

**D3D11 games** (Nidhogg 2) — Wine Devel runs the Windows binary, DXVK translates D3D11 to Vulkan, MoltenVK translates Vulkan to Metal. MetalSharp copies DXVK DLLs and sets env vars automatically.

**GPTK games** (Rain World) — Apple's Game Porting Toolkit handles D3D→Metal translation via D3DMetal.

**Source Engine** (Portal 2) — Wine Devel's built-in wined3d handles D3D9 via OpenGL. Goldberg emulator replaces Steam auth for offline play.

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
cd metalsharp/app
npm run build
npx electron-builder --mac dmg
```

## Setup

First launch walks through:

1. **Dependencies** — installs what's needed (Homebrew, Rosetta 2, Xcode CLI, CrossOver). Optional: Mono, GPTK, Wine Devel, MoltenVK, macOS Steam
2. **Device Name** — gives your machine a name
3. **Steam API Key** — free from [steamcommunity.com/dev/apikey](https://steamcommunity.com/dev/apikey), loads your full game library
4. **Windows Steam** — installs the Windows Steam client via CrossOver Wine. Log in with your Steam account. Games are installed through Steam's interface.

### Installing games

1. Click **Install** on any game in your library — MetalSharp starts Windows Steam
2. In the Steam window, find the game and click Install
3. MetalSharp detects the installation automatically and shows it as installed
4. Click **Play** — MetalSharp applies game-specific patches and launches

### Auto-setup (per-game)

Clicking Play for the first time runs game-specific setup:

- **Terraria** — copies macOS native libs, builds launcher, installs Xact assembly
- **Celeste** — builds SDL3/FNA3D x86_64, CSteamworks shim with 609 aliases, FMOD stubs
- **Rain World** — initializes GPTK Wine prefix
- **Nidhogg 2** — copies DXVK DLLs, sets DXVK+MoltenVK environment, creates Wine Devel prefix
- **Among Us / Ghostrunner / RE4** — launches via Steam DRM (`steam://run/`)
- **Elden Ring** — launches via Steam DRM with MetalFX upscaling + D3DMetal performance tuning
- **All other games** — auto-detected engine, launches via Steam DRM with D3DMetal performance tuning
- **Portal 2** — installs Goldberg Steam emulator for offline play

## Directory Layout

```
~/.metalsharp/
├── games/              Downloaded games (by Steam App ID, for FNA/GPTK games)
├── runtime/
│   ├── fna/            FNA assemblies for XNA games
│   ├── shims/          Native dylibs (FNA3D, SDL3, CSteamworks, FMOD stubs)
│   ├── mono-x86/       x86_64 Mono runtime (Celeste)
│   ├── dxvk-moltenvk/  Patched DXVK DLLs (Nidhogg 2)
│   └── goldberg/       Goldberg Steam emulator DLLs
├── prefix-steam-cx/    CrossOver Wine prefix with Windows Steam + DRM games
├── prefix-*/           Per-game Wine prefixes (GPTK, Wine Devel)
├── cache/              Steam config, owned games cache
└── config.json         Global settings
```

## Tech Stack

| Component | What it does |
|-----------|--------------|
| Rust backend | HTTP API, game launch, SteamCMD, dep management |
| Electron app | Desktop UI, setup wizard, library browser |
| CrossOver Wine | Win32 translation + Windows Steam + built-in Vulkan renderer |
| DXVK (patched) | D3D9/11 → Vulkan translation (Nidhogg 2) |
| Apple GPTK | D3D11/12 → Metal via D3DMetal framework (Rain World) |
| MoltenVK | Vulkan → Metal (Nidhogg 2) |
| FNA + SDL3 | XNA compatibility, native rendering (Celeste, Terraria) |
| Goldberg | Steam API emulation for offline play (Portal 2) |

## License

MIT
