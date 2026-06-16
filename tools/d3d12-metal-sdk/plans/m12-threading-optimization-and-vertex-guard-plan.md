# M12 Threading/Optimization and Vertex Range Guard Plan

This plan starts after the clean M12 render PSO path became the default:

```text
DXMT_D3D12_TYPED_STAGE_IN_VERTEX_DESCRIPTOR=1
DXMT_D3D12_FORCE_DXIL_SOURCE_COMPILE=1
```

The render PSO workstream is considered functionally clean for bounded Elden Ring runs. This plan focuses on CPU/GPU threading, pipeline compile scheduling, shader-cache refresh cost, and any intermittent vertex range guard behavior.

## 1. Establish baseline matrix

Use the current default M12 path with no explicit descriptor/source-compile env overrides.

Run bounded launches with:

```text
workers=1
workers=2
workers=4
workers=6
```

Track:

```text
present_count
drawn_present_count
graphics_pso_compiled
compute_pso_compiled
render_pso_failed
vertex_descriptor_missing
vs_ps_varying_mismatch
unsafe_draw_skips
vertex_range_oob skips
CPU samples / process stats
```

Goal: find the best safe worker count after the render PSO fix. Prior baseline favored `workers=2`, but defaults changed, so remeasure.

## 2. Split performance buckets

### A. Pipeline compile parallelism

Knobs:

```text
METALSHARP_M12_PSO_WORKERS
METALSHARP_M12_ASYNC_PIPELINE_COMPILE
```

Questions:

- Does more than two workers now help because render PSO failures are gone?
- Does async compile reduce stutter or increase unsafe skips?
- Does source compilation default make shader compile cost too high?

Success:

```text
drawn presents stable
render_pso_failed=0
unsafe_draw_skips=0 or explained
lower launch/runtime stalls
```

### B. Shader cache freshness cost

Current default forces DXIL source compilation, which is correct but likely expensive.

Next improvement candidate:

- add a cache epoch/version salt for the M12 DXIL render-interface fix
- allow refreshed metallibs to persist safely
- stop recompiling forever once cache is refreshed

Target future behavior:

```text
force source compile only when cache epoch is stale
otherwise load refreshed metallib
```

### C. Draw/encode scheduling

Investigate CPU/GPU submit behavior after PSO success:

- command encoding stalls
- Metal queue depth
- async PSO readiness
- render encoder reopen/close frequency
- per-frame PSO compile bursts

## 3. Vertex range guard investigation

The default render PSO verification had one intermittent run with:

```text
unsafe_draw_skips=82
reason=vertex_range_oob
```

This must be classified rather than disabled blindly.

Add/extend log-only analysis for skipped draws. For each skip capture:

```text
pso hash/pointer
vs hash
ps hash
draw type
indexed vs non-indexed
start index
base vertex
index count
instance count
vertex buffer slot
buffer size
stride
computed required bytes
available bytes
input layout elements
stage_in / vertex-pulling mode
```

Then aggregate:

```text
top PSOs causing skips
top VS hashes
top vertex slots
negative base vertex?
start index outliers?
small buffer / huge draw mismatch?
only startup/loading?
only specific worker counts?
```

## 4. Offline/scratch analysis for guard cases

Create or extend a scratch-only script:

```text
tools/d3d12-metal-sdk/scripts/analyze-m12-vertex-range-skips.py
```

Inputs:

```text
launch log
scratch corpus
pso manifests if available
```

Outputs:

```text
guard bucket report
top offenders
whether skips look like true OOB or guard false positives
```

No live cache mutation.

## 5. Candidate fixes, one at a time

### If guard is false positive

Adjust range computation:

- account for index buffer max index instead of `start + count`
- handle `baseVertexLocation`
- handle compact vertex buffers / stripped draws
- account for per-instance slots separately

### If guard is true OOB but harmless

Make guard policy smarter:

- skip only affected slot
- clamp fetch to zero for OOB attrs
- allow known-safe pattern behind env gate

### If threading causes races

Keep workers low by default or serialize affected stage:

```text
workers=2 default
higher workers opt-in only
```

## 6. Test gates

For every optimization candidate:

1. restore stable cache snapshot
2. start backend with candidate env/default
3. run 45–60s bounded Elden Ring
4. compare to clean default baseline
5. only then test Subnautica 2
6. never mutate live cache with offline tools

Success target:

```text
render_pso_failed=0
vertex_descriptor_missing=0
vs_ps_varying_mismatch=0
unsafe_draw_skips=0
drawn_present_count == present_count
better CPU/GPU timing or fewer stalls
```

## Immediate next step

Run a fresh worker matrix on the new default:

```text
workers=1,2,4,6
```

Then parse any `vertex_range_oob` skips and decide whether the next change is threading, cache epoching, or guard math.

## Corrected worker matrix — 2026-06-15

Important correction: backend launch env such as `METALSHARP_M12_PSO_WORKERS` is read by the backend process, not by the `curl` client process. The corrected matrix restarted the backend with `METALSHARP_M12_PSO_WORKERS` for each run.

Artifacts:

