# AGENTS.md

Guide for AI agents working on the MetalSharp repository.

## What This Project Is

MetalSharp is a macOS app that runs Windows Steam games via Wine + Metal translation. It's an Electron app with a Rust HTTP backend, a C++ native D3D/Metal engine, and per-game engine routing that picks the right graphics pipeline automatically.

## Repository Structure

```
app/
├── src-rust/                    Rust HTTP backend (tiny_http server on port 9274)
│   └── src/
│       ├── main.rs              HTTP router — all /launch, /steam/*, /setup/*, /config, /logs endpoints
│       ├── launch.rs            Engine detection + game launch — the core routing logic
│       ├── steam.rs             Steam process management, library, install/uninstall, CEF wrapper
│       ├── setup.rs             Per-game preparation (shim builds, DLL staging, FNA runtime)
│       ├── installer.rs         Dependency installer (Wine bundle, Rosetta, Xcode CLI, GPTK, Mono)
│       ├── scan.rs              Game library scanner (Steam appmanifest parsing, wine path resolution)
│       └── updater.rs           Self-update via GitHub releases DMG download
├── src/
│   ├── main/                    Electron main process
│   └── renderer/                Electron renderer (UI, library, setup wizard)
├── bundles/                     Pre-packaged deps (metalsharp_bundle.tar.zst, SteamSetup.exe, etc.)
├── updater/                     Python update runtime script
├── package.json                 Electron app manifest
└── src-rust/Cargo.toml          Rust backend manifest

src/                             C++ native D3D11/D3D12/DXGI/XAudio2/XInput → Metal implementations
├── d3d/d3d11/                   D3D11 device, context, shaders, resources
├── d3d/d3d12/                   D3D12 device, command queue, command list, resources
├── dxgi/                        DXGI factory, adapter, swap chain
├── metal/                       Metal device, command queue, pipeline, shader translation
├── audio/                       XAudio2 → CoreAudio backend
├── input/                       XInput → GameController backend
├── perf/                        Shader cache, pipeline cache, MetalFX upscaler, GPU profiler
├── runtime/                     PE hooks, compat database, crash diagnostics, DRM detector
├── loader/                      Native PE loader + Win32 shims (kernel32, user32, etc.)
├── wine/                        Wine-specific integration code
├── steam/                       Steam integration
├── win32/                       Win32 API shims (kernel32, user32, registry, etc.)
└── fna/                         FNA/XNA game support (Terraria, Celeste shims)

include/                         C++ public headers
tests/                           C++ test suite (20+ tests: D3D11, D3D12, DXBC, Metal, audio, input)
tools/
├── launcher/                    Native launcher (metalsharp binary + Wine prefix management)
└── dmg/                         DMG packaging scripts
scripts/                         Per-game setup scripts (setup-rainworld-deps.sh, etc.)
configs/                         Mono DLL maps for FNA games
docs/                            Architecture docs + game compatibility matrix
CMakeLists.txt                   C++ build (native engine + tests)
install.sh                       CLI build script (cmake + test runner)
```

## Key Concepts

### Engine Routing (launch.rs)

10 engine types. Each game maps to one via hardcoded appid or directory scan fallback:

| Engine | Method | Example Games |
|--------|--------|--------------|
| `DxmtMetal` | Direct exe, DXMT DLLs into game dir | Rain World, Schedule I, Subnautica BZ |
| `DxmtMetal12` | Same + d3d12.dll | RE4 (setup), future D3D12 games |
| `DxvkMetal32` | Direct exe, DXVK d3d9.dll + MoltenVK | Portal 2, Goat Simulator |
| `Wined3d32` | Direct exe, Wine builtin OpenGL | Nidhogg 2 |
| `MetalsharpWine` | Bare Wine, no overrides | Legacy D3D9 games |
| `SteamD3DMetalPerf` | `steam://run/` + GPTK D3DMetal | Celeste, RE4, 70+ games |
| `SteamMetalfx` | `steam://run/` + MetalFX env vars | Elden Ring, Sekiro |
| `SteamBare` | `steam://run/`, no extras | Among Us, Valheim |
| `FnaArm64` | Native Mono ARM64 | Terraria |
| `FnaX86` | Rosetta Mono x86 | (reserved, no current mapping) |

### Key Paths at Runtime

- Wine runtime: `~/.metalsharp/runtime/wine/`
- Wine prefix: `~/.metalsharp/prefix-steam/`
- DXMT PE DLLs: `~/.metalsharp/runtime/wine/lib/dxmt/x86_64-windows/`
- DXVK i386 DLLs: `~/.metalsharp/runtime/wine/lib/dxvk/i386-windows/`
- MoltenVK ICD: `~/.metalsharp/runtime/wine/etc/vulkan/icd.d/MoltenVK_icd.json`
- DXMT config: `~/.metalsharp/runtime/wine/etc/dxmt.conf`
- Shader cache: `~/.metalsharp/shader-cache/<engine>/<appid>/`
- Game local copies: `~/.metalsharp/games/<appid>/`
- Logs: `~/.metalsharp/logs/`

### HTTP API (main.rs)

