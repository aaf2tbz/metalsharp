# MetalSharp

Run Windows games on your Mac. Natively.

No virtual machine. No Windows license. No Vulkan translation layer. MetalSharp takes Direct3D calls from Windows games and translates them directly to Metal — Apple's native GPU framework. Two hops. That's it.

```
Everyone else:   Game → D3D → DXVK → Vulkan → MoltenVK → Metal → GPU   (4 hops)
MetalSharp:      Game → D3D → MetalSharp → Metal → GPU                 (2 hops)
```

The result is a thinner stack, less overhead, and a rendering path that gets out of the way so your game can get to the GPU.

---

## How It Works

MetalSharp has a native PE loader. That means it loads Windows `.exe` files directly on macOS — the game binary runs under Rosetta 2, and every Windows API call it makes gets intercepted and handled by macOS equivalents. No Wine dependency. No compatibility layers stacked on compatibility layers.

**The translation chain:**

1. **Load the game.** The PE loader parses the Windows executable, maps its sections into memory, processes relocations, and resolves imports — all the things a Windows kernel would do, but on macOS.

2. **Intercept Windows calls.** Games call Win32 and DirectX APIs through their import tables. MetalSharp resolves these to shim functions — 400+ across 14+ DLLs — that delegate to native macOS APIs. File I/O goes to POSIX. Windows go to NSWindow. Networking goes to BSD sockets. The game doesn't know it's not on Windows.

3. **Translate D3D to Metal.** When the game draws a frame, D3D11 or D3D12 calls are translated directly into Metal API calls. Shaders follow two paths: DXIL bytecode compiles to metallib via Apple's Metal Shader Converter (when installed), or DXBC bytecode translates to Metal Shading Language as a fallback. Compiled shaders are cached to disk so subsequent launches are fast.

4. **Render.** Metal drives your GPU. The game appears on screen.

The full Win32 shim surface covers kernel32, user32, gdi32, ws2_32, advapi32, ole32, oleaut32, ntdll, and 51 api-ms-win-* API sets. D3D11 and D3D12 are both supported, along with DXGI, XAudio2 → CoreAudio, and XInput → GameController.

---

## Install

### Requirements

- macOS 13 (Ventura) or later
- Apple Silicon Mac or Intel Mac with Metal support
- Xcode Command Line Tools (`xcode-select --install`)

### Quick Install (CLI)

One command. Checks dependencies, builds, runs tests, sets up everything:

```bash
git clone https://github.com/aaf2tbz/metalsharp.git && cd metalsharp && ./install.sh
```

Then launch a game:

```bash
./build/metalsharp path/to/game.exe
```

### Desktop App

MetalSharp also ships with an Electron app — game library, cover art, one-click launch, settings, crash reporting, the works.

```bash
cd metalsharp/app
npm install
npm run build:all
npm run start
```

The app auto-detects games installed through Steam, Epic Games Store, and GOG. You can also download Windows games directly via SteamCMD integration and add local executables to `~/.metalsharp/games/`.

### DMG Installer

For a packaged install:

```bash
./tools/dmg/create-dmg.sh
```

Creates a macOS DMG with all binaries, libraries, documentation, and a setup script.

---

## Settings

Configuration lives at `~/.metalsharp/settings.json` and can be managed through the desktop app or manually:

| Setting | Options | Description |
|---------|---------|-------------|
| Resolution | 720p – 4K | Internal render resolution |
| Window Mode | Fullscreen, Borderless, Windowed | How the game window appears |
| Upscaling | Off, Low, Medium, High, Ultra | MetalFX spatial upscaling quality |
| VSync | On / Off | Frame sync to display |
| Shader Cache | On / Off, configurable size | Persist compiled shaders for fast reloads |
| Pipeline Cache | On / Off | Persist compiled pipeline state objects |
| Launch Mode | Native PE / Wine | How executables are loaded |

Per-game profiles can override global settings and are stored in `~/.metalsharp/profiles/`.

---

## Game Compatibility

Games are rated on a five-tier scale: **Platinum** (perfect), **Gold** (minor issues), **Silver** (playable with glitches), **Bronze** (boots but unplayable), **Broken** (won't launch).

MetalSharp includes a binary scanner that detects 30+ known DRM and anti-cheat systems. Games using kernel-level anti-cheat (Fortnite, Valorant, PUBG, Apex Legends) will not work — these require a real Windows kernel.

Games using D3D11 generally have the best coverage. D3D12 support is present and growing. Games relying on OpenGL or Vulkan won't work through MetalSharp's translation path.

See the [Compatibility Guide](docs/COMPATIBILITY.md) for details.

---

## What's Inside

- **Native PE Loader** — Loads Windows x86_64 executables directly. Handles relocations, imports, TLS callbacks, delay loads, SEH, CFG, and CRT initialization. No Wine needed.
- **D3D11 & D3D12** — Full device and context implementations backed by Metal. Shader translation via Apple IRConverter (primary) or DXBC→MSL (fallback).
- **400+ Win32 API Shims** — kernel32, user32, gdi32, ws2_32, advapi32, ole32, oleaut32, ntdll, and 51 API sets, all delegating to native macOS APIs.
- **Audio** — XAudio2 → CoreAudio with positional audio (distance attenuation, stereo panning, Doppler). DirectSound fallback for legacy titles.
- **Input** — XInput → Apple GameController with rumble and gyro support.
- **Performance** — Shader cache, pipeline cache, render thread pool, frame pacing, MetalFX upscaling, GPU profiler.
- **Desktop App** — Game library with cover art, Steam/Epic/GOG detection, one-click download and launch, settings, crash reporter, update checker.
- **Game Validation** — DRM scanner, compatibility database, import reporter, crash diagnostics.

~35K lines of C++/Objective-C++, ~1K lines TypeScript, ~900 lines Rust. MIT licensed.

---

## Documentation

- [User Guide](docs/USER-GUIDE.md) — Getting started
- [Compatibility Guide](docs/COMPATIBILITY.md) — Game compatibility and DRM
- [Troubleshooting](docs/TROUBLESHOOTING.md) — Common issues
- [Developer Guide](docs/DEVELOPER-GUIDE.md) — Contributing
- [Architecture](docs/ARCHITECTURE.md) — System design and data flow
- [Roadmap](ROADMAP.md) — Full development history (24 phases, all complete)

---

## License

MIT
