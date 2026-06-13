# M12 Pipeline Map

Last verified: 2026-06-13.

M12 is MetalSharp's D3D12 -> DXMT -> WineMetal -> Metal route for Wine-launched
games. It is not the native `metalsharp_d3d12` CMake target and it is not the
GPTK/D3DMetal route. The M12 contract is a full runtime pipeline: installed
bundles, Steam prefix initialization, game-local DXMT DLLs, Wine/DYLD routing,
shader-engine corpus material, focused logs, and developer probes all have to
agree.

## Source Of Truth

| Layer | Owner | Contract |
| --- | --- | --- |
| Route selection | `app/src-rust/src/mtsp/pe.rs`, `rules.rs`, `engine.rs` | 64-bit D3D12 imports map to `PipelineId::M12`; M12 is the public DXMT D3D12 route. |
| Runtime install | `app/src-rust/src/installer.rs`, `prefix_runtime.rs` | Installs current DXMT DLLs, Unix sidecars, Wine runtime files, and M12 shader-engine corpus material. |
| Runtime migration | `app/src-rust/src/migrate.rs`, `prefix_runtime.rs` | Updates preserve user settings, refresh `prefix-steam`, and repair stale/broken prefix runtime surfaces without reinstalling Steam. |
| Launch handoff | `app/src-rust/src/mtsp/launcher.rs` | Builds the game recipe, deploys game-local DLLs/sidecars, sets Wine routing, cache env, Steam identity, and M12 logs. |
| Shader cache | `app/src-rust/src/mtsp/shader_cache.rs` | Seeds M12 shader-engine material into `shader-cache/m12/<appid>/` from installed corpus sources. |
| Runtime implementation | `vendor/dxmt` artifacts staged into `~/.metalsharp/runtime/wine/lib/dxmt/` | Provides `d3d12.dll`, `dxgi.dll`, `d3d11.dll`, `d3d10core.dll`, `winemetal.dll`, `winemetal.so`, and native D3D12/DXIL/Metal behavior. |
| Machine contract | `tools/d3d12-metal-sdk/contracts/m12-pipeline-contract.json` | Defines required launch env, log location, pipeline stages, and developer gates. |
| Shader contract | `tools/d3d12-metal-sdk/contracts/d3d12-shader-engine-contract.json` | Defines DXIL intake, MSL lowering, binding manifests, PSO build, runtime binding, and offline gates. |

## Installed Runtime Surfaces

M12 depends on two runtime surfaces.

The installed runtime lives under:

```text
~/.metalsharp/runtime/wine/
|-- bin/metalsharp-wine
|-- lib/dxmt/x86_64-windows/
|   |-- d3d10core.dll
|   |-- d3d11.dll
|   |-- d3d12.dll
|   |-- dxgi.dll
|   |-- dxgi_dxmt.dll
|   |-- winemetal.dll
|   |-- nvapi64.dll
|   `-- nvngx.dll
|-- lib/dxmt/x86_64-unix/
|   |-- winemetal.so
|   |-- libc++.1.dylib
|   |-- libc++abi.1.dylib
|   `-- libunwind.1.dylib
|-- lib/wine/x86_64-windows/
|   |-- d3d9.dll
|   |-- d3d10.dll
|   `-- d3d10_1.dll
|-- lib/wine/x86_64-unix/
|   |-- ntdll.so
|   |-- winemac.so
|   `-- mscompatdb.so
|-- lib/metalsharp/x86_64-windows/
|   `-- metalsharp_ntdll_hook.dll
`-- share/d3d12-metal-sdk/shader-corpus/
```

The Steam prefix runtime surface is a copy of the files that Wine needs during
prefix init and Steam/game launch:

```text
~/.metalsharp/prefix-steam/
|-- drive_c/windows/system32/
|   |-- d3d9.dll
|   |-- d3d10.dll
|   |-- d3d10_1.dll
|   |-- d3d10core.dll
|   |-- d3d11.dll
|   |-- d3d12.dll
|   |-- dxgi.dll
|   |-- dxgi_dxmt.dll
|   |-- winemetal.dll
|   |-- nvapi64.dll
|   |-- nvngx.dll
|   `-- metalsharp_ntdll_hook.dll
`-- .metalsharp/unix/
    |-- winemetal.so
    |-- winemac.so
    |-- ntdll.so
    |-- libc++.1.dylib
    |-- libc++abi.1.dylib
    `-- libunwind.1.dylib
