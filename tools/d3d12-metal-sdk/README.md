# D3D12 Metal SDK

This directory is the repo-owned development SDK for MetalSharp's D3D12 to Metal work.

The SDK exists to make D3D12 changes evidence-driven before game-specific debugging starts. A D3D12 claim should be backed by at least one of:

- a contract entry in `contracts/`
- a probe under `probes/`
- a repeatable script under `scripts/`
- a baseline or generated result under `baselines/` or `results/`
- a documented unsupported or risky-stub entry

## Goals

- Prove the intended DXMT D3D12 runtime is loaded.
- Prove Agility SDK negotiation behaves as modern D3D12 games expect.
- Prove feature reports match implemented or explicitly emulated behavior.
- Prove resources, descriptors, shaders, queues, fences, and rendering paths through headless probes.
- Keep future D3D12 work accurate, repeatable, and reviewable.

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
