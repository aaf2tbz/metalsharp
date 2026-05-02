# MetalSharp

Run Windows games on macOS. Natively.

MetalSharp translates Direct3D calls into Metal — Apple's native GPU framework. No VM, no Windows license, no Vulkan middleman.

```
Current stack:  Game → D3D → DXVK → Vulkan → MoltenVK → Metal → GPU  (4 hops)
MetalSharp:     Game → D3D → MetalSharp → Metal → GPU                (2 hops)
```

---

## Supported Games

| Game | Engine | Runtime | Audio | Steam | Verified |
|------|--------|---------|-------|-------|----------|
| **Celeste** | XNA/FNA | Mono x86_64 + SDL3 Metal | FMOD 1.10 | Working | M4, macOS 26 |
| **Terraria** | XNA/FNA | Mono arm64 + SDL3 Metal | FAudio | Working | M4, macOS 26 |
| **Rain World** | Unity Mono | GPTK Wine + D3DMetal | Working | Working | M4, macOS 26 |

| Game Type | Status | Method |
|-----------|--------|--------|
| XNA/FNA | Working | Native Mono + FNA3D + SDL3 Metal |
| Unity D3D11 | Working | Apple Game Porting Toolkit (D3DMetal) |
| D3D9 | In progress | MojoShader SM2.0/SM3.0 → MSL |
| D3D11 native | In progress | MetalSharp translation layer |
| D3D12 | In progress | Command queues, descriptor heaps, PSO |

---

## Install

### DMG (recommended)

1. Download `MetalSharp.dmg` from [Releases](https://github.com/aaf2tbz/metalsharp/releases)
2. Mount → drag to Applications → launch
3. Setup wizard handles the rest

### From source

```bash
git clone https://github.com/aaf2tbz/metalsharp.git
cd metalsharp
./install.sh
```

### Electron app (development)

```bash
cd metalsharp/app
npm install
npm run build:all    # builds Rust backend + TypeScript
npm run start        # launches Electron app
```

### Build a DMG locally

```bash
cd metalsharp/app
npm run dist -- --mac dmg
```

---

## Setup Wizard

First launch walks you through:

1. **Welcome** — what MetalSharp does
2. **Dependencies** — installs what's needed:
   - Homebrew (package manager)
   - Rosetta 2 (x86_64 translation for GPTK and Celeste)
   - Game Porting Toolkit (D3D→Metal for Unity games)
   - Mono arm64 (Terraria and arm64 FNA games)
   - Mono x86_64 (Celeste and x86 FMOD games)
   - SDL3 (graphics/input backend)
   - SteamCMD (downloads Windows game depots)
3. **Device Name** — auto-generated (e.g. `Storm-Falcon`) or custom
4. **Steam API Key** — free from [steamcommunity.com/dev/apikey](https://steamcommunity.com/dev/apikey)
5. **Steam Login** — credentials sent only to Steam servers
6. **Done**

---

## Playing Games

### From the app

1. Open MetalSharp → browse your Steam library
2. Click **Install** — downloads via SteamCMD
3. Click **Play** — auto-detects engine, configures runtime, launches

### From the command line

```bash
# XNA/FNA games (native Metal)
./scripts/setup-celeste-deps.sh    # one-time
./scripts/launch-celeste.sh

./scripts/setup-terraria-deps.sh   # one-time
./scripts/launch-terraria.sh

# Unity/D3D11 games (Game Porting Toolkit)
./scripts/setup-rainworld-deps.sh  # one-time
./scripts/launch-rainworld.sh
```

### How auto-launch works

When you click Play, the Rust backend detects the game by Steam App ID and picks the right runtime:

| App ID | Game | Launch method |
|--------|------|---------------|
| 504230 | Celeste | `arch -x86_64` mono-x86 + FMOD + SDL3 x86 |
| 105600 | Terraria | arm64 mono + FAudio + SDL3 arm64 |
| 312520 | Rain World | GPTK wine64 + prefix-gptk |
| other | auto | detect via `.metalsharp_prepared` marker |

---

## How It Works

### D3D → Metal path

```
Game (.exe)
  → PE loader (sections, relocations, imports)
  → 400+ Win32 API shims (POSIX, NSWindow, BSD sockets)
  → D3D shims (d3d11.dll, dxgi.dll) → Metal API calls
  → Shader compilation (DXBC → MSL via MojoShader / Apple IRConverter)
  → Metal GPU
```

### XNA/FNA path

```
Game.exe (Mono)
  → FNA assemblies replace XNA
  → FNA3D → SDL3 → Metal
  → CSteamworks shim → libsteam_api
  → FMOD stubs / FAudio for audio
  → Metal GPU
```

---

## Directory Layout

```
~/.metalsharp/
├── games/              Downloaded games (by Steam App ID)
│   ├── 504230/         Celeste
│   ├── 105600/         Terraria
│   └── 312520/         Rain World
├── runtime/
│   ├── fna/            FNA assemblies for XNA games
│   ├── shims/          Native dylibs (FNA3D, SDL3, CSteamworks, FMOD stubs)
│   └── mono-x86/       x86_64 Mono runtime (Celeste)
├── prefix-gptk/        Wine prefix for GPTK games (Rain World)
├── cache/              Steam config, owned games cache
├── logs/               Runtime logs
├── profiles/           Per-game config overrides
├── config.json         Global settings
└── setup.json          Setup wizard state
```

---

## Settings

| Setting | Options | Description |
|---------|---------|-------------|
| Resolution | 720p – 4K | Internal render resolution |
| Window Mode | Fullscreen / Borderless / Windowed | Game window style |
| Upscaling | Off – Ultra | MetalFX spatial upscaling |
| Shader Cache | On / Off | Persist compiled shaders to disk |
| Pipeline Cache | On / Off | Persist pipeline state objects |
| Launch Mode | Native PE / Wine | How executables load |

Per-game profiles in `~/.metalsharp/profiles/`.

---

## Tech Stack

| Component | Language | What it does |
|-----------|----------|--------------|
| D3D→Metal engine | C++20 / ObjC++ | D3D9/11/12 translation, shader compilation |
| Native PE loader | C++20 | Windows x86_64 executable loading on macOS |
| Win32 shims | C++20 | 400+ API shims across 14+ DLLs |
| Rust backend | Rust | HTTP API (tiny_http), game launch, dep management |
| Electron app | TypeScript | Desktop UI, setup wizard, library browser |

~35K lines C++/ObjC++, ~1.5K lines TypeScript, ~1.2K lines Rust. MIT licensed.

---

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [User Guide](docs/USER-GUIDE.md)
- [Compatibility](docs/COMPATIBILITY.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Developer Guide](docs/DEVELOPER-GUIDE.md)
- [PE Loader](docs/PE-LOADER.md)
- [Win32 Shims](docs/WIN32-SHIMS.md)
- [Electron App](docs/ELECTRON-APP.md)
- [FNA Integration](src/fna/README.md)
- [Roadmap](ROADMAP.md)

---

## License

MIT
