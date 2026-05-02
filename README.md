# MetalSharp

Run Windows games on your Mac. Natively.

No virtual machine. No Windows license. No Vulkan translation layer. MetalSharp takes Direct3D calls from Windows games and translates them directly to Metal — Apple's native GPU framework. Two hops. That's it.

```
Everyone else:   Game → D3D → DXVK → Vulkan → MoltenVK → Metal → GPU   (4 hops)
MetalSharp:      Game → D3D → MetalSharp → Metal → GPU                 (2 hops)
```

---

## What Works

MetalSharp currently supports two paths for running Windows games on macOS:

**Native D3D → Metal** — Windows games that use Direct3D 9, 11, or 12 run through MetalSharp's translation layer. D3D calls are intercepted and mapped to Metal API calls. Shaders are compiled to Metal Shading Language. Works with both the native PE loader and Wine.

**XNA/FNA** — Games built on Microsoft's XNA Framework (Celeste, Terraria, Stardew Valley, and hundreds more) run natively via FNA + SDL3 + Metal. No Wine needed. Audio, input, and Steam integration all work. [Celeste runs natively on Apple Silicon with controller support.](src/fna/README.md)

---

## Install

### DMG Installer (Recommended)

1. Download `MetalSharp.dmg`
2. Double-click to mount
3. Drag **MetalSharp** to **Applications**
4. Launch — the setup wizard walks you through everything

The setup wizard handles:
- Installing dependencies (Mono, SDL3)
- Choosing a device name for persistent Steam sessions
- Connecting your Steam account (API key + SteamCMD login)
- Everything else happens automatically from there

### Manual Install

```bash
git clone https://github.com/aaf2tbz/metalsharp.git
cd metalsharp
./install.sh
```

Then launch a game:

```bash
./build/metalsharp path/to/game.exe
```

### Desktop App (Development)

```bash
cd metalsharp/app
npm install
npm run build:all
npm run start
```

### Build a DMG

```bash
./tools/dmg/build-dmg.sh
```

---

## First Launch

When you open MetalSharp for the first time, a setup wizard guides you through:

