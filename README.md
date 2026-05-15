<div align="center">

# M E T A L S H A R P

**Run Windows Steam games on macOS. No VM, no external runtime or paid service. Just play.**

<a href="https://github.com/aaf2tbz/metalsharp/actions"><img src="https://img.shields.io/github/actions/workflow/status/aaf2tbz/metalsharp/ci.yml?branch=main&style=for-the-badge" alt="CI"></a>
<a href="https://github.com/aaf2tbz/metalsharp/releases"><img src="https://img.shields.io/github/v/release/aaf2tbz/metalsharp?include_prereleases&style=for-the-badge" alt="Release"></a>
<a href="https://github.com/aaf2tbz/metalsharp/discussions"><img src="https://img.shields.io/github/discussions/aaf2tbz/metalsharp?style=for-the-badge" alt="Discussions"></a>
<a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg?style=for-the-badge" alt="MIT"></a>

**D3D to Metal, in one hop.**<br />
Custom Wine 11.5 + DXMT + DXVK + MoltenVK + Goldberg + FNA — all bundled, one-click install.

</div>

---

MetalSharp is a standalone D3D-to-Metal translation layer that runs Windows
Steam games natively on Apple Silicon through Apple's Metal API. It bundles a
custom Wine 11.5 runtime with DXMT (D3D11/D3D12 to Metal), DXVK + MoltenVK
(D3D9 to Vulkan to Metal), the Goldberg Steam emulator, and FNA/XNA support into
a single macOS app.

Games are auto-detected and routed to the correct rendering pipeline by the
**MTSP Engine** — a three-tier resolver that checks per-game TOML rules, then
scans the game directory for engine markers, then falls back to PE header
analysis. No manual configuration needed.

## Pipelines

The MTSP Engine supports 14 rendering pipelines. Games are routed
automatically — no manual selection required.

| Pipeline | ID | How it works | Example games |
|---|---|---|---|
| **DXMT D3D11 → Metal** | `m11` | D3D11/DXGI calls translated directly to Metal command buffers via DXMT. Per-game shader cache + MetalFX 2x spatial upscaling. | Rain World, Schedule I, Persona 3 Reload |
| **DXMT D3D12 → Metal** | `m12` | D3D12 translated to Metal via DXMT. Same pipeline as m11 with d3d12.dll injection. | Subnautica: Below Zero |
| **DXVK D3D9 → Metal** | `m32_vk` | D3D9 → Vulkan → Metal via DXVK 1.10.3 + MoltenVK. Goldberg emulator handles Steam DRM. | Portal 2, Goat Simulator |
| **WineD3D OpenGL** | `m9_gl` | Wine's builtin OpenGL backend for 32-bit D3D9 games. | Nidhogg 2 |
| **WineD3D 32-bit** | `m32_w` | WineD3D for 32-bit games without Vulkan. | — |
| **Bare Wine 64-bit** | `m64` | Plain Wine, no graphics overrides. | — |
| **Steam Native** | `steam` | macOS native launch via Steam. | Undertale, Stardew Valley, Among Us |
| **D3DMetal + MetalFX** | `steam_metalfx` | Steam DRM + Apple D3D→Metal with MetalFX spatial upscaling. | Elden Ring, Sekiro, RE2 |
| **D3DMetal Perf** | `steam_d3dmetal_perf` | Steam DRM + Apple D3D→Metal with perf env vars. | Cyberpunk 2077, Baldur's Gate 3, 60+ games |
| **FNA ARM64** | `fna_arm64` | Native Mono + FNA + SDL3 — no Wine at all. | Terraria |
| **FNA x86** | `fna_x86` | Rosetta Mono x86 + FNA + SDL3. | Celeste |

## Install