Backend listens on `127.0.0.1:9274` (override with `METALSHARP_PORT`). Key endpoints:

- `POST /game/launch-auto` — launch game by appid with engine auto-detection
- `POST /game/prepare` — prepare game runtime (shims, DLLs, config)
- `GET /steam/library` — full game library (owned + installed)
- `POST /steam/launch` — start Wine Steam
- `POST /steam/stop` — kill Wine Steam + wineserver
- `GET /setup/dependencies` — check which deps are installed
- `POST /setup/install-all` — run full dependency installer
- `POST /kill` — kill game by pid or appid
- `GET /logs` — recent log entries

## Build Commands

### Rust backend
```bash
cd app/src-rust && cargo build --release
```

### Electron app
```bash
cd app && npm install && npm run build
```

### Everything (Rust + TypeScript)
```bash
cd app && npm run build:all
```

### C++ native engine + tests
```bash
./install.sh
# or manually:
mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build . --parallel $(sysctl -n hw.ncpu)
ctest --output-on-failure
```

### DMG packaging
```bash
./tools/dmg/create-bundles.sh
cd app && npx electron-builder --mac dmg --arm64
```

## CI Workflows

Four separate CI pipelines run on push/PR to main:

| Workflow | Triggers | What it does |
|----------|----------|-------------|
| `ci-rust.yml` | `app/src-rust/**` changes | `cargo fmt --check`, `cargo clippy`, `cargo build --release`, `cargo test`, `cargo audit` |
| `ci-cpp.yml` | `src/**`, `include/**`, `tests/**`, `CMakeLists.txt` | clang-format check, cmake build, ctest, clang-tidy |
| `ci-js.yml` | `app/src/**`, `app/package.json` | npm install, TypeScript build, biome lint |
| `ci-python.yml` | `app/updater/**` | Python linting |
| `ci.yml` | All pushes, tags `v*` | Full build + DMG packaging. On tag push: creates GitHub Release with DMG |

## Version Bumping

Three files must be updated together for a version bump:

| File | Field |
|------|-------|
| `app/package.json` | `"version": "X.Y.Z"` |
| `app/src-rust/Cargo.toml` | `version = "X.Y.Z"` |
| `CMakeLists.txt` | `project(metalsharp VERSION X.Y.Z ...)` |

The Rust backend reads its version from `CARGO_PKG_VERSION` (set by Cargo.toml). The CI release workflow (`ci.yml`) reads the version from the git tag — if the tag version differs from `package.json`, it rewrites `package.json` before building.

### Tag and release process

```bash
# 1. Update all three version files to the new version
# 2. Commit and push to main
# 3. Create and push the tag
git tag v<X.Y.Z>
git push origin v<X.Y.Z>
# 4. CI builds the DMG and creates a GitHub Release automatically
```

The `ci.yml` release job only triggers on tag pushes matching `v*`. It builds the full app, packages the DMG, and uploads it to a new GitHub Release with auto-generated release notes.

The updater module (`updater.rs`) checks for new releases by hitting `https://api.github.com/repos/aaf2tbz/metalsharp/releases/latest` and comparing the tag to `CARGO_PKG_VERSION`.

## Suggested Tests Before Committing

### Rust changes
```bash
cd app/src-rust
cargo fmt --all -- --check     # must pass (CI enforces)
cargo clippy --all-targets -- -D warnings  # must pass (CI enforces)
cargo test                     # unit tests
cargo build --release          # verify build
```

### C++ changes
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build . --parallel $(sysctl -n hw.ncpu)
ctest --output-on-failure
```

### TypeScript/Electron changes
```bash
cd app
npm run build                  # tsc + copy assets
npx biome check src/           # lint (CI enforces)
```

### Integration sanity
- `POST /setup/install-all` → verify progress endpoint shows completion
- `GET /steam/status` → verify returns valid JSON
- `POST /game/launch-auto` with a known appid → verify correct engine selected (check logs)
- `GET /steam/library` → verify games appear with correct launch_method

## Common Pitfalls

- **Don't edit the Wine plist before running `daemon start`** — let signet create it first, then patch HOME and kickstart
- **DXVK i386 DLLs are at `lib/dxvk/i386-windows/`**, NOT `lib/wine/i386-windows/` — the latter is for Wine builtins
- **Shader cache is per-appid** (`~/.metalsharp/shader-cache/<engine>/<appid>/`), not per-exename
- **Celeste (504230) is SteamD3DMetalPerf**, not FnaX86 — it uses Steam DRM + GPTK D3DMetal
- **Portal 2 (620) and Goat Simulator (265930) are DxvkMetal32**, not SteamD3DMetalPerf
- **SteamD3DMetalPerf requires GPTK installed** at `/Applications/Game Porting Toolkit.app/` — it sets WINEDLLPATH to GPTK's d3d11.dll
- **CMakeLists.txt version must match Cargo.toml and package.json** — all three are independently read
- **`winemetal.so` has no i386-unix version** — it uses WoW64 thunks for 32-bit PE clients, always lives in x86_64-unix/
- **Steam auto-updates overwrite the steamwebhelper wrapper** — `deploy_steamwebhelper_wrapper()` handles this but it's a known pain point