```text
/tmp/metalsharp-m12-threading-matrix-corrected-20260615-204746/matrix-summary.md
/tmp/metalsharp-m12-threading-matrix-corrected-20260615-204746/matrix-summary.json
/tmp/metalsharp-m12-threading-matrix-corrected-20260615-204746/vertex-range-workers-1.md
/tmp/metalsharp-m12-threading-matrix-corrected-20260615-204746/vertex-range-workers-1.json
```

Corrected matrix:

| workers | drawn/present | gfx PSO | render failed | vertex missing | varying mismatch | unsafe skips | tess fallback | new render PSO |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 23/23 | 837 | 0 | 0 | 0 | 43 | 58 | 4 |
| 2 | 23/23 | 625 | 0 | 0 | 0 | 0 | 34 | 0 |
| 4 | 23/23 | 706 | 0 | 0 | 0 | 0 | 58 | 0 |
| 6 | 23/23 | 717 | 0 | 0 | 0 | 0 | 58 | 0 |

Findings:

- Render PSO creation remained clean for all worker counts.
- Workers `2`, `4`, and `6` were clean for unsafe draw skips.
- Workers `1` produced `43` vertex-range unsafe skips despite rendering.
- The guard bucket is narrow:
  - reason: `vertex_range_oob`
  - indexed: `1` for all rows
  - slot: `0` for all rows
  - stage_in: `0` for all rows
  - tess_fallback: `0` for all rows
  - no negative base vertex
  - `large_start_count=32/43`
- Top VS hashes in workers=1 guard bucket:
  - `35c8291d8f42aabd` × 14
  - `422831802103242e` × 6
  - `25714213ee8f7dfc` × 6

Initial decision:

- Do not optimize toward workers=1; it is slower/noisier and exposes the guard bucket.
- Keep/default workers at runtime default `4` for now unless a later timing sample proves `2` or `6` is better.
- Next test should compare async pipeline compile at workers `4` and possibly `6`.
- Vertex range guard analysis should focus on indexed vertex-pulling slot-0 draws with large start offsets.

## Async pipeline compile probe — 2026-06-15

Artifacts:

```text
/tmp/metalsharp-m12-async-matrix-20260615-205656/async-summary.md
/tmp/metalsharp-m12-async-matrix-20260615-205656/async-summary.json
/tmp/metalsharp-m12-async-matrix-20260615-205656/vertex-range-async1-workers-4.md
/tmp/metalsharp-m12-async-matrix-20260615-205656/vertex-range-async1-workers-4.json
```

Backend was restarted with:

```text
METALSHARP_M12_ASYNC_PIPELINE_COMPILE=1
```

Results:

| workers | async | drawn/present | gfx PSO | render failed | vertex missing | varying mismatch | unsafe skips | tess fallback |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 4 | 1 | 23/23 | 970 | 0 | 0 | 0 | 70 | 72 |
| 6 | 1 | 23/23 | 696 | 0 | 0 | 0 | 0 | 56 |

Findings:

- Async compile does not regress render PSO creation.
- Async workers=4 exposes the same vertex-range guard family:
  - reason: `vertex_range_oob`
  - indexed: `1` for all rows
  - slot: `0` for all rows
  - stage_in: `0` for all rows
  - tess_fallback: `0` for all rows
  - `large_start_count=51/70`
- Async workers=6 stayed clean in this bounded run.

Initial decision:

- Do not default async compile yet.
- If async compile is pursued, workers=6 is the only currently clean async point from this matrix.
- The guard bucket is consistent across workers=1 and async workers=4, so next investigation should target indexed vertex-pulling range math / resource state timing rather than render PSO creation.

## User performance correction and safe-draw direction — 2026-06-15

Manual timing observation supersedes interpreting lower worker counts as worse because of unsafe-skip counts alone:

- workers `1` is fastest
- workers `2` is second fastest
- workers `4` is slow
- workers `6` is very slow and can look clean only because it does not reach as far into the game
- game is slow while loading and gets much slower at character creation
- recommended scheduling direction is workers `4` with async compile, while fixing unsafe draws instead of skipping them

This strengthens the diagnosis that the next issue is resource timing / indexed vertex-pulling range math rather than render PSO creation.

Safe-draw policy candidate:

- keep all missing-resource and index-buffer guards intact
- for indexed vertex-pulling draws only, do not skip solely because the CPU-side vertex range estimate says OOB
- rely on shader-side `m12_load_vertex_attr` bounds checks to return zero for OOB vertex attributes
- expose rollback with:
  `DXMT_D3D12_VERTEX_RANGE_SAFE_DRAW=0` / `METALSHARP_M12_VERTEX_RANGE_SAFE_DRAW=0`

## Safe vertex-range draw candidate test — 2026-06-15

Commit:

```text
0d3e71b fix: allow safe M12 vertex range draws
```

Policy implemented:

- missing resources, unresolved resources, bad index buffers, and index-buffer OOB still skip
- indexed vertex-pulling draws no longer skip solely for CPU-side vertex range OOB
- shader-side `m12_load_vertex_attr` already bounds-checks each vertex attribute load and returns zero for OOB attributes
- rollback/debug override:
  `METALSHARP_M12_VERTEX_RANGE_SAFE_DRAW=0`

