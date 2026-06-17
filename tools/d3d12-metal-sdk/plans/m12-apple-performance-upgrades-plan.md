# M12 Apple performance upgrades plan

Source-backed from `tools/d3d12-metal-sdk/results/apple-performance-docs-research-*/summary.md`.

## Policy

- Keep M12 async pipeline compilation enabled.
- Treat high worker fanout as harmful for FromSoftware-style workloads until disproven.
- Keep GPTK/D3DMetal and MoltenVK flags opt-in/profile-only.
- Prefer counters and GPU traces over speculative tuning.

## Implementation order

1. [done] Add allowlisted GPTK/D3DMetal and MoltenVK env override plumbing.
2. [done] Add M12 PSO cache/reuse/churn counters.
3. [done] Extend bounded-run summaries with PSO/cache/replay/bind counters.
4. Build M12-native prewarm manifests from D3DMetal oracle linkage.
5. Add redundant resource binding, broad texture usage, and load/store diagnostics.
6. Patch only the hot paths proven by counters/traces.
7. Validate AC6, Elden Ring, and Subnautica 2 with strict bounded launches and hash gates.

## Focused m-sync / MetalFX addendum

Source-backed artifact: `tools/d3d12-metal-sdk/results/apple-msync-metalfx-research-20260616-224653/summary.md`.

- Preserve current `WINEMSYNC=1` default, but add launch/runtime wait observability before changing sync policy.
- Add a bounded opt-out experiment path only after wait counters exist.
- Add GPTK/D3DMetal MetalFX as opt-in only: `METALSHARP_GPTK_METALFX=1 -> D3DM_ENABLE_METALFX=1`.
- Keep MetalFX disabled for M12 correctness validation because it can mask base-render failures.
- Treat M12/D3D12 MetalFX spatial upscaling as a fallback-safe presenter feature behind an explicit profile flag.
- Do not attempt generic M12 temporal MetalFX unless trustworthy color/depth/motion/jitter/reset/exposure inputs are available.

## Phase A completion note

Artifact: `tools/d3d12-metal-sdk/results/perf-env-overrides-phase-a-20260616-230637/summary.md`.

Implemented backend-scoped, opt-in env forwarding for GPTK/D3DMetal `D3DM_*`/`MTL_*` knobs and DXVK/MoltenVK `MVK_CONFIG_*` knobs. Launch logs record `sync.WINEMSYNC=1` and active backend override envs.

## Phase B completion note

Artifact: `tools/d3d12-metal-sdk/results/m12-pso-pressure-instrumentation-20260616-231318/summary.md`.

Implemented M12 `PSO_PRESSURE` runtime counters for PSO requests, unique/repeated descriptor hashes, shader/metallib cache hits/misses, Metal pipeline creation calls, and compile wait time. `analyze-m12-perf-run.py` now folds those counters into `perf-analysis.json` and `perf-analysis.md`.

## Phase C cache-pressure note

Artifact: `tools/d3d12-metal-sdk/results/m12-dxil-function-pipeline-cache-20260616-233802/summary.md`.

DXIL-backed shaders now reuse the in-process function cache even when the D3D12 path requested SM50 reflection outputs, avoiding repeated DXIL-to-MSL/source-library work for duplicate shaders in the same process. A conservative Metal pipeline object cache was also added. AC6 validation remained correct (`20/20` drawn/present, zero failures), but counters still show many unique PSO descriptors, so the next optimization step should be oracle/prewarm guided rather than higher worker fanout.