Download the latest DMG from [Releases](https://github.com/aaf2tbz/metalsharp/releases),
drag to `/Applications`, and launch. The setup wizard handles everything:

1. **Dependencies** — extracts the MetalSharp Wine 11.5 runtime (one Touch ID prompt), DXMT Metal runtime, Goldberg emulator, Mono runtimes, and pipeline routing rules
2. **Steam** — installs and launches Windows Steam via Wine. Log in through the Steam window
3. **Done** — your game library loads. Install and play.

### Requirements

- macOS 14+ (Sonoma or later)
- Apple Silicon (M1/M2/M3/M4)
- ~2 GB disk space for the runtime bundle

## How it works

```text
User clicks Play
  │
  ▼
MTSP Engine (3-tier resolver)
  ├── 1. TOML rules      → exact appid→pipeline mapping (50+ games)
  ├── 2. Directory scan   → engine markers (Unity, Unreal, .NET/FNA, D3D9)
  └── 3. PE analysis     → D3D API detection (D3D9/D3D11/D3D12)
      │
      ▼
Pipeline dispatch
  ├── m11: inject d3d11.dll + dxgi.dll + winemetal.dll → launch via Wine
  ├── m32_vk: inject d3d9.dll → deploy Goldberg → launch via Wine
  ├── steam_*: set D3DMetal env vars → steam://run/ via Wine Steam
  └── fna_*: native Mono + FNA launch (no Wine)
```

## Runtime layout

All runtime files live under `~/.metalsharp/`:

```text
~/.metalsharp/
├── runtime/
│   ├── wine/                          MetalSharp Wine 11.5
│   │   ├── bin/wine, wineboot, metalsharp-wine
│   │   ├── lib/wine/x86_64-unix/       Wine unix .so files
│   │   ├── lib/dxmt/x86_64-unix/       winemetal.so
│   │   ├── lib/dxmt/x86_64-windows/    d3d11.dll, dxgi.dll, d3d12.dll, winemetal.dll
│   │   ├── lib/dxvk/i386-windows/      d3d9.dll, d3d8.dll
│   │   ├── etc/dxmt.conf              DXMT config (MetalFX, framerate)
│   │   └── etc/vulkan/icd.d/          MoltenVK ICD
│   ├── goldberg/                       Steam emulator DLLs (x86 + x64)
│   ├── mono-x86/                      Celeste and other x86 .NET games
│   ├── mono-arm64/                    Terraria and other ARM64 .NET games
│   └── shims/                         CSteamworks, FMOD stubs
├── prefix-steam/                      Wine prefix (Windows filesystem)
├── configs/
│   └── mtsp-rules.toml                AppID → pipeline mapping
├── shader-cache/
│   ├── dxmt-metal/<appid>/             DXMT D3D11 shader cache
│   └── dxvk-metal32/<appid>/           DXVK D3D9 state cache
└── logs/                              Launch logs
```

## Auto-detection

Games not in the TOML rules file are auto-detected by scanning their
install directory:

| Marker | Pipeline |
|---|---|
| `*_Data/Managed/` dir (no native PE DLLs) | `fna_arm64` |
| `UnityPlayer.dll` or `GameAssembly.dll` | `steam_d3dmetal_perf` |
| `engine/` + `binaries/` dirs (Unreal) | `steam_metalfx` |
| `.pak` files (Source/Unreal) | `steam_d3dmetal_perf` |
| `.bdt` / `.bhd` files (Dark Souls / FromSoft) | `steam_metalfx` |
| PE header: D3D11 64-bit | `m11` |
| PE header: D3D12 64-bit | `m12` |
| PE header: D3D9 32-bit | `m32_vk` |
| Default fallback | `steam_d3dmetal_perf` |

## Supported games

See [docs/GAMES-SUPPORTED.md](docs/GAMES-SUPPORTED.md) for full details.

| Game | Pipeline | Status |
|---|---|---|
| Rain World | DXMT Metal | Working |
| Rain World Downpour | DXMT Metal | Working |
| Schedule I | DXMT Metal | Working |
| Persona 3 Reload | DXMT Metal | Working |
| Subnautica: Below Zero | DXMT Metal 12 | Working |
| Portal 2 | DXVK + MoltenVK | Working |
| Goat Simulator | DXVK + MoltenVK | Working |
| Nidhogg 2 | WineD3D OpenGL | Working |
| Terraria | FNA ARM64 | Working |
| Celeste | FNA x86 | Working |
| Undertale | Steam Native | Working |
| Stardew Valley | Steam Native | Working |
| Among Us | Steam Native | Working |
| Elden Ring | D3DMetal + MetalFX | Working |
| Sekiro | D3DMetal + MetalFX | Working |
| Baldur's Gate 3 | D3DMetal Perf | Working |
| Cyberpunk 2077 | D3DMetal Perf | Working |
| The Witcher 3 | D3DMetal Perf | Working |
| Dark Souls III | D3DMetal Perf | Working |
| 60+ more | D3DMetal Perf | Auto-routed |

Tested on Apple M4, macOS 26.

## Tech stack

| Layer | Technology |
|---|---|
| Desktop app | Electron + TypeScript |
| Backend | Rust HTTP server (tiny_http, port 9274) |
| Wine runtime | MetalSharp Wine 11.5 (from-source, gnutls TLS, DXMT builtins, 7 custom patches) |
| D3D11/D3D12 → Metal | DXMT v0.80+10 (LLVM 15 + Metal) |
| D3D9 → Metal | DXVK 1.10.3 + MoltenVK |
| Steam DRM | Goldberg Steam emulator |
| XNA/FNA | FNA + SDL3 + Mono |
| Pipeline routing | TOML rules + directory scan + PE header analysis |
| Bundles | zstd-compressed archives (runtime, DXMT, DXVK, Goldberg, Mono) |

## Architecture

```
app/
├── src-rust/           Rust backend — HTTP API, MTSP engine, game launch, Steam, installer
│   └── src/
│       ├── main.rs     HTTP router (port 9274)
│       ├── mtsp/
│       │   ├── engine.rs    Pipeline definitions (14 pipelines, DLL deploys, env vars)
│       │   ├── launcher.rs  Per-pipeline launch logic, Goldberg deploy, shader cache
│       │   ├── rules.rs    3-tier resolver (TOML → directory → PE analysis)
│       │   └── pe.rs       PE header parser (D3D API detection)
│       ├── setup.rs    Per-game preparation (shim builds, DLL staging, Goldberg)
│       ├── installer.rs Dependency installer (Wine, DXMT, Goldberg, Mono, Rosetta)
│       ├── launch.rs   Legacy engine detection
│       ├── steam.rs    Steam process management, steamapps watcher
│       ├── scan.rs     Game library scanner
│       └── updater.rs  Self-update via GitHub releases DMG
├── src/
│   ├── main/           Electron main process
│   └── renderer/       Electron renderer — library, setup wizard, pipeline dropdown
├── updater/
│   └── update.sh       Bash update script (zero dependencies)
└── bundles/            Pre-packaged runtime archives

src/                    C++ native engine (D3D11/D3D12/DXGI Metal implementations)
include/                C++ headers
tools/
├── dmg/                DMG packaging scripts
└── launcher/           Native launcher (C++ MetalSharp binary, Wine prefix management)
configs/
└── mtsp-rules.toml     AppID → pipeline mapping (50+ games)
```

## Documentation

- [docs/GAMES-SUPPORTED.md](docs/GAMES-SUPPORTED.md) — Full game compatibility, settings, and launch methods
- [docs/launch-architecture.md](docs/launch-architecture.md) — MTSP engine dispatch, launch paths, process lifecycle
- [docs/wine-architecture.md](docs/wine-architecture.md) — Wine runtime layout, WoW64, environment variables
- [docs/dxmt-vulkan-architecture.md](docs/dxmt-vulkan-architecture.md) — DXMT Metal and DXVK pipeline internals
- [docs/mtsp-roadmap.md](docs/mtsp-roadmap.md) — MTSP engine development roadmap

## Building from source

```bash
git clone https://github.com/aaf2tbz/metalsharp.git
cd metalsharp

# Rust backend
cd app/src-rust && cargo build --release && cd ../..

# Electron app
cd app && npm install && npm run build

# Dev mode
npm run start
```

```bash
# DMG packaging
./tools/dmg/create-bundles.sh
cd app && npx electron-builder --mac dmg --arm64 --publish never
```

## Contributing

Open an issue before contributing significant features. See [AGENTS.md](AGENTS.md) for code conventions, build commands, and PR checklist.

## License

MIT

---

[Releases](https://github.com/aaf2tbz/metalsharp/releases) ·
[Discussions](https://github.com/aaf2tbz/metalsharp/discussions) ·
[Issues](https://github.com/aaf2tbz/metalsharp/issues)
