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
│       ├── installer.rs         Dependency installer (split bundles, Rosetta, Xcode CLI, GPTK, Mono)
│       ├── migrate.rs           Runtime migration + settings-only preservation and prefix runtime repair
│       ├── prefix_runtime.rs    Shared Wine prefix runtime surface for install/migration validation
│       ├── scan.rs              Game library scanner (Steam appmanifest parsing, wine path resolution)
│       ├── sharp_library.rs     Sharp Library imports, installer launch, bottle app imports
│       └── updater.rs           Self-update via GitHub releases DMG download
├── src/
│   ├── main/                    Electron main process
│   └── renderer/                Electron renderer (UI, library, setup wizard)
├── bundles/                     Split release bundles downloaded from the `bundles` GitHub release
├── updater/                     Python update runtime script
├── package.json                 Electron app manifest
└── src-rust/Cargo.toml          Rust backend manifest

src/                             Native host shims and legacy MetalSharp graphics/runtime components
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

vendor/dxmt/                     Active DXMT Metal runtime used by M9/M10/M11/M12
├── src/d3d12/                   M12 D3D12 implementation, DXIL/MSL lowering, PSO/binding code
├── src/d3d11/                   M11 D3D11 implementation
├── src/d3d10/                   M10 D3D10 handoff
├── src/dxgi/                    Shared DXGI bridge and dxgi_dxmt sidecar
├── src/winemetal/               PE winemetal.dll and Unix winemetal.so bridge
└── tests/d3d12_game/            M12 cube/stress executables and launch harnesses

tools/d3d12-metal-sdk/           M12 contracts, probes, shader corpora, offline gates, SDK package source
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

Modern runtime paths use MTSP pipeline ids and bottle profiles. Steam games get `steam_<appid>` bottles that are launch-authoritative for runtime checks. Wine Steam remains the live background Steam client; env-dependent Steam routes launch the game executable directly with the bottle prefix, route env, and Steam identity variables instead of trying to make an already-running Steam process inherit new env.

| Public route | Method | Example games |
|--------|--------|--------------|
| `M9` | D3D9 / 32-bit capable DXMT-family route | Nidhogg 2, Undertale, Blasphemous, Dave the Diver |
| `M10` | D3D10 to Metal | D3D10 apps/games |
| `M11` | D3D11 to Metal | Rain World, Schedule I, Subnautica BZ |
| `M12` | D3D12 to Metal | Peak, Silksong, D3D12 investigation titles |
| `Mono/FNA` | Windows XNA/FNA through native Mono, staged FNA/XNA assemblies, and host shims | Celeste, Terraria |

Internal route ids such as `dxmt`, `wine_bare`, `m32`, `steam`, `macos_steam`, and `m13` stay backend-parseable for legacy records, diagnostics, and fallback behavior, but they are not normal bottle route buttons.

### M12 Runtime Contract

M12 is a DXMT/WineMetal route, not GPTK/D3DMetal. A merge-ready M12 change must keep these surfaces aligned:

- Release bundles: `metalsharp-runtime.tar.zst`, `metalsharp-graphics-dll.tar.zst`, and `metalsharp-d3d12-developer-sdk.tar.zst`.
- Installed runtime: `~/.metalsharp/runtime/wine/lib/dxmt/`, `lib/wine/`, `lib/metalsharp/`, and `share/d3d12-metal-sdk/shader-corpus/`.
- Prefix runtime surface: `~/.metalsharp/prefix-steam/drive_c/windows/system32/`, `syswow64/` for i386 PE DLLs, and `~/.metalsharp/prefix-steam/.metalsharp/unix/`.
- Game-local launch surface: DXMT DLLs next to the selected executable plus M12 Unix sidecars staged game-local, under `unix/`, and under `.metalsharp/unix/`.
- Logs and caches: `~/.metalsharp/logs/m12/<appid>/m12.log`, `shader-cache/m12/<appid>/`, and `pipeline-cache/m12/<appid>/`.

`POST /mtsp/prepare` is a preflight/staging endpoint for this contract. For M12 it must stage Agility, prefix-route DLLs, game-local sidecars, Steam identity, and the same launch-critical assets that the real launch path needs. It should never report `ok` for a route that would fail immediately because sidecars or prefix DLLs are missing.

Prefix init uses `metalsharp-wine cmd /c exit`, not `wineboot`. A timeout is a failure, not success. Install and migration must validate the prefix runtime surface and M12 shader corpus proof before marking setup complete.

### Key Paths at Runtime

