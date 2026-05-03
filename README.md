# MetalSharp

Run Windows games on macOS. One app, no fuss.

MetalSharp takes the existing tools that make Windows gaming on Mac possible — Wine, CrossOver, DXVK, MoltenVK, Apple's Game Porting Toolkit, FNA, Mono — and wraps them into a single Electron app with a setup wizard, automatic game configuration, and one-click launch. No manual Wine prefix management. No hunting for DLLs. No editing registry keys.

## What it does

1. **Browses your Steam library** — shows every game you own
2. **Downloads games** — uses SteamCMD to pull Windows depots, with cached credentials so you're not re-authenticating every time
3. **Auto-configures the runtime** — each game gets the right Wine build, graphics pipeline, audio bridge, and Steam auth set up automatically
4. **Launches with one click** — the backend detects the game by Steam App ID and picks the correct runtime

## Supported Games

| Game | Engine | Pipeline | Status |
|------|--------|----------|--------|
| **Celeste** | XNA/FNA | Mono x86_64 + SDL3 Metal + FMOD | Working |
| **Terraria** | XNA/FNA | Mono arm64 + SDL3 Metal + FAudio | Working |
| **Rain World** | Unity Mono | GPTK Wine + D3DMetal | Working |
| **Nidhogg 2** | GameMaker | Wine Devel + DXVK + MoltenVK | Working |
| **Among Us** | Unity IL2CPP | CrossOver Wine + Vulkan | Working |
| **Portal 2** | Source Engine | Wine Devel + Goldberg emulator | Working |
| **Ghostrunner** | Unreal Engine 4 | CrossOver Wine + Vulkan | Working |

Tested on Apple M4, macOS 26.

## How games run

MetalSharp doesn't reinvent anything. It picks the right existing toolchain per game:

**XNA/FNA games** (Celeste, Terraria) — Run natively via Mono + FNA + SDL3. No Wine, no translation layers. FNA replaces Microsoft's XNA framework, SDL3 handles windowing/input/audio, and Metal handles rendering.

**Unity games** (Rain World, Among Us, Ghostrunner) — Apple's Game Porting Toolkit or CrossOver Wine handle the Windows-to-Mac translation. D3D11 calls go through D3DMetal (Apple's D3D→Metal layer) or CrossOver's Vulkan renderer → MoltenVK → Metal.

**D3D11 games** (Nidhogg 2) — Wine Devel runs the Windows binary, DXVK translates D3D11 to Vulkan, MoltenVK translates Vulkan to Metal. 4 hops but it works.

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

1. **Dependencies** — installs what's needed (Homebrew, Rosetta 2, GPTK, Mono, SDL3, SteamCMD, CrossOver, Wine Devel, MoltenVK, Goldberg)
2. **Steam API Key** — free from [steamcommunity.com/dev/apikey](https://steamcommunity.com/dev/apikey)
3. **Steam Login** — credentials sent only to Steam servers via SteamCMD

### Auto-setup (per-game)

Clicking Install downloads the game and runs game-specific setup:

- **Terraria** — copies macOS native libs, builds launcher, installs Xact assembly
- **Celeste** — builds SDL3/FNA3D x86_64, CSteamworks shim with 609 aliases, FMOD stubs
- **Rain World** — initializes GPTK Wine prefix
- **Nidhogg 2** — builds patched DXVK from source with MoltenVK compat patches
- **Among Us / Ghostrunner** — initializes CrossOver Wine prefix, removes conflicting DXVK DLLs
- **Portal 2** — installs Goldberg Steam emulator for offline play

## Directory Layout

```
~/.metalsharp/
├── games/              Downloaded games (by Steam App ID)
├── runtime/
│   ├── fna/            FNA assemblies for XNA games
│   ├── shims/          Native dylibs (FNA3D, SDL3, CSteamworks, FMOD stubs)
│   ├── mono-x86/       x86_64 Mono runtime (Celeste)
│   ├── dxvk-moltenvk/  Patched DXVK DLLs (Nidhogg 2)
│   ├── vkd3d-proton/   VKD3D-Proton DLLs (D3D12 games)
│   └── goldberg/       Goldberg Steam emulator DLLs
├── prefix-*/           Per-game Wine prefixes
├── cache/              Steam config, owned games cache
└── config.json         Global settings
```

## Tech Stack

| Component | What it does |
|-----------|--------------|
| Rust backend | HTTP API, game launch, SteamCMD, dep management |
| Electron app | Desktop UI, setup wizard, library browser |
| DXVK (patched) | D3D9/11 → Vulkan translation (cross-compiled, MoltenVK patches) |
| VKD3D-Proton | D3D12 → Vulkan translation (cross-compiled) |
| Apple GPTK | D3D11/12 → Metal via D3DMetal framework |
| CrossOver Wine | Win32 translation with built-in Vulkan renderer |
| MoltenVK | Vulkan → Metal |
| FNA + SDL3 | XNA compatibility, native rendering |
| Goldberg | Steam API emulation for offline play |

## Documentation

- [Game Compatibility](GAME_COMPAT.md)
- [Architecture](docs/ARCHITECTURE.md)
- [FNA Integration](src/fna/README.md)

## License

MIT
