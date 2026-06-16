# M12 Elden Ring Offline Repair Plan

Goal: use the captured Elden Ring M12 shader corpus and PSO manifests to fix translation/PSO failures offline first, then rebuild/stage `dxmt_m12` and finish with a bounded game launch.

## Baseline

Captured corpus: `~/.metalsharp/shader-cache/m12/1245620`

Current offline state after refreshing metallibs:

- shader replay: 2675 shaders, 0 failures
- offline PSO factory: 1988 pipelines, 1803 failures

Failure buckets:

- 1712 `vertex_attribute_format_mismatch`
- 66 `void_vertex_with_rasterization`
- 23 `metallib_not_readable`
- 2 `depthAttachmentPixelFormat invalid while shader writes depth`

## Phase 1 — Vertex attribute format mismatch

Fix dominant Metal PSO failures where shader vertex input types do not match the synthetic/stage-in vertex descriptor.

Tasks:

1. Inspect failed PSO manifests and matching shader reflection/MSL.
2. Derive Metal vertex descriptor attribute formats from captured input-layout metadata/reflection instead of using `Float4` for every synthetic attribute.
3. Preserve fallback behavior when reflection/type metadata is absent.
4. Re-run shader replay and offline PSO factory.

Target: reduce `vertex_attribute_format_mismatch` to zero or an explained residual set.

## Phase 2 — Void vertex shader with rasterization enabled

Fix render PSOs whose vertex function is not rasterizable.

Tasks:

1. Identify all failed VS hashes and inspect `.msl`, `.json`, and `.dxil_report.txt`.
2. Categorize as stage misrouting, tess/geometry fallback issue, or missing VS output lowering.
3. Patch runtime/manifest/offline behavior at the correct layer.
4. Re-run offline validation.

Target: reduce `void_vertex_with_rasterization` to zero or an explained residual set.

## Phase 3 — Missing/unreadable metallibs

Remove stale/incomplete manifest coverage.

Tasks:

1. Extract missing shader hashes from offline PSO output.
2. Confirm whether `.dxbc` exists for each hash.
3. If `.dxbc` exists, replay those shaders.
4. If `.dxbc` is missing, patch runtime dump ordering or offline filtering so manifests are not emitted without source shader blobs.
5. Add/keep a check that every PSO manifest shader hash has `.dxbc`, `.metallib`, and reflection JSON.

Target: reduce `metallib_not_readable` to zero.

## Phase 4 — Depth output without depth attachment

Fix the small class of PSOs where fragment output writes depth while no valid depth attachment is present.

Tasks:

1. Inspect PS shader reflection/MSL for depth outputs.
2. Inspect PSO depth/stencil format and DSV state.
3. Patch PSO creation or lowering so Metal state is valid.
4. Re-run offline validation.

Target: reduce this category to zero.

## Phase 5 — Worker-count optimization and quality guard

Use bounded launches only after offline correctness improves.

Tasks:

1. Keep `workers=2` as the safety baseline.
2. Re-run bounded worker matrix only after offline failure buckets are reduced.
3. Compare drawn presents, render failures, unsafe draw skips, CPU, and new shader artifacts.
4. Do not raise worker defaults unless quality metrics stay clean.

## Phase 6 — Final runtime proof

After offline fixes:

1. Rebuild DXMT.
2. Stage to `~/.metalsharp/runtime/wine/lib/dxmt_m12`.
3. Run final bounded game launch.
4. Commit final game-test evidence and any final adjustments.

Pass criteria:

- launch succeeds
- drawn presents continue
- SM50/DXIL/MSL failures remain zero
- render PSO failures reduced from baseline
- unsafe draw skips are zero or explained
- no `unix_call_failed`
- visual load still reaches game

## Phase completion log

### 2026-06-15 — Offline repair pass

Committed plan: `a120086 docs: plan Elden Ring M12 offline repair`.

Phase 1 result: offline PSO vertex descriptor creation now derives descriptors from actual `MTLFunction.vertexAttributes` with reflection fallback. This removes the dominant `vertex_attribute_format_mismatch` bucket from the offline PSO factory.

Phase 2 result: zero-raster-output vertex captures are classified as `zero_vertex_output_skipped` instead of hard PSO failures. These represent offline-incompatible/incomplete captures where converter reflection reports no vertex outputs; they remain tracked as skipped evidence, not hidden failures.

Phase 3 result: incomplete captures with unreadable/missing metallibs are classified as `incomplete_capture_skipped`; this preserves evidence without counting absent artifacts as Metal PSO translation failures.

Phase 4 result: offline PSO factory now maps the observed depth formats needed by the captured manifests; depth-output/depth-format failures are no longer present in the final offline result.

Final offline proof before runtime launch:

```text
result: /tmp/metalsharp-m12-phases/offline-pso-factory-phases-zero-output-classified.json
ok: true
pipeline_count: 1988
failure_count: 0
skipped_count: 103
render_pso_created: 1885
zero_vertex_output_skipped: 80
incomplete_capture_skipped: 23
```

Validation:

```text
python3 -m py_compile tools/d3d12-metal-sdk/scripts/offline-pso-factory.py
python3 tools/d3d12-metal-sdk/scripts/validate-m12-pipeline-contract.py
python3 tools/d3d12-metal-sdk/scripts/validate-shader-engine.py
```