```

`prefix_runtime.rs` owns this file list. Install and migration call the same
staging and validation functions so the two paths cannot drift. Validation is
byte-for-byte against the installed runtime, not a name-only check. Wine init
timeout is fail-closed: a bounded `cmd /c exit` probe that has to be killed is a
runtime error, not a successful install.

## Install And Migration Contract

Fresh install and update both validate the Wine init surface.

Install order:

1. Runtime bundle and scripts/tools bundles are installed.
2. DXMT graphics runtime is installed and its manifest is checked.
3. `Wine Init Runtime Surface` stages the prefix surface into `prefix-steam`.
4. A bounded Wine probe runs `cmd /c exit` through `metalsharp-wine`.
5. The prefix surface is staged again in case Wine rewrote `system32`.
6. Prefix DLLs/sidecars and M12 shader-engine material are validated.

The shader-engine material check requires the checked-in corpus proof file:

```text
runtime/wine/share/d3d12-metal-sdk/shader-corpus/elden-ring-present-vb-pull-20260612/proof/SHA256SUMS
```

A partial corpus with only a random shader sidecar is not sufficient.

Migration order:

1. User settings, Steam prefix settings, bottle settings, compatdata metadata,
   and library metadata are preserved.
2. Runtime payload directories are refreshed.
3. Preserved settings are restored.
4. Existing prefixes are initialized with the current runtime surface.
5. Migration validates the prefix runtime surface and M12 shader-engine material
   before it marks the update complete.

Migration repair is update-only. If Wine init validation reports failure,
MetalSharp makes one deterministic repair pass:

1. Kill prefix-scoped Steam update/helper processes.
2. Repair `drive_c/windows/system32`, `.metalsharp/unix`, `drive_c`, and
   `dosdevices`.
3. Back up an invalid non-directory `dosdevices` path as
   `dosdevices.migration-repair-*`.
4. Recreate `dosdevices/c:` and remove root `z:` style mappings.
5. Clear only the staged runtime DLL/sidecar files.
6. Restage current runtime files, rerun the bounded probe, and validate again.

Migration does not reinstall Steam when `Steam.exe` already exists, and it does
not require `Steam.exe` for prefix runtime validation. Steam installation is a
separate user/install concern; updates only refresh the runtime surface Steam
and games load from.

Migration only skips runtime work when the core runtime and the prefix runtime
surface both validate. A runtime archive that exists but has stale
`prefix-steam` DLLs or missing M12 corpus proof still needs repair.

## Game Launch Flow

1. The PE scanner and route rules select `PipelineId::M12`.
2. `build_launch_recipe` resolves the executable, game directory, runtime DLLs,
   optional vendor stubs, Agility SDK payloads, and title-specific arguments.
3. `prepare_steam_pipeline_env` validates runtime files, cleans stale
   injections, seeds Steam identity, and stages Agility SDK files when needed.
4. `deploy_recipe_dlls` copies DXMT DLLs into the game directory and records
   `.metalsharp/injections.json`.
5. `deploy_prefix_route_dlls` stages the same route DLLs into
   `prefix-steam/drive_c/windows/system32`; i386 PE DLLs such as M9's 32-bit
   `d3d9.dll` go to `prefix-steam/drive_c/windows/syswow64`.
6. `stage_m12_unix_sidecars` copies `winemetal.so`, Wine sidecars, loader
   dylibs, and the validated Wine `mscompatdb.so` into the game directory,
   `unix/`, and `.metalsharp/unix/`.
7. `verify_m12_game_local_launch_path` checks that the cube-style game-local
   sidecar layout exists before launch.
8. The launcher sets the Wine/DXMT environment and launches the game executable
   directly while Wine Steam stays available for Steamworks state.

`POST /mtsp/prepare` follows the same staging contract without spawning Wine or
the game. It is a useful preflight only because it stages Agility, prefix-route
DLLs, game-local sidecars, Steam identity, and M12 path verification before
returning `ok`.

Game-local M12 DLLs include:

```text
d3d12.dll
dxgi.dll
dxgi_dxmt.dll
d3d11.dll
d3d10core.dll
winemetal.dll
nvapi64.dll
nvngx.dll
```

`d3d11.dll` and `d3d10core.dll` are intentionally present on M12. They are the
DXMT fallback surface for games or launchers that touch D3D11/D3D10 components
inside an otherwise D3D12 route.

## Required Environment

M12 launch env must include the following shape:

```text
WINEPREFIX=~/.metalsharp/prefix-steam
WINEDLLOVERRIDES=d3d12,dxgi,dxgi_dxmt,winemetal,nvapi64,nvngx,d3d11,d3d10core=n,b
WINEDLLPATH=~/.metalsharp/runtime/wine/lib/dxmt/x86_64-windows:...
DYLD_FALLBACK_LIBRARY_PATH=...runtime/wine/lib/dxmt/x86_64-unix:...
DXMT_CONFIG_FILE=~/.metalsharp/runtime/wine/etc/dxmt.conf
DXMT_WINEMETAL_UNIXLIB=winemetal.so
SteamAppId=<appid>
SteamGameId=<appid>
MS_GRAPHICS_BACKEND=dxmt-metal12
WINEMSYNC=1
```

For M12, `DXMT_WINEMETAL_UNIXLIB` is forced to the basename `winemetal.so` on
the game-local launch surface. This is the cube-proven method: the PE
`winemetal.dll` bridge resolves the app-local Unix sidecar by name instead of
being pinned to a stale absolute path.

## Logs, Caches, And Corpus

M12 keeps logs, shader cache, and pipeline cache separate.

```text
~/.metalsharp/logs/m12/<appid>/m12.log
~/.metalsharp/shader-cache/m12/<appid>/
~/.metalsharp/pipeline-cache/m12/<appid>/
```

Required log env:

```text
METALSHARP_LOG_DIR=~/.metalsharp/logs/m12/<appid>/
METALSHARP_M12_LOG_DIR=~/.metalsharp/logs/m12/<appid>/
DXMT_LOG_PATH=~/.metalsharp/logs/m12/<appid>/
DXMT_LOG_FILE=m12.log
```

Shader-engine corpus material is installed from:

```text
~/.metalsharp/runtime/wine/share/d3d12-metal-sdk/shader-corpus/
~/.metalsharp/runtime/d3d12-metal-sdk/shader-corpus/
~/.metalsharp/scripts/tools/d3d12-metal-sdk/shader-corpus/
```

`shader_cache.rs` copies `.metallib`, `.air`, `.msl`, `.dxbc`, `.dxil`, `.cso`,
`.json`, `.module.txt`, and `.dxil_report.txt` files into the selected M12
cache. Proof/log folders are intentionally not copied as runtime cache inputs.
The proof file still remains part of install/migration readiness because it
proves the packaged corpus is the expected source-controlled fixture.

## Developer And CI Gates

Use these gates before treating a D3D12 change as stable:

```bash
python3 tools/d3d12-metal-sdk/scripts/validate-m12-pipeline-contract.py
python3 tools/d3d12-metal-sdk/scripts/validate-shader-engine.py \
  --json tools/d3d12-metal-sdk/results/shader-engine-audit-metalsharp.json
