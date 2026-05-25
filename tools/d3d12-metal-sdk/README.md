# D3D12 Metal SDK

This directory is the repo-owned development SDK for D3D12 to Metal work through Wine-compatible runtimes.

MetalSharp is one host profile for this SDK. The probes are normal Windows executables that should also run under standalone Wine prefixes, DXMT development prefixes, and future host integrations.

The SDK exists to make D3D12 changes evidence-driven before game-specific debugging starts. A D3D12 claim should be backed by at least one of:

- a contract entry in `contracts/`
- a probe under `probes/`
- a repeatable script under `scripts/`
- a baseline or generated result under `baselines/` or `results/`
- a documented unsupported or risky-stub entry

## Goals

- Prove the intended DXMT D3D12 runtime is loaded.
- Keep the core probes Wine-compatible and host-agnostic.
- Prove Agility SDK negotiation behaves as modern D3D12 games expect.
- Prove feature reports match implemented or explicitly emulated behavior.
- Prove resources, descriptors, shaders, queues, fences, and rendering paths through headless probes.
- Keep future D3D12 work accurate, repeatable, and reviewable.

## Runtime Profiles

Run the SDK against the local MetalSharp runtime:

```bash
tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp
```

For fast one-behavior-at-a-time D3D12 validation without launching Steam or a
game, run the headless mini-app suite:

```bash
tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp --mini-only
```

This builds normal Windows EXEs and runs each under Wine with the DXMT M12
runtime. Each result is written to `results/probe-mini-*-metalsharp.json`.
The current mini suite isolates:

- `create_device`
- `command_queue`
- `swapchain_present`
- `rtv_clear`
- `compute_dispatch`
- `root_signature`
- `descriptors`
- `graphics_pso`
- `geometry_shader_pso`
- `mesh_object_shader_pso`
- `texture_sample`

`mesh_object_shader_pso` is intentionally a tracked gap until the SDK has a
real mesh/object pipeline-state-stream probe. Keeping it as a runnable mini-app
prevents future UE5/Nanite work from being described as supported without a
repeatable proof.

Before launching Steam or a game, run the game-safe preflight:

```bash
tools/d3d12-metal-sdk/scripts/preflight-before-game.sh \
  --profile subnautica2 \
  --game-dir "/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2/Subnautica2/Binaries/Win64"
```

This command does not launch Steam or the game. It validates the Winemetal route
layout, runs the existing D3D12 Wine probes, and replays any dumped `.dxbc`
shader corpus through MetalShaderConverter. If no shader corpus exists yet, it
fails with a clear capture-needed message instead of using the game as the
debugger.

For layout-only checks while iterating on DLL staging:

```bash
python3 tools/d3d12-metal-sdk/scripts/preflight-runtime-layout.py \
  --profile subnautica2 \
  --game-dir "/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2/Subnautica2/Binaries/Win64"
```

For offline shader replay against a known corpus:

```bash
python3 tools/d3d12-metal-sdk/scripts/replay-shader-corpus.py \
  --profile subnautica2 \
  --corpus "/path/to/shader-cache/m12/1962700"
```

For a strict Winemetal ABI/export gate before any Steam or game launch:

```bash
python3 tools/d3d12-metal-sdk/scripts/check-winemetal-abi.py \
  --profile subnautica2 \
  --game-dir "/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2/Subnautica2/Binaries/Win64"
```

This verifies that Steam/global Wine copies keep wrapper exports such as
`WMTSetMetalShaderCachePath`, while DXMT/game-local copies also expose shader
and PSO bridge exports such as `MTLLibrary_newFunctionWithDescriptor`.

For offline Metal PSO factory checks against converted shaders:

```bash
python3 tools/d3d12-metal-sdk/scripts/offline-pso-factory.py \
  --profile subnautica2 \
  --corpus "/path/to/shader-cache/m12/1962700"
```

Without a manifest, this loads converted `.metallib` files, verifies function
lookup, and creates compute PSOs for compute shaders. With a captured manifest,
it can create render PSOs too:

```json
{
  "schema": "metalsharp.d3d12-metal.offline-pso-manifest.v1",
  "pipelines": [
    {
      "name": "captured-render-pso",
      "type": "render",
      "vertex": {"metallib": "/path/vs.metallib", "function": "Main"},
      "fragment": {"metallib": "/path/ps.metallib", "function": "Main"},
      "color_formats": ["bgra8unorm"],
      "depth_format": "depth32float",
      "sample_count": 1
    }
  ]
}
```