Test artifacts:

```text
/tmp/metalsharp-m12-safe-draw-test-20260615-210742/safe-draw-summary.json
tools/d3d12-metal-sdk/results/bounded-launches/elden-ring-20260615-210747/summary.md
tools/d3d12-metal-sdk/results/bounded-launches/elden-ring-20260615-210951/summary.md
```

Previously unsafe target configs now clean:

| case | workers | async | drawn/present | gfx PSO | render failed | vertex missing | varying mismatch | unsafe skips | tess fallback |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| workers1 | 1 | 0 | 23/23 | 1418 | 0 | 0 | 0 | 0 | 102 |
| workers4-async | 4 | 1 | 23/23 | 776 | 0 | 0 | 0 | 0 | 58 |

Interpretation:

- The vertex-range guard bucket was a false-positive/overconservative CPU guard for indexed vertex-pulling draws.
- Turning those into shader-clamped safe draws removes unsafe skips without regressing render PSO creation.
- Workers=4 + async is now a viable recommended scheduling direction for further loading/character-creation profiling.

## M12 worker/async default — 2026-06-15

After safe vertex-range draws were verified, workers `1` with async pipeline compile was tested and became the preferred useful default based on manual performance observation and clean bounded metrics.

Test evidence:

```text
tools/d3d12-metal-sdk/results/bounded-launches/elden-ring-20260615-211333/summary.md
present_count=24
drawn_present_count=24
graphics_pso_compiled=1328
render_pso_failed=0
vertex_descriptor_missing=0
vs_ps_varying_mismatch=0
dxil_msl_compile_failed=0
unsafe_draw_skips=0
```

M12 launcher defaults now include:

```text
DXMT_D3D12_PSO_WORKERS=1
DXMT_ASYNC_PIPELINE_COMPILE=1
DXMT_D3D12_TYPED_STAGE_IN_VERTEX_DESCRIPTOR=1
DXMT_D3D12_FORCE_DXIL_SOURCE_COMPILE=1
DXMT_D3D12_VERTEX_RANGE_SAFE_DRAW=1  # runtime default, disable with env override
```

Rollback/debug overrides remain:

```text
METALSHARP_M12_PSO_WORKERS=<n>
METALSHARP_M12_ASYNC_PIPELINE_COMPILE=0
METALSHARP_M12_TYPED_STAGE_IN_VERTEX_DESCRIPTOR=0
METALSHARP_M12_FORCE_DXIL_SOURCE_COMPILE=0
METALSHARP_M12_VERTEX_RANGE_SAFE_DRAW=0
```

## Default workers=1 async verification — 2026-06-15

Commit:

```text
f9b3c3d fix: default M12 to single async worker
```

Default verification was run with no explicit worker/async env on the backend. The bounded launcher summary shows empty overrides, so this exercised launcher defaults:

```text
tools/d3d12-metal-sdk/results/bounded-launches/elden-ring-20260615-211750/summary.md
workers_override=
async_compile_override=
present_count=23
drawn_present_count=23
graphics_pso_compiled=800
render_pso_failed=0
vertex_descriptor_missing=0
vs_ps_varying_mismatch=0
dxil_msl_compile_failed=0
unsafe_draw_skips=0
```

This locks in the useful default for future update work:

```text
DXMT_D3D12_PSO_WORKERS=1
DXMT_ASYNC_PIPELINE_COMPILE=1
```

with existing rollback overrides:

```text
METALSHARP_M12_PSO_WORKERS=<n>
METALSHARP_M12_ASYNC_PIPELINE_COMPILE=0
```

## Empty cache default M12 test — 2026-06-15

A fully empty live shader cache was tested with current M12 defaults. The pre-test cache was preserved at:

```text
/Volumes/AverySSD/MetalSharp-M12-Preserved/elden-ring-pre-empty-cache-test-20260615-212204
```

Test evidence:

```text
tools/d3d12-metal-sdk/results/bounded-launches/elden-ring-20260615-212209/summary.md
present_count=24
drawn_present_count=24
graphics_pso_compiled=2238
compute_pso_compiled=2
render_pso_failed=0
compute_pso_failed=0
sm50_compile_failed=0
dxil_msl_compile_failed=0
vertex_descriptor_missing=0
vs_ps_varying_mismatch=0
unsafe_draw_skips=0
```

Generated cache after the run contained approximately:

```text
file_count=9702
dxbc=2030
msl=2029
metallib=2
json=1582
dxil_report=2029
msl_errors=0
fail_markers=0
```

Interpretation:

- The preserved/stable cache is not required for correctness.
- M12 can generate from an empty cache with the new defaults and still render cleanly.
- First-run cost remains high because `DXMT_D3D12_FORCE_DXIL_SOURCE_COMPILE=1` compiles from source and does not currently persist refreshed DXIL metallibs for reuse.
- Future update work should add cache epoching/persistent refreshed metallibs so first-run generation becomes one-time rather than every run.

After the test, live cache was restored from the pre-test preservation snapshot.
