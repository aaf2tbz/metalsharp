<div align="center">

# M E T A L S H A R P

**Run Windows Steam games on macOS with Metal. No VM, no CrossOver, no GPTK. Just play.**

<a href="https://github.com/aaf2tbz/metalsharp/actions"><img src="https://img.shields.io/github/actions/workflow/status/aaf2tbz/metalsharp/ci.yml?branch=main&style=for-the-badge" alt="CI"></a>
<a href="https://github.com/aaf2tbz/metalsharp/releases"><img src="https://img.shields.io/github/v/release/aaf2tbz/metalsharp?style=for-the-badge" alt="Release"></a>
<a href="https://github.com/aaf2tbz/metalsharp/discussions"><img src="https://img.shields.io/github/discussions/aaf2tbz/metalsharp?style=for-the-badge" alt="Discussions"></a>
<a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg?style=for-the-badge" alt="MIT"></a>

**v0.33 — Beta 5**<br />
Rebuilt from the ground up. New Vue 3 frontend, 5 consolidated engines, DXMT Metal without GPTK, seamless migration.

</div>

---

MetalSharp is a standalone D3D-to-Metal translation layer for running Windows
Steam games on Apple Silicon. It bundles a custom Wine 11.5 runtime with
DXMT (D3D11/D3D12 → Metal), Goldberg Steam emulator, and FNA/XNA support into
a single macOS app — no Game Porting Toolkit or CrossOver required.

Games are auto-detected and routed through the correct engine by the
**MTSP Engine**, a rules-based resolver that matches per-game TOML rules,
scans game directories for engine markers, and falls back to PE header analysis.
No manual configuration.

## What's new in Beta 5

**Rebuilt UI** — Vue 3 + Vite SPA with Steam-inspired dark/light themes. Collapsible sidebar, animated game cards, pipeline badges, live log viewer, and a 3-step setup wizard. The old 3800-line renderer is gone.

**5 engines, not 14** — Consolidated to DXMT Metal (D3D11), DXMT Metal 12 (D3D12), FNA Native, Steam, and Wine Bare. Auto-routed — no dropdowns.

**DXMT Metal without GPTK** — DXMT rebuilt from upstream with Wine 11.5 builtin integration. Rain World and others run on pure upstream Wine + DXMT Metal on Apple Silicon.

**Seamless upgrades** — Migration system detects older builds, preserves your Steam install and game library, reinstalls the runtime cleanly, and shows a progress overlay.

**Bulletproof stability** — All `unwrap()` panics eliminated. Clean shutdown handler. Shader cache pre-deployed. Async pipeline compilation. CI on every push.

## Engines

| Engine | API | How it works | Example games |
|--------|-----|--------------|---------------|
| **M11** DXMT Metal | D3D9/10/11 → Metal | DXMT translates D3D11/DXGI calls directly to Metal command buffers. Per-game shader cache. | Rain World, Schedule I, Subnautica BZ |
| **M12** DXMT Metal 12 | D3D12 → Metal | D3D12 translated to Metal via DXMT. Same pipeline as M11 with d3d12.dll injection. | Future D3D12 titles |
| **FNA ARM64** | XNA/FNA → Metal | Native Mono + FNA + SDL3 — no Wine. | Terraria |
| **Steam** | macOS native | Native launch via macOS Steam. Wine Steam games bridge achievements to the macOS client. | Among Us, Valheim |
| **Wine Bare** | Wine builtin | Plain Wine, no graphics overrides. Legacy D3D9 fallback. | — |

## Install

