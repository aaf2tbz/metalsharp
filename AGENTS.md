# AGENTS.md

Guide for AI agents working on the MetalSharp repository.

## What This Project Is

MetalSharp is a macOS app that runs Windows Steam games and Windows programs via Wine + Metal translation. It's an Electron app with a Rust HTTP backend, a C++ native D3D/Metal engine, per-game engine routing, runtime bottles, installer profiles, and Linux `.deb` packaging.

## Repository Structure

```
app/
├── src-rust/                    Rust HTTP backend (tiny_http server on port 9274)
│   └── src/
│       ├── main.rs              HTTP router — all /launch, /steam/*, /setup/*, /config, /logs endpoints
│       ├── bottles.rs           Runtime bottles, installer profiles, runtime doctor, redist/source checks
│       ├── launch.rs            Engine detection + game launch — the core routing logic
│       ├── steam.rs             Steam process management, library, install/uninstall, CEF wrapper
│       ├── setup.rs             Per-game preparation (shim builds, DLL staging, FNA runtime)
│       ├── installer.rs         Dependency installer (Wine bundle, Rosetta, Xcode CLI, GPTK, Mono)
│       ├── migrate.rs           Runtime migration + preservation of Steam/prefix/game/bottle state
│       ├── scan.rs              Game library scanner (Steam appmanifest parsing, wine path resolution)
│       ├── sharp_library.rs     Sharp Library imports, installer launch, bottle app imports
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
├── dmg/                         DMG packaging scripts
└── linux/                       DEB/Docker/runtime tarball/GHCR package scripts
scripts/                         Per-game setup scripts (setup-rainworld-deps.sh, etc.)
configs/                         Mono DLL maps for FNA games
docs/                            Architecture docs + game compatibility matrix
CMakeLists.txt                   C++ build (native engine + tests)
install.sh                       CLI build script (cmake + test runner)
```

## Key Concepts

### MTSP Routing and Runtime Bottles

Modern runtime paths use MTSP pipeline ids and bottle profiles. Steam games get `steam_<appid>` bottles that are launch-authoritative for runtime checks, while Wine Steam remains the live launcher/session owner for `steam://run`.

| Pipeline | Method | Example Games |
|--------|--------|--------------|
| `M9` | D3D9 / 32-bit capable DXMT-family route | Portal 2-class D3D9 titles, 32-bit installers |
| `M10` | D3D10 to Metal | D3D10 apps/games |
| `M11` | D3D11 to Metal | Rain World, Schedule I, Subnautica BZ |
| `M12` | D3D12 to Metal | RE4-class D3D12 titles |
| `M32` | 32-bit Wine fallback | legacy 32-bit apps |
| `Steam` | Wine Steam `steam://run/` with bottle preflight | Steam games |
| `MacOS Steam` | Native macOS Steam handoff | user-confirmed native Steam flow |
| `Wine` | Plain Wine custom-app fallback | Sharp Library apps |
| `Native macOS` | Native Mono/FNA/XNA path | Terraria/FNA-class apps |

### Key Paths at Runtime

- Wine runtime: `~/.metalsharp/runtime/wine/`
- Wine prefix: `~/.metalsharp/prefix-steam/`
- DXMT PE DLLs: `~/.metalsharp/runtime/wine/lib/dxmt/x86_64-windows/`
- DXVK i386 DLLs: `~/.metalsharp/runtime/wine/lib/dxvk/i386-windows/`
- MoltenVK ICD: `~/.metalsharp/runtime/wine/etc/vulkan/icd.d/MoltenVK_icd.json`
- DXMT config: `~/.metalsharp/runtime/wine/etc/dxmt.conf`
- Local redistributables: `~/.metalsharp/runtime/redist/`
- Runtime bottles: `~/.metalsharp/bottles/<bottle_id>/`
- Bottle prefix: `~/.metalsharp/bottles/<bottle_id>/prefix/`
- Bottle manifest: `~/.metalsharp/bottles/<bottle_id>/bottle.json`
- Bottle logs: `~/.metalsharp/bottles/<bottle_id>/logs/`
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
- `POST /steam/launch-game` — Steam game launch through Wine Steam with bottle preflight
- `POST /steam/runtime-doctor` — inspect a Steam game's bottle/runtime readiness
- `GET /sharp-library` — Sharp Library apps and imported Windows programs
- `POST /sharp-library/install` — Install Windows Program flow for EXE/MSI/installer bottles
- `POST /sharp-library/import-bottle-app` — import detected app candidates from a bottle
- `GET /bottles` — list runtime bottles
- `GET /bottles/profiles` — list supported bottle runtime profiles
- `GET /bottles/compatibility-matrix` — bottle compatibility cases
- `GET /bottles/redist-sources` — local redistributable source status
- `POST /bottles/doctor` — diagnose bottle readiness
- `POST /bottles/prepare` — prepare runtime assets/components for a bottle
- `POST /bottles/repair-component` — repair missing runtime components
- `POST /bottles/set-runtime-profile` — change a bottle profile
- `POST /bottles/set-windows-version` — apply a Wine Windows-version mode
- `POST /bottles/relaunch-installer` — relaunch an installer bottle's source installer
- `GET /setup/dependencies` — check which deps are installed
- `POST /setup/install-all` — run full dependency installer
- `POST /kill` — kill game by pid or appid
- `GET /logs` — recent log entries
- `GET /logs/stream` — streaming logs
- `GET /logs/crash-reports` — discovered crash report metadata

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