cargo test --manifest-path app/src-rust/Cargo.toml m12_
cargo test --manifest-path app/src-rust/Cargo.toml prefix_runtime
cargo test --manifest-path app/src-rust/Cargo.toml migration_prefix_runtime
tools/ci/m12-check.sh
M12_CHECK_RUN_LIVE=1 tools/ci/m12-check.sh
tools/d3d12-metal-sdk/scripts/m12-dev.sh stress-game
```

`tools/ci/m12-check.sh` is the PR gate. Non-live mode rebuilds/stages the M12
runtime and builds `m12_game.exe`. Live mode runs the 10-second cube proof.

`m12_stress_game.exe` is the higher-pressure developer executable. It includes a
splash/movie-style pass and a dense beach scene intended to exercise textures,
vertices, awkward geometry, PSOs, draws, presents, and readback without
requiring a commercial game launch.

## Boundaries

- M12 is the DXMT route. D3DMetal/GPTK is separate.
- M12 Wine games present through DXMT/WineMetal, not the native
  Cocoa/CAMetalLayer viewer.
- Live game runs are diagnostics. Contract status comes from source contracts,
  Rust launch tests, SDK probes, corpus replay, Metal compilation, and CI gates.
- Do not paper over M12 launch failures by falling back to the Steam client-only
  path. The normal route is a prepared Wine prefix plus direct game executable
  launch with Wine Steam available in the background.
