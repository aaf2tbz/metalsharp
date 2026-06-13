# D3D12 Developer Runtime Package

`metalsharp-d3d12-developer-sdk.tar.zst` is the portable developer package for
D3D12 to Metal runtime work. It keeps contracts, probes, scripts, docs, and a
ready staged Wine/DXMT runtime in one archive.

## Layout

```text
developer-sdk/d3d12/
  README.md
  contracts/
  docs/
  probes/
  scripts/
  runtime/
    wine/
    dxmt/
      x86_64-windows/
      x86_64-unix/
    host/
    metalsharp-backend
    manifest.json
```

The runtime files are generated from release bundle inputs. They are not stored
directly in git.

## Platform Posture

- macOS: the package includes the MetalSharp Wine runtime, DXMT DLLs, and
  Winemetal bridge needed for Apple Silicon D3D12 probe work.
- Linux: contracts, probes, and scripts are portable. Use a local Wine runtime
  with `--wine`, `--prefix`, and `--dxmt-runtime` when investigating non-Metal
  loader behavior.
- Windows: probe sources, contracts, and PowerShell environment helpers are
  included. Native Windows does not use Wine, but the sources are useful for
  compiling reference probes and comparing export contracts.

## Quick Start

```bash
tar --use-compress-program=unzstd -xf metalsharp-d3d12-developer-sdk.tar.zst
cd developer-sdk/d3d12
source scripts/sdk-env.sh
scripts/preflight-runtime-layout.py --dxmt-runtime "$METALSHARP_DXMT_RUNTIME"
scripts/run-probes.sh --wine "$WINE" --prefix "$WINEPREFIX" --dxmt-runtime "$METALSHARP_DXMT_RUNTIME" --mini-only
```

For repository work, prefer the developer wrapper:

```bash
tools/d3d12-metal-sdk/scripts/m12-dev.sh mini
tools/d3d12-metal-sdk/scripts/m12-dev.sh shader-lab -- --name latest-auto-discovery
tools/d3d12-metal-sdk/scripts/m12-dev.sh full-offline
```

The wrapper is the stable front door for build, stage, run, offline shader-lab,
and SDK packaging tasks. The lower-level scripts remain available for focused
debugging.

In a full MetalSharp repository checkout, use the M12 cube pipeline check before
launching Steam or a game:

```bash
tools/ci/m12-check.sh
```

That check rebuilds the current DXMT D3D12 artifacts, stages the same
WineMetal layout used by the MetalSharp M12 route, builds `m12_game.exe`, and
verifies the staged runtime contract. Pull requests expose this gate as
`M12 Check`. For a local live render pass, run:

```bash
M12_CHECK_RUN_LIVE=1 tools/ci/m12-check.sh
```

The live executable is a 10-second RGB cube scene with depth, lighting, shadows,
indexed draws, and readback validation.

## Offline Game Shader Validation

Captured game shader caches can be staged into an offline Metal validation lab
without launching the game again. Keep these runs on external storage:

```bash
tools/d3d12-metal-sdk/scripts/stage-game-metal-validation.py \
  --corpus "$HOME/.metalsharp/tmp/subnautica2_m12_offline/sm6-20260612-225622/shader-cache" \
  --stage-root /Volumes/AverySSD/MetalSharp-M12-CorpusLab \
  --name subnautica2-sm6
```

The tool regenerates MSL through DXMT's native DXIL converter when available,
compiles the MSL into AIR and metallib with Apple's Metal toolchain, then builds
and runs a tiny Objective-C++ Metal framework harness. The native harness loads
each metallib, resolves exported functions, and creates compute pipeline states
for kernel functions. Results are written to `manifest.json` and `summary.json`
inside the staged run directory.

This is a shader/library validation surface, not a full game replay. It gives a
hard pass/fail matrix for DXIL lowering, Metal compilation, metallib linking,
Metal framework library loading, and compute pipeline creation. Graphics PSO
pairing from captured PSO JSON is the next layer above this gate.

PowerShell users can initialize equivalent environment variables with:

```powershell
.\scripts\sdk-env.ps1
```

## Rebuild And Publish

The CI release flow rebuilds this package from the current repository SDK
source plus the released runtime and graphics split bundles:

```bash
tools/dmg/create-bundles.sh
tools/bundles/create-developer-sdk.py \
  --bundle-dir app/bundles \
  --out-dir dist/developer-sdk \
  --manifest dist/bundles/metalsharp-bundle-manifest.tsv
tools/bundles/verify-developer-sdk.sh \
  dist/developer-sdk/metalsharp-d3d12-developer-sdk.tar.zst
```

On `main` and version tags, CI uploads the refreshed SDK tarball and updated
bundle manifest back to the `bundles` release with `--clobber`. The DMG build
then downloads that refreshed SDK instead of repackaging a stale tarball.