### Linux DEB and package assets
```bash
cd app && npm run deb
cd app && npm run deb:docker
tools/linux/create-release-tarballs.sh
tools/linux/publish-oci-packages.sh
```

## CI Workflows

Current CI is split between PR smoke coverage and tag/main release packaging:

| Workflow | Triggers | What it does |
|----------|----------|-------------|
| `pr-ci.yml` | PRs to `main` | Metal CI, Vue CI, Rust CI, Electron CI, C/C++/Obj-C CI, Docker Package Smoke |
| `ci.yml` | pushes to `main`, tags `v*` | Full DMG build, Linux DEB build, Linux runtime tarballs, GHCR package publish, release artifact upload on tags |
| `publish-linux-packages.yml` | manual | Re-publish Linux DEB/runtime release assets to GHCR with ORAS |

## Version Bumping

Four files must be updated together for a version bump:

| File | Field |
|------|-------|
| `app/package.json` | `"version": "X.Y.Z"` |
| `app/package-lock.json` | root/package lock `"version": "X.Y.Z"` |
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

The `ci.yml` release job only triggers on tag pushes matching `v*`. It builds the full app, packages the DMG and DEB, creates Linux runtime tarballs, publishes Linux OCI package assets, and uploads release artifacts with auto-generated release notes.

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
- `POST /steam/runtime-doctor` with a numeric appid → verify bottle checks/components are returned
- `GET /bottles` and `GET /bottles/redist-sources` → verify bottle metadata loads

## Common Pitfalls

- **Don't edit the Wine plist before running `daemon start`** — let signet create it first, then patch HOME and kickstart
- **DXVK i386 DLLs are at `lib/dxvk/i386-windows/`**, NOT `lib/wine/i386-windows/` — the latter is for Wine builtins
- **Shader cache is per-appid** (`~/.metalsharp/shader-cache/<engine>/<appid>/`), not per-exename
- **Celeste (504230) is SteamD3DMetalPerf**, not FnaX86 — it uses Steam DRM + GPTK D3DMetal
- **Portal 2 (620) and Goat Simulator (265930) are DxvkMetal32**, not SteamD3DMetalPerf
- **SteamD3DMetalPerf requires GPTK installed** at `/Applications/Game Porting Toolkit.app/` — it sets WINEDLLPATH to GPTK's d3d11.dll
- **CMakeLists.txt version must match Cargo.toml and package.json** — all three are independently read
- **app/package-lock.json version must match package.json** — npm package metadata and release automation both see it
- **Steam game bottles are launch-authoritative, but Steam stays the launcher** — bottles preflight and bind runtime assets, but Wine Steam must remain alive for Steam-connected games
- **Installer bottles use their own prefixes** — apps imported from installer bottles must keep `bottle_id` so Sharp Library launches them from that bottle
- **Linux Docker DEB builds can leave `dist/` root-owned** — `tools/linux/create-release-tarballs.sh` repairs ownership before writing `dist/packages`
- **`winemetal.so` has no i386-unix version** — it uses WoW64 thunks for 32-bit PE clients, always lives in x86_64-unix/
- **Steam auto-updates overwrite the steamwebhelper wrapper** — `deploy_steamwebhelper_wrapper()` handles this but it's a known pain point