Run that manifest with:

```bash
python3 tools/d3d12-metal-sdk/scripts/offline-pso-factory.py \
  --profile subnautica2 \
  --manifest /path/to/pso-manifest.json
```

Failures are captured in `results/offline-pso-factory-*.json` with the exact
Metal error string from `newRenderPipelineStateWithDescriptor` or
`newComputePipelineStateWithFunction`.

If no corpus exists yet, use the bounded capture runner instead of a blind
interactive launch:

```bash
tools/d3d12-metal-sdk/scripts/capture-game-shader-corpus.sh \
  --profile subnautica2 \
  --seconds 20
```

The capture runner preflights the runtime layout, launches through the backend
for a fixed short window, kills the target, and writes
`results/shader-corpus-capture-subnautica2.json` with the newly captured `.dxbc`
files. Re-run `preflight-before-game.sh` afterward without
`--allow-empty-corpus` to prove MetalShaderConverter can replay the captured
corpus.

Run the SDK against an arbitrary Wine/DXMT runtime:

```bash
tools/d3d12-metal-sdk/scripts/run-probes.sh \
  --wine /path/to/wine \
  --prefix "$HOME/wine-d3d12-test" \
  --dxmt-runtime /path/to/dxmt-runtime
```

The `--dxmt-runtime` directory should contain:

```text
x86_64-windows/
  d3d12.dll
  dxgi.dll
  d3d11.dll
  d3d10core.dll
  winemetal.dll
x86_64-unix/
  winemetal.so
```

The runtime preflight intentionally treats Winemetal as two routes:

- Steam/global Wine copies must preserve legacy wrapper exports such as
  `WMTSetMetalShaderCachePath`.
- DXMT/game-local copies must preserve those legacy exports and expose any new
  shader bridge exports such as `MTLLibrary_newFunctionWithDescriptor`.

Do not copy the smaller DXMT `winemetal.dll` into `system32`, `syswow64`, or
`runtime/wine/lib/wine`. The preflight is designed to catch that class of
regression before Steam is launched.

Wine builtin DLLs commonly report as `C:\windows\system32\*.dll` from inside the probe even when they are backed by `WINEDLLPATH` or builtin replacement files. For D3D12, the loader probe therefore also checks ordinal `101` for `D3D12CreateDevice`, which is the important custom-runtime compatibility signal for games that import D3D12 by ordinal.

`build-probes.sh` copies the Agility SDK 1.619.3 DLLs into `out/bin/D3D12/` before building `probe_agility_ue5.exe`. Override `AGILITY_BIN` when testing a different extracted SDK:

```bash
AGILITY_BIN=/path/to/agility/build/native/bin/x64 \
  tools/d3d12-metal-sdk/scripts/build-probes.sh
```

The Agility probe exports `D3D12SDKVersion=619` and `D3D12SDKPath=".\\D3D12\\"`, then records app-local Agility DLL discovery, D3D12 device creation, and modern `ID3D12Device*` QueryInterface behavior as JSON.

The device capability probe uses the same Agility export pattern and records UE5-relevant `CheckFeatureSupport` results: feature levels, shader model, resource binding tier, wave ops, atomic64, raytracing, mesh shader, sampler feedback, and other advanced feature gates.

The DXGI factory probe records factory creation, `IDXGIFactory*` QueryInterface behavior through `IDXGIFactory7`, adapter enumeration, GPU-preference enumeration when available, LUID lookup, output enumeration, and stable adapter description fields.

## Contract Commands

Generate the first-class contract files from the current external source maps:

```bash
python3 tools/d3d12-metal-sdk/scripts/generate-contracts.py
```

Validate all required contract files:

```bash
python3 tools/d3d12-metal-sdk/scripts/validate-contracts.py
```

Phase 1 imports:

- `contracts/d3d12-metal-contract.json` from `/Volumes/AverySSD/metalsharp/metal-api-table/final/d3d12_to_metal_map.json`
- `contracts/agility-1.619.3-contract.json` from `/Volumes/AverySSD/metalsharp/metal-api-table/final/agility_sdk_d3d12_to_metal_map.json`
- `contracts/feature-support-contract.json`
- `contracts/dxgi-contract.json`
- `contracts/unsupported-api-ledger.json`
- `contracts/risky-stub-ledger.json`

## Phase Discipline

Each phase should:

1. Update the SDK source or contracts.
2. Update the Obsidian roadmap.
3. Commit to the draft PR branch.
4. Update the PR summary.
5. Run a hardening pass before starting the next phase.
