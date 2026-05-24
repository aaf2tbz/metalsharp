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

Wine builtin DLLs commonly report as `C:\windows\system32\*.dll` from inside the probe even when they are backed by `WINEDLLPATH` or builtin replacement files. For D3D12, the loader probe therefore also checks ordinal `101` for `D3D12CreateDevice`, which is the important custom-runtime compatibility signal for games that import D3D12 by ordinal.

`build-probes.sh` copies the Agility SDK 1.619.3 DLLs into `out/bin/D3D12/` before building `probe_agility_ue5.exe`. Override `AGILITY_BIN` when testing a different extracted SDK:

```bash
AGILITY_BIN=/path/to/agility/build/native/bin/x64 \
  tools/d3d12-metal-sdk/scripts/build-probes.sh
```

The Agility probe exports `D3D12SDKVersion=619` and `D3D12SDKPath=".\\D3D12\\"`, then records app-local Agility DLL discovery, D3D12 device creation, and modern `ID3D12Device*` QueryInterface behavior as JSON.

The device capability probe uses the same Agility export pattern and records UE5-relevant `CheckFeatureSupport` results: feature levels, shader model, resource binding tier, wave ops, atomic64, raytracing, mesh shader, sampler feedback, and other advanced feature gates.

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