- Wine runtime: `~/.metalsharp/runtime/wine/`
- Wine prefix: `~/.metalsharp/prefix-steam/`
- DXMT PE DLLs: `~/.metalsharp/runtime/wine/lib/dxmt/x86_64-windows/`
- DXMT Unix sidecars: `~/.metalsharp/runtime/wine/lib/dxmt/x86_64-unix/`
- MetalSharp hook DLLs: `~/.metalsharp/runtime/wine/lib/metalsharp/x86_64-windows/`
- M12 shader corpus: `~/.metalsharp/runtime/wine/share/d3d12-metal-sdk/shader-corpus/`
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
- `POST /steam/launch-game` — Steam game launch with bottle preflight; env-dependent routes keep Wine Steam alive and spawn the game through the selected MTSP pipeline
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
./tools/bundles/verify-bundles.sh --bundle-dir app/bundles --require mac
./tools/bundles/verify-developer-sdk.sh app/bundles/metalsharp-d3d12-developer-sdk.tar.zst
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

`M12 Check` is the PR gate for the D3D12 DXMT route. Non-live mode rebuilds and stages the M12 runtime and `m12_game.exe`; local live mode is available with `M12_CHECK_RUN_LIVE=1 tools/ci/m12-check.sh`.

## Version Bumping

Five files must be updated together for a version bump:

| File | Field |
|------|-------|
| `app/package.json` | `"version": "X.Y.Z"` |
| `app/package-lock.json` | root/package lock `"version": "X.Y.Z"` |
| `app/src-rust/Cargo.toml` | `version = "X.Y.Z"` |
| `app/src-rust/Cargo.lock` | `metalsharp-backend` package `version = "X.Y.Z"` |
| `CMakeLists.txt` | `project(metalsharp VERSION X.Y.Z ...)` |

The Rust backend reads its version from `CARGO_PKG_VERSION` (set by Cargo.toml). The CI release workflow (`ci.yml`) reads the version from the git tag — if the tag version differs from `package.json`, it rewrites `package.json` before building.

### Tag and release process

```bash
# 1. Update all synchronized version files to the new version
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
- M12 contract gates:
  ```bash
  python3 tools/d3d12-metal-sdk/scripts/validate-m12-pipeline-contract.py
  python3 tools/d3d12-metal-sdk/scripts/validate-shader-engine.py \
    --json tools/d3d12-metal-sdk/results/shader-engine-audit-metalsharp.json
  python3 tools/d3d12-metal-sdk/scripts/build-metal-shader-corpus.py --clean
  python3 tools/d3d12-metal-sdk/scripts/verify-built-metal-shader-corpus.py dist/d3d12-metal-shaders
  tools/ci/m12-check.sh
  tools/d3d12-metal-sdk/scripts/m12-dev.sh stress-game
  ```
- Bundle contract gates:
  ```bash
  tools/bundles/verify-bundles.sh --bundle-dir app/bundles --require mac
  tools/bundles/verify-developer-sdk.sh app/bundles/metalsharp-d3d12-developer-sdk.tar.zst
  ```
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
- **Celeste (504230) and Terraria (105600) are Mono/FNA** — they use the native Mono/XNA/FNA lane with Steamworks/audio/native-library shims.
- **Goat Simulator (265930) is currently an M9/D3D9 investigation target** — it still needs native .NET 4.0 CLR work before it can be promoted.
- **CMakeLists.txt version must match Cargo.toml and package.json** — all three are independently read
- **app/package-lock.json version must match package.json** — npm package metadata and release automation both see it
- **Steam game bottles are launch-authoritative, but Steam stays alive as the client** — bottles preflight and bind runtime assets; env-dependent routes spawn the game process with `SteamAppId`/`SteamGameId` while Wine Steam remains connected
- **Installer bottles use their own prefixes** — apps imported from installer bottles must keep `bottle_id` so Sharp Library launches them from that bottle
- **Linux Docker DEB builds can leave `dist/` root-owned** — `tools/linux/create-release-tarballs.sh` repairs ownership before writing `dist/packages`
- **`winemetal.so` has no i386-unix version** — it uses WoW64 thunks for 32-bit PE clients, always lives in x86_64-unix/
- **Steam auto-updates overwrite the steamwebhelper wrapper** — `deploy_steamwebhelper_wrapper()` handles this but it's a known pain point
- **Do not treat a Wine init timeout as success** — install/migration must fail and report the prefix runtime issue after killing the probe.
- **M12 shader corpus proof is required** — `runtime/wine/share/d3d12-metal-sdk/shader-corpus/elden-ring-present-vb-pull-20260612/proof/SHA256SUMS` is part of the runtime bundle contract.
- **Repair release bundles from the real `bundles` release asset** — patch `metalsharp-runtime.tar.zst` with current backend/host/mscompatdb/corpus, verify, then `gh release upload bundles ... --clobber`.
- **M9 i386 PE DLLs go to `syswow64` in prefix route staging** — do not copy `lib/wine/i386-windows/d3d9.dll` into `system32`.
- **`mscompatdb.so` for M12 comes from `lib/wine/x86_64-unix`** — the launch sidecar resolver validates and stages the Wine hook copy, not an arbitrary DXMT-side copy.
