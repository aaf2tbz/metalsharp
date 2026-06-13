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

## SDK Bundle

```bash
tools/d3d12-metal-sdk/scripts/m12-dev.sh sdk-bundle
```

This builds and verifies `metalsharp-d3d12-developer-sdk.tar.zst`, including
scripts, contracts, probes, runtime manifest, and staged shader-corpus assets.
