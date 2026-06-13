# M12 Developer Assets

M12 work should leave behind a reusable asset instead of a one-off command. The
developer SDK keeps those assets in predictable places:

- `scripts/`: build, stage, validate, replay, and probe entrypoints.
- `probes/`: small Windows D3D12 executables for one behavior at a time.
- `shader-corpus/`: checked-in game-derived MSL/AIR/metallib proof corpora.
- `contracts/`: expected runtime, shader, DXGI, feature, and ABI behavior.
- `results/`: generated proof from local runs.
- `/Volumes/AverySSD/MetalSharp-M12-CorpusLab`: large local shader-lab output.

Use the wrapper first:

```bash
tools/d3d12-metal-sdk/scripts/m12-dev.sh help
```

## Build And Stage

```bash
tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime
tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime
```

`build-runtime` rebuilds the DXMT x86_64 D3D12 artifacts. `stage-runtime`
copies the matching DLLs and Unix sidecars into the SDK runtime layout and
writes a stage result JSON.

The runtime layout has to match the application M12 contract:

- Windows DLLs live under `runtime/wine/lib/dxmt/x86_64-windows/`.
- Unix sidecars live under `runtime/wine/lib/dxmt/x86_64-unix/`.
- Wine fallback DLLs and Unix modules live under `runtime/wine/lib/wine/`.
- Prefix validation copies the same files into `prefix-steam` through
  `app/src-rust/src/prefix_runtime.rs`.
- M12 shader corpus material lives under
  `runtime/wine/share/d3d12-metal-sdk/shader-corpus/`.
- The installed corpus must include
  `elden-ring-present-vb-pull-20260612/proof/SHA256SUMS` plus runtime-safe
  shader material. This is an install/migration readiness proof, not a cache
  input.

When adding new runtime files, update the source contract first, then the stage
script, then the docs. Do not add one-off copies only to a game directory; that
is how launch drift reappears.

## Fast Local Proof

```bash
tools/d3d12-metal-sdk/scripts/m12-dev.sh mini
```

The mini suite is the default first proof when touching loader, device,
descriptor, PSO, compute, texture, swapchain, or shader-cache behavior. It
does not launch Steam or a game.

## Full Offline Gate

```bash
tools/d3d12-metal-sdk/scripts/m12-dev.sh full-offline
```

The full gate rebuilds, stages, validates contracts, validates runtime layout,
runs probes, compares contract results, validates the probe matrix, and checks
the shader-engine contract. This is the developer merge gate for offline M12
runtime work.

## Shader Lab

Captured game corpora should be staged on AverySSD:

```bash
tools/d3d12-metal-sdk/scripts/m12-dev.sh shader-lab -- \
  --corpus "$HOME/.metalsharp/tmp/subnautica2_m12_offline/sm6-20260612-225622/shader-cache" \
  --name subnautica2-sm6
```

The shader lab regenerates MSL when the native DXMT converter is available,
compiles MSL to AIR, links metallibs, and runs a native Metal framework harness
that loads each metallib and creates compute pipeline states for kernel
functions. It writes `manifest.json`, `summary.json`, and per-shader logs into
the staged run directory.

## Runtime Cube Gate

```bash
tools/d3d12-metal-sdk/scripts/m12-dev.sh m12-check
M12_CHECK_RUN_LIVE=1 tools/d3d12-metal-sdk/scripts/m12-dev.sh m12-check
```

The non-live mode is the PR CI gate. The live mode runs the 10-second RGB cube
scene and validates readback.

This gate proves the cube-method launch surface:

```text
DXMT_WINEMETAL_UNIXLIB=winemetal.so
WINEDLLPATH=<staged dxmt windows dll dir>:...
DXMT_LOG_PATH=<m12 log dir>/
DXMT_LOG_FILE=m12.log
```

The basename `winemetal.so` value is intentional. It keeps the PE
`winemetal.dll` bridge aligned with the app-local Unix sidecar layout used by
M12 game launches.

## M12 Stress Game

```bash
tools/d3d12-metal-sdk/scripts/m12-dev.sh stress-game
```

This builds, stages, and runs `m12_stress_game.exe` through the same working
M12 game-local runtime recipe as the cube harness. The executable behaves more
like a title than a probe: it opens with a 5-second splash/movie-style
fullscreen pass, then enters a dense 3D stress scene until 15 seconds by
default. Override the duration with:

```bash
M12_STRESS_SECONDS=6 tools/d3d12-metal-sdk/scripts/m12-dev.sh stress-game
```

Use the stress game before returning to commercial game launches when changing
any of these surfaces:

- DXIL to MSL lowering
- shader binding manifests
- root signatures and descriptor tables
- graphics or compute PSO creation
- vertex/index binding
- texture sampling
- render target and present encoding
- WineMetal sidecar loading
- M12 log routing

## SDK Bundle

```bash
tools/d3d12-metal-sdk/scripts/m12-dev.sh sdk-bundle
```

This builds and verifies `metalsharp-d3d12-developer-sdk.tar.zst`, including
scripts, contracts, probes, runtime manifest, and staged shader-corpus assets.

For release parity, the SDK should be rebuilt from the same verified split
bundles the app uses:

```bash
python3 tools/d3d12-metal-sdk/scripts/build-metal-shader-corpus.py --clean
python3 tools/bundles/create-developer-sdk.py \
  --bundle-dir app/bundles \
  --out-dir dist/developer-sdk \
  --manifest dist/bundles/metalsharp-bundle-manifest.tsv
tools/bundles/verify-developer-sdk.sh \
  dist/developer-sdk/metalsharp-d3d12-developer-sdk.tar.zst
```

When publishing a refreshed SDK to the `bundles` release, upload with
`--clobber`, download it back, and compare the SHA-256 against the verified
local archive.