Download the latest DMG from [Releases](https://github.com/aaf2tbz/metalsharp/releases),
drag to `/Applications`, and launch. The setup wizard handles everything in 3 steps:

1. **Runtime** — extracts MetalSharp Wine 11.5 + DXMT Metal + Goldberg + Mono + pipeline rules (one Touch ID prompt)
2. **Steam** — installs and launches Windows Steam via Wine. Log in through the Steam window
3. **Play** — your library loads. Install and play.

### Upgrading from Beta 4

Install the new DMG over your existing copy. The built-in updater handles the transition:

1. Updater downloads and installs the new app
2. First launch detects the old runtime layout
3. Migration overlay appears — your games and Steam install are preserved
4. Runtime wiped and reinstalled from scratch
5. Restart and you're on Beta 5

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
  ├── 1. TOML rules       → exact appid→engine mapping (591+ rules)
  ├── 2. Directory scan   → engine markers (Unity, Unreal, .NET/FNA, D3D9)
  └── 3. PE analysis      → D3D API detection (D3D9/D3D11/D3D12)
      │
      ▼
Engine dispatch
  ├── M11: inject d3d11.dll + dxgi.dll + winemetal.dll → launch via Wine
  ├── M12: inject d3d12.dll + dxgi.dll + winemetal.dll → launch via Wine
  ├── FNA: native Mono + FNA launch (no Wine)
  ├── Steam: steam://run/ via macOS Steam
  └── Wine Bare: plain Wine launch
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
│   │   └── etc/dxmt.conf              DXMT config (MetalFX, framerate)
│   ├── goldberg/                       Steam emulator DLLs (x86 + x64)
│   ├── mono-arm64/                    ARM64 .NET games (Terraria)
│   └── shims/                         CSteamworks, FMOD stubs
├── prefix-steam/                      Wine prefix (Windows filesystem)
├── configs/
│   └── mtsp-rules.toml                AppID → engine mapping (591 rules)
├── shader-cache/
│   └── dxmt-metal/<appid>/             DXMT shader cache
└── logs/                              Launch logs
```

## Supported games

See [docs/GAMES-SUPPORTED.md](docs/GAMES-SUPPORTED.md) for full details.

| Game | Engine | Status |
|------|--------|--------|
| Rain World | DXMT Metal | Working |
| Rain World Downpour | DXMT Metal | Working |
| Schedule I | DXMT Metal | Working |
| Persona 3 Reload | DXMT Metal | Working |
| Subnautica: Below Zero | DXMT Metal 12 | Working |
| Terraria | FNA ARM64 | Working |
| Among Us | Steam Native | Working |
| Valheim | Steam Native | Working |
| 580+ more | Auto-routed | See game list |

Tested on Apple M4, macOS 26.

## Tech stack

| Layer | Technology |
|-------|------------|
| Desktop app | Electron + TypeScript |
| Frontend | Vue 3 + Vite SPA |
| Backend | Rust HTTP server (tiny_http, port 9274) |
| Wine runtime | MetalSharp Wine 11.5 (from-source, gnutls TLS, DXMT builtins, 5 custom patches) |
| D3D11/D3D12 → Metal | DXMT (LLVM 15 + Metal) |
| Steam DRM | Goldberg Steam emulator |
| XNA/FNA | FNA + SDL3 + Mono |
| Pipeline routing | TOML rules + directory scan + PE header analysis (MTSP Engine) |
| Migration | Version-stamped setup.json + standalone Cocoa migrator |

## Architecture

```text
app/
├── src-rust/               Rust backend — HTTP API, MTSP engine, game launch, Steam, migration
│   └── src/
│       ├── main.rs         HTTP router (port 9274)
│       ├── migrate.rs      Runtime migration (version detection, overlay, reinstall)
│       ├── mtsp/
│       │   ├── engine.rs   Engine definitions (5 engines, DLL deploys, env vars)
│       │   ├── launcher.rs Per-engine launch logic, Goldberg deploy, shader cache
│       │   ├── rules.rs    3-tier resolver (TOML → directory → PE analysis)
│       │   ├── pe.rs       PE header parser (D3D API detection)
│       │   └── shader_cache.rs  DXMT shader cache management
│       ├── installer.rs    Dependency installer (Wine, DXMT, Goldberg, Mono)
│       ├── setup.rs        Per-game preparation (shim builds, DLL staging)
│       ├── launch.rs       Engine dispatch (299 lines)
│       ├── steam.rs        Steam process management, steamapps watcher
│       ├── scan.rs         Game library scanner
│       ├── sharp_library/  EXE library management
│       └── updater.rs      Self-update via GitHub releases
├── src/
│   ├── main/               Electron main process
│   └── renderer/           Vue 3 + Vite SPA — library, setup wizard, settings, logs
├── updater/
│   └── update.sh           Bash update script
└── bundles/                Pre-packaged runtime archives

src/                        C++ native engine (D3D11/D3D12/DXGI Metal implementations)
include/                    C++ headers
tools/
├── dmg/                    DMG packaging scripts
├── launcher/               Native launcher (C++, Wine prefix management)
└── migrator/               Standalone Cocoa migration binary (Objective-C)
configs/
└── mtsp-rules.toml         AppID → engine mapping (591 rules)
scripts/                    Per-game launch and dependency scripts
shader-presets/             Pre-built DXMT shader caches
```

## Documentation

- [docs/GAMES-SUPPORTED.md](docs/GAMES-SUPPORTED.md) — Full game compatibility, settings, and launch methods
- [docs/launch-architecture.md](docs/launch-architecture.md) — MTSP engine dispatch, launch paths, process lifecycle
- [docs/wine-architecture.md](docs/wine-architecture.md) — Wine runtime layout, WoW64, environment variables
- [docs/dxmt-vulkan-architecture.md](docs/dxmt-vulkan-architecture.md) — DXMT Metal pipeline internals
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