1. **Welcome** — what MetalSharp does and how it works
2. **Dependencies** — checks for Mono, SDL3, SteamCMD, and installs what's missing
3. **Device Name** — auto-generates a memorable name (e.g. `Storm-Falcon`) or pick your own. This identifies your machine to Steam so you don't need to re-login every session
4. **Steam API Key** — free key from [steamcommunity.com/dev/apikey](https://steamcommunity.com/dev/apikey) to load your full game library
5. **Steam Login** — your Steam credentials (sent only to Steam, never stored)
6. **Done** — start downloading and playing

---

## Playing Games

### From Your Steam Library

1. Open MetalSharp
2. Browse your full Steam library with cover art
3. Click **Install** on any game — it downloads via SteamCMD
4. Click **Play** — MetalSharp auto-configures the runtime on first launch

### Auto Runtime Setup

When you launch a game for the first time, MetalSharp detects what kind of game it is and sets everything up:

- **XNA games** — copies FNA assemblies, FNA3D (SDL3 Metal backend), SDL3, Steamworks shim, FMOD stubs, and DllMap configs
- **D3D games** — configures Wine prefix with DLL overrides and Metal translation shims
- Subsequent launches skip setup (tracked via `.metalsharp_prepared` marker)

### What's Supported

| Game | Engine | Method | Status | Notes |
|------|--------|--------|--------|-------|
| Celeste | XNA/FNA | Native Mono (x86_64) + SDL3 Metal | **Working** | Full gameplay, audio (FMOD 1.10), controller, Steam |
| Terraria | XNA/FNA | Native Mono (arm64) + SDL3 Metal | **Working** | Full gameplay, audio (FAudio), controller, Steam |
| Rain World | Unity Mono | GPTK Wine + D3D→Metal | **Working** | Full gameplay via Apple Game Porting Toolkit |

| Game Type | Status | Notes |
|-----------|--------|-------|
| XNA/FNA | Working | Native Metal rendering via FNA3D + SDL3 |
| Unity D3D11 | Working | Via Apple Game Porting Toolkit (D3DMetal) |
| D3D9 | In Progress | MojoShader SM2.0/SM3.0 → MSL translation working |
| D3D11 | In Progress | MetalSharp native translation layer |
| D3D12 | In Progress | Command queues, descriptor heaps, pipeline state |

### Launching Games

```bash
# XNA/FNA games (native Metal)
./scripts/setup-celeste-deps.sh    # one-time setup
./scripts/launch-celeste.sh

./scripts/setup-terraria-deps.sh   # one-time setup
./scripts/launch-terraria.sh

# Unity/D3D11 games (via Game Porting Toolkit)
./scripts/setup-rainworld-deps.sh  # one-time setup
./scripts/launch-rainworld.sh
```

---

## How It Works

### D3D → Metal Path

1. **Load the game.** The PE loader parses the Windows executable, maps sections, processes relocations, resolves imports.

2. **Intercept Windows calls.** 400+ Win32 API shims across 14+ DLLs delegate to native macOS APIs. File I/O → POSIX. Windows → NSWindow. Networking → BSD sockets. The game doesn't know it's not on Windows.

3. **Translate D3D to Metal.** D3D calls map directly to Metal. Shaders compile via Apple IRConverter or DXBC→MSL fallback. Compiled shaders cache to disk for fast reloads.

4. **Render.** Metal drives your GPU.

### XNA/FNA Path

1. Game `.exe` is .NET/Mono — runs directly on macOS Mono runtime (arm64)
2. FNA replaces XNA assemblies — same API, different backend
3. FNA3D renders via SDL3's GPU API → Metal
4. Native shims bridge Steamworks.NET and FMOD to macOS dylibs
5. Controller input via SDL3 → GameController

```
Celeste.exe (Mono) → FNA → FNA3D → SDL3 → Metal → Apple M4
```

---

## Settings

| Setting | Options | Description |
|---------|---------|-------------|
| Resolution | 720p – 4K | Internal render resolution |
| Window Mode | Fullscreen, Borderless, Windowed | Game window appearance |
| Upscaling | Off – Ultra | MetalFX spatial upscaling |
| Shader Cache | On / Off | Persist compiled shaders |
| Pipeline Cache | On / Off | Persist pipeline state objects |
| Launch Mode | Native PE / Wine | How executables load |

Per-game profiles in `~/.metalsharp/profiles/`.

---

## Directory Layout

```
~/.metalsharp/
├── games/           Downloaded games (by Steam App ID)
├── runtime/
│   ├── fna/         FNA assemblies (.dll) for XNA games
│   └── shims/       Native dylibs (FNA3D, SDL3, CSteamworks, FMOD stubs)
├── cache/           Steam config, owned games cache
├── logs/            Runtime logs
├── prefix/          Wine prefix (for D3D games via Wine)
├── profiles/        Per-game config overrides
├── config.json      Global settings
└── setup.json       Setup wizard state
```

---

## What's Inside

- **Native PE Loader** — Windows x86_64 executables on macOS. Relocations, imports, TLS, SEH, CFG.
- **D3D9/11/12 → Metal** — Full device/context backed by Metal. MojoShader for SM2.0/SM3.0 → MSL.
- **400+ Win32 Shims** — kernel32, user32, gdi32, ws2_32, advapi32, ole32, ntdll, 51 API sets.
- **XNA/FNA Support** — FNA + SDL3 + Metal for XNA games. CSteamworks shim. FMOD stubs.
- **Audio** — XAudio2 → CoreAudio. FMOD stubs for games without arm64 builds.
- **Input** — XInput → GameController. Full controller support with rumble.
- **Performance** — Shader cache, pipeline cache, MetalFX upscaling, frame pacing.
- **Desktop App** — Electron + Rust backend. Setup wizard, library browser, one-click install/play.

~35K lines C++/ObjC++, ~1.5K lines TypeScript, ~1.2K lines Rust. MIT licensed.

---

## Documentation

- [User Guide](docs/USER-GUIDE.md)
- [Compatibility Guide](docs/COMPATIBILITY.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Developer Guide](docs/DEVELOPER-GUIDE.md)
- [Architecture](docs/ARCHITECTURE.md)
- [FNA Integration](src/fna/README.md)
- [Roadmap](ROADMAP.md)

---

## License

MIT
