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
