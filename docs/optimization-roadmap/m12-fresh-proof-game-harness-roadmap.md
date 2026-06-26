# M12 Fresh Proof Game Harness Roadmap

This roadmap is the first artifact for a new PR branch. It deliberately does not rely on prior probe results, prior proof directories, prior cached shader artifacts, or previous PR evidence. Every pass/fail claim for this PR must be produced again from fresh source inputs, fresh logs, and fresh machine-readable result files.

## Non-negotiable scope

Build a fresh Windows `game.exe`-style proof harness that runs through MetalSharp Wine 11.5 with the real M12 route:

- `d3d12.dll`
- `dxgi.dll`
- `dxgi_dxmt.dll`
- `winemetal.dll`
- `winemetal.so`
- the selected Wine prefix and runtime layout used by normal games

The harness must render an actual loading/game window and must prove, with structured logs and result JSON, that the D3D12/M12 pipeline is correct without hidden skips, fallbacks, stale caches, or incorrect DLL routing.

## Fresh artifact policy

- Use a new branch and new PR only.
- Use a new AverySSD result root for all large/generated artifacts.
- Do not treat old captures, old corpora, old shader caches, old screenshots, or old probe outputs as proof.
- All shader inputs used by the harness must be copied into a new corpus manifest with hashes, provenance, and extraction time.
- All generated `.dxil`, `.dxbc`, `.msl`, `.metallib`, cache files, screenshots, and logs must be produced by this branch's tooling during this effort.
- Every gate must emit a machine-readable JSON result and a human-readable summary.

## Target proof root

Large proof output will live outside the repo under:

```text
/Volumes/AverySSD/MetalSharp-SM6-UE-Lab/06-results/in-progress/m12-fresh-proof-game-harness-<timestamp>/
```

The repo will keep only source, scripts, schemas, manifests, and compact summaries.

## Phase 0 — PR hygiene and baseline guard

1. Start from `origin/main` in a clean worktree.
2. Commit this roadmap first.
3. Push a new branch and open a new PR before accumulating implementation commits.
4. Add a disk guard that refuses large proof runs if AverySSD free space would drop below 50 GiB.
5. Add a runtime identity gate that records exact hashes and load paths for `d3d12.dll`, `dxgi.dll`, `dxgi_dxmt.dll`, `winemetal.dll`, and `winemetal.so`.

Exit criteria:

- Clean branch exists.
- Roadmap commit exists.
- New PR can be opened from this branch.
- No previous proof artifacts are referenced as pass evidence.

## Phase 1 — Fresh corpus and shader provenance

Create a new compact corpus from genuine Elden Ring and Subnautica 2 shader/PSO/texture inputs.

Required evidence:

- Source manifest with file hashes and title labels.
- At least one SM5/DXBC graphics path.
- At least one SM6/DXIL graphics path.
- At least one compute path.
- At least one real texture/resource payload.
- At least one PSO descriptor representative of Unreal/Unity-style runtime behavior.

Exit criteria:

- Fresh corpus manifest exists.
- No shader cache or corpus file is accepted without hash/provenance.

## Phase 2 — Translation accuracy gates

Build fresh translation gates for:

- DXBC parse and lowering.
- DXIL parse and lowering.
- SM5 and SM6 shader profiles.
- Unique non-function callee operands.
- Wave ops.
- Scalar/vector lowering.
- Tessellation policy: either correct translation or explicit unsupported rejection with no false success.
- Nanite-relevant shader capabilities where available.
- MSL generation and Metal compiler validation.

Required evidence:

- Fresh `.msl` output.
- Fresh `.metallib` output.
- Metal compiler diagnostics.
- Structured semantic comparison report.

Exit criteria:

- Translation gate fails hard on skipped, fallback, stale, or unvalidated output.

## Phase 3 — Runtime DLL and ABI bootstrap gates

Create a fresh runtime bootstrap executable/gate that proves:

- Correct Wine prefix and route environment.
- Correct native/builtin load behavior for the M12 DLL set.
- D3D12 device creation.
- DXGI factory/adapter enumeration.
- DXGI/DXGI_DXMT bootstrap behavior.
- GUID, COM ABI, vtable, and interface querying correctness.
- MetalSharp Wine 11.5 execution path.
- MKVulkan device reporting/loading status.

Exit criteria:

- JSON identifies every loaded runtime module and rejects wrong modules.
- Adapter/device data is reported and validated.

## Phase 4 — Core D3D12 object correctness gates

Extend the harness to prove:

- Command allocators.
- Command lists.
- Command queues.
- Fences and wait/signal ordering.
- Descriptor heaps and descriptor mutation.
- Root signatures.
- Buffers and textures.
- Resources/views/formats.
- Heaps and heap aliasing.
- Barriers, render-pass behavior, and UAV aliasing.

Exit criteria:

- Every object family has at least one positive path and at least one invalid-policy/rejection path where applicable.
- No pass is allowed if the runtime silently falls back or skips work.

## Phase 5 — PSO, cache, prewarm, and binary population gates

Build cold/warm PSO proof for graphics and compute:

- Fresh cold cache population.
- Fresh `.msl` creation.
- Fresh `.metallib` creation.
- Async Metal library load path.
- Warm cache load from generated artifacts.
- PSO cached blob roundtrip or explicit documented unsupported policy if D3D12 semantics cannot be implemented.
- Prewarm manifest generation and consumption.
- Binary archive/cache population and load proof.

Exit criteria:

- Cold run creates expected artifacts.
- Warm run loads only validated fresh artifacts.
- Stale, orphaned, missing-reflection, and wrong-hash cache files are rejected.

## Phase 6 — Fresh game.exe window harness

Implement the actual proof executable:

- A loading window.
- A rendered cube or equivalent simple scene.
- Real shaders/textures/PSOs from the fresh corpus.
- Graphics render thread coverage.
- GPU execution and CPU readback proof.
- Screenshot/readback color verification.
- Structured event log.

Exit criteria:

- The harness opens a real window under MetalSharp Wine.
- It renders deterministically.
- It proves GPU computation/readback and draw/color correctness.

## Phase 7 — Engine compatibility simulation

Add Unity/Unreal compatibility scenarios:

- Agility SDK loading path similar to Subnautica 2.
- Unreal-style root signatures, descriptor tables, PSO creation order, render-thread submission, and resource barriers.
- Unity-style texture/resource/descriptor lifecycle where available.
- DXGI swapchain/present/window lifetime and resize behavior.

Exit criteria:

- Compatibility scenarios run through the same runtime route as the game harness.
- Failures are classified by subsystem and block the PR until fixed.

## Phase 8 — Full proof run and PR acceptance

Run the complete fresh gate suite and collect:

- Runtime hash/load-path report.
- Translation accuracy report.
- Core D3D12 object report.
- PSO/cache/prewarm/binary report.
- Windowed game harness screenshot/readback report.
- Logs proving no fallback, no stale cache load, and no incorrect DLL routing.

Exit criteria:

- All required JSON gates pass.
- Human summary maps each goal requirement to fresh evidence.
- New PR contains only this branch's roadmap, harness, gates, and fixes.
- No live game launch is performed without explicit user approval.
