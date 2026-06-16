# M12 Elden Ring Slow Repair Plan

This plan supersedes the earlier fast repair attempt. The priority is to avoid regressions and keep the currently stable Elden Ring baseline intact.

## Current stable setup

Stable runtime + cache baseline preserved at:

```text
/Volumes/AverySSD/MetalSharp-M12-Preserved/elden-ring-stable-baseline-20260615-192313
```

Stable baseline required both:

1. Known-good `dxmt_m12` runtime.
2. Shader cache restored from the successful loaded snapshot.

Runtime hashes:

```text
d3d12.dll      92fba1da24895a9bb3c66c7f5a595001caf6f4375e6195966ccbdbabf3525a16
dxgi_dxmt.dll  793884a39b195c74121fe322be56617b100a952c1f1bb54d1aaa43d4c6fd31a2
winemetal.so   e82da21aca2a5b748cefdacc9794be9d96c1d028b90d4f94414c14214993daaa
```

Important discovery: running offline replay against the live game cache polluted the cache and caused the same runtime to stop rendering. Offline tools must never write into `~/.metalsharp/shader-cache/m12/1245620` again.

## Hard rules

1. Do not run `replay-shader-corpus.py --force` against the live game cache.
2. Do not run `offline-pso-factory.py` against a writable live cache if it depends on outputs produced by replay.
3. Before offline work, copy the stable corpus to scratch storage under `/Volumes/AverySSD` or `/tmp`.
4. Before runtime experiments, preserve both runtime and shader cache.
5. One runtime behavior change per commit.
6. Runtime changes must be env-gated until proven visually safe.
7. First runtime test after any rebuild is gate/default OFF; it must match stable baseline before gate ON is tried.
8. If default-OFF does not render, stop and bisect/rebuild baseline. Do not continue feature work.
9. The final commit in any repair sequence must be the successful bounded game test evidence.

## Working directories

Live cache, do not mutate for offline work:

```text
~/.metalsharp/shader-cache/m12/1245620
```

Scratch corpus root for offline work:

```text
/Volumes/AverySSD/MetalSharp-M12-CorpusLab/elden-ring-scratch
```

Stable rollback helper:

```bash
tools/d3d12-metal-sdk/scripts/restore-known-good-m12-runtime.sh
```

If cache rollback is needed, use the stable baseline snapshot:

```bash
rsync -a --delete \
  /Volumes/AverySSD/MetalSharp-M12-Preserved/elden-ring-stable-baseline-20260615-192313/shader-cache-m12-1245620/ \
  "$HOME/.metalsharp/shader-cache/m12/1245620/"
```

## Current observed runtime failures

Stable loaded run had:

```text
M12 present entry: 24
classification=drawn: 24
Graphics PSO compiled: 1610
Failed to create render PSO: 204
unsafe draw skips: 0
SM50Compile failed: 0
DXIL MSL compilation failed: 0
unix_call_failed: 0
```

Polluted-cache/no-render run had:

```text
M12 present entry: 22
classification=drawn: 0
Graphics PSO compiled: 180
Failed to create render PSO: 3804
Vertex function has input attributes but no vertex descriptor was set: 8698
mismatching vertex shader output: 102
unsafe draw skips: 256
DXIL MSL compilation failed: 4
```

Primary failure surfaces to investigate offline:

1. Vertex descriptor missing/type mismatch.
2. VS/PS varying mismatch.
3. Zero-output vertex captures.
4. Incomplete capture/missing metallib.
5. Cache poisoning/replay output incompatibility.

## SDK tool sequence

All commands below must use a scratch corpus, not the live cache.

### Phase 0 — Create scratch corpus

```bash
SCRATCH=/Volumes/AverySSD/MetalSharp-M12-CorpusLab/elden-ring-scratch/stable-$(date +%Y%m%d-%H%M%S)
mkdir -p "$SCRATCH"
rsync -a ~/.metalsharp/shader-cache/m12/1245620/ "$SCRATCH/"
```

### Phase 1 — Structural manifest audits

```bash
mkdir -p /tmp/metalsharp-m12-slow-repair

python3 tools/d3d12-metal-sdk/scripts/d3d12-graphics-pso-audit.py \
  --corpus "$SCRATCH" \
  --markdown /tmp/metalsharp-m12-slow-repair/d3d12-graphics-pso-audit.md \
  --json /tmp/metalsharp-m12-slow-repair/d3d12-graphics-pso-audit.json

python3 tools/d3d12-metal-sdk/scripts/d3d12-compute-pso-audit.py \
  --corpus "$SCRATCH" \
  --markdown /tmp/metalsharp-m12-slow-repair/d3d12-compute-pso-audit.md \
  --json /tmp/metalsharp-m12-slow-repair/d3d12-compute-pso-audit.json
```

Purpose: identify invalid manifests, stage-in/input-layout mismatches, geometry/tessellation PSOs, depth/stencil issues, and structural warnings before changing runtime.

### Phase 2 — Shader corpus replay in scratch only

```bash
python3 tools/d3d12-metal-sdk/scripts/replay-shader-corpus.py \
  --corpus "$SCRATCH" \
  --profile elden-ring-stable-scratch \
  --results-dir /tmp/metalsharp-m12-slow-repair \
  --force --allow-empty
```

Purpose: prove all captured DXBC blobs still convert without touching live cache.

### Phase 3 — Offline PSO factory + classification

```bash
python3 tools/d3d12-metal-sdk/scripts/offline-pso-factory.py \
  --corpus "$SCRATCH" \
  --profile elden-ring-stable-scratch \
  --results-dir /tmp/metalsharp-m12-slow-repair \
  --allow-empty

python3 tools/d3d12-metal-sdk/scripts/classify-offline-pso-failures.py \
  /tmp/metalsharp-m12-slow-repair/offline-pso-factory-elden-ring-stable-scratch.json \
  --corpus "$SCRATCH"
```

Purpose: classify failures into stable buckets without polluting runtime state.

### Phase 4 — Cache poisoning investigation

Compare live-stable cache vs scratch-replayed cache:

- Which files changed?
- Which `.metallib`/`.json` outputs differ from the stable cache?
- Does replay write converter outputs that runtime loads differently than captured runtime outputs?

No runtime fix should begin until this is understood.

### Phase 5 — Rebuild baseline before feature fixes

Before any feature change, prove that current source with no new behavior can rebuild to a rendering baseline. If a clean rebuild/default-off does not render, the task is source/binary baseline recovery, not descriptor fixing.

Required checks:

1. Restore stable runtime + stable cache.
2. Rebuild current source with no feature change.
3. Stage rebuilt runtime.
4. Launch default/off only.
5. If not rendering, bisect current source against the known-good binary/source state.

### Phase 6 — Only then fix one bucket

After rebuilt baseline renders, pick exactly one bucket:

1. Vertex descriptor missing/type mismatch.
2. VS/PS varying mismatch.
3. Zero-output/tessellation fallback routing.
4. Capture completeness.

Each bucket gets:

- offline scratch proof
- one env-gated runtime commit
- default-off launch
- gate-on launch
- preserve evidence

## Success criteria

No change is considered successful unless:

- live cache remains restorable and unpolluted
- stable baseline can still render
- default-off rebuilt runtime renders before gate-on testing
- bounded launch metrics do not regress drawn presents or unsafe draw skips
- the final commit contains the game-test evidence

## Phase 0/1/2 findings — 2026-06-15

Scratch corpus created from stable baseline:

```text
/Volumes/AverySSD/MetalSharp-M12-CorpusLab/elden-ring-scratch/stable-20260615-192733
```

Results:

```text
/tmp/metalsharp-m12-slow-repair/stable-20260615-192733
```

Scratch corpus counts:

```text
file_count=7975
dxbc=1584
metallib=238
pso_render=1172
```

Structural graphics PSO audit:

```text
render_pso_count=1172
violation_count=6
warning_count=1172
violations: invalid-active-color-format=6
warnings: input-layout-without-stage-in=1172
```

Compute PSO audit:

```text
compute_pso_count=0
violation_count=0
```

Scratch shader replay:

```text
shader_count=1584
failure_count=0
```

Tooling bug found and fixed: captured PSO manifests contain absolute shader paths back to the live cache. `offline-pso-factory.py` now remaps shader `.metallib`/reflection paths to the selected `--corpus` root by shader hash before running the Metal PSO harness. This prevents scratch validation from accidentally reading live-cache artifacts.

Scratch-isolated offline PSO result after remapping:

```text
pipeline_count=1172
failure_count=0
skipped_count=76
ok/render_pso_created=1096
zero_vertex_output=54
incomplete_capture=22
```

Interpretation:

- The stable corpus can replay in scratch without converter failures.
- Most captured render PSOs can be created offline when the scratch-replayed metallibs/reflections are used.
- The dominant structural warning remains `input-layout-without-stage-in` for all render manifests. This matches the current stable runtime behavior and must not be changed directly until a rebuilt baseline is proven.
- Remaining offline residuals are incomplete/offline-incompatible captures, not current hard PSO factory failures.

## Rebuilt source baseline proof — 2026-06-15

After restoring the stable cache, current source rebuilt and rendered. This proves the earlier no-render behavior was caused by live shader-cache pollution, not by current source alone.

Preserved rebuilt-source rendering baseline:

```text
/Volumes/AverySSD/MetalSharp-M12-Preserved/elden-ring-current-source-rendering-baseline-20260615-193352
```

Bounded launch evidence:

```text
tools/d3d12-metal-sdk/results/bounded-launches/elden-ring-20260615-193204/summary.md
```

Runtime hashes for rebuilt current source:

```text
d3d12.dll      d768b14ea63d741383ac695aad831727113b142009acac290a0a031378c8e090
dxgi_dxmt.dll  ff1e67e088c5dcf3ff7e82124caba95c0e562096c4c6b09bc280096b3da7b60d
winemetal.so   e82da21aca2a5b748cefdacc9794be9d96c1d028b90d4f94414c14214993daaa
```

Bounded metrics:

```text
present_count=22
drawn_present_count=22
graphics_pso_compiled=994
render_pso_failed=478
vertex_descriptor_missing=570
vs_ps_varying_mismatch=386
tessellation_fallback=115
unsafe_draw_skips=0
SM50Compile failed=0
DXIL MSL compilation failed=0
unix_call_failed=0
```

Conclusion: future runtime fixes can proceed from current source, but only if the stable cache is preserved/restored and offline work remains scratch-isolated.

## Vertex attribute inspection — 2026-06-15

Added scratch-only inspection of `MTLFunction.vertexAttributes` for captured render PSOs.

Result:

```text
/tmp/metalsharp-m12-slow-repair/stable-20260615-192733/metal-vertex-attributes-elden-ring-stable-scratch-v2.json
/tmp/metalsharp-m12-slow-repair/stable-20260615-192733/metal-vertex-attributes-elden-ring-stable-scratch-v2.md
```

Findings:

```text
pipelines=1172
has_metal_vertex_attributes=1150
metallib_not_readable=22
```

Metal vertex attributes are present for almost every readable vertex function. They are commonly indexed starting at 11 and include typed data such as Float/Float2/Float3/Float4 and UInt/UInt4. Example:

```text
render-000dffeebad6e96c
  vs=063ef13e839de754
  d3d12_inputs=11
  reflection_inputs=11
  attrs:
    position0[11] type=Float3
    position1[12] type=Float3
    normal0[13] type=UInt4
    normal1[14] type=UInt4
    tangent0[15] type=UInt4
    tangent1[16] type=UInt4
    binormal0[17] type=UInt2
    blendindices0[18] type=UInt4
    blendweight0[19] type=Float4
    color1[20] type=Float4
    texcoord0[21] type=Int4
```

Implication:

The runtime should not guess a synthetic all-Float4 vertex descriptor. A safe fix likely needs a Winemetal reflection API that exposes `MTLFunction.vertexAttributes` to the D3D12 pipeline builder, then an env-gated descriptor path that constructs a descriptor from actual Metal function attributes. This API can be added unused-by-default before any behavior change.

## Reflected vertex descriptor gated test — 2026-06-15

Implemented two commits:

```text
4772fbf feat: expose Metal vertex attribute reflection
1f2c8d1 fix: gate reflected M12 vertex descriptors
```

The first commit adds an inert Winemetal API for `MTLFunction.vertexAttributes`. The second uses it only when:

```text
DXMT_D3D12_TYPED_STAGE_IN_VERTEX_DESCRIPTOR=1
```

Default-off bounded launch after staging the candidate rendered:

```text
tools/d3d12-metal-sdk/results/bounded-launches/elden-ring-20260615-194241/summary.md
present_count=23
drawn_present_count=23
render_pso_failed=410
vertex_descriptor_missing=482
vs_ps_varying_mismatch=338
unsafe_draw_skips=0
```

True gate-on test required restarting the backend with:

```bash
METALSHARP_M12_TYPED_STAGE_IN_VERTEX_DESCRIPTOR=1
```

Gate-on bounded launch rendered but is not shippable:

```text
tools/d3d12-metal-sdk/results/bounded-launches/elden-ring-20260615-194702/summary.md
present_count=23
drawn_present_count=23
render_pso_failed=308
vertex_descriptor_missing=2
vs_ps_varying_mismatch=690
unsafe_draw_skips=79
```

Interpretation:

- The reflected vertex descriptor path addresses the missing vertex descriptor class.
- It exposes or worsens VS/PS varying mismatch failures and causes unsafe draw skips.
- Therefore the descriptor gate must remain opt-in and off by default.
- Next repair target should be VS/PS signature compatibility for the newly unblocked PSOs, not further descriptor guessing.

Environment reset after the gate-on test:

- Backend restarted without `METALSHARP_M12_TYPED_STAGE_IN_VERTEX_DESCRIPTOR`.
- Live shader cache restored from the pre-test snapshot:
  `/Volumes/AverySSD/MetalSharp-M12-Preserved/elden-ring-pre-reflected-descriptor-test-20260615-194234`.

## VS/PS varying mismatch diagnostics after reflected descriptors — 2026-06-15

Additional commits:

```text
9f05472 chore: log M12 render PSO failure keys
8b5ea20 test: analyze M12 varying mismatch failures
a949ed9 test: log reflected M12 descriptor details
```

A diagnostic gate was added:

```text
DXMT_D3D12_LOG_RENDER_PSO_FAILURE_KEYS=1
```

It is log-only/default-off. It records failed render PSO identity and render state without writing manifests or mutating cache.

Diagnostic launch evidence:

```text
tools/d3d12-metal-sdk/results/bounded-launches/elden-ring-20260615-195823/summary.md
tools/d3d12-metal-sdk/results/bounded-launches/elden-ring-20260615-200551/summary.md
tools/d3d12-metal-sdk/results/bounded-launches/elden-ring-20260615-200916/summary.md
```

The game continued to render during diagnostic gate-on launches. Environment was reset afterward and live cache restored from:

```text
/Volumes/AverySSD/MetalSharp-M12-Preserved/elden-ring-pre-reflected-descriptor-test-20260615-194234
```

Read-only analysis artifacts:

```text
/tmp/metalsharp-m12-varying-analysis/failure-keys-195823.log
/tmp/metalsharp-m12-varying-analysis/varying-failure-analysis.json
/tmp/metalsharp-m12-varying-analysis/varying-failure-analysis.md
/tmp/metalsharp-m12-varying-analysis/failure-keys-200551.log
/tmp/metalsharp-m12-varying-analysis/failure-detail-200916.log
```

Key findings:

- Failure-key run parsed `326` render PSO failures.
- All parsed failures had `uses_stage_in=0`.
- Split by descriptor path:
  - `reflected_descriptor=0`: `176`
  - `reflected_descriptor=1`: `150`
- Reflected-descriptor mismatch dominant bucket:
  - `locn0` requested by fragment but missing from VS reflection: `104/150`
- Non-reflected mismatch dominant bucket remains semantic-style names:
  - `texcoord*`, `normal0`, `position1`, `tangent*`, `color0`.
- Reflected descriptor details show the bridge is not garbage. Representative failures use:
  - `attrs=13 layouts=1 stride0=24`
  - populated attrs commonly `attr=11 fmt=Float4 offset=0`, `attr=12 fmt=Float2 offset=16`

Exact VS/PS failure pairs were replayed offline from scratch with matching RT/depth state using generated manifests under `/tmp/metalsharp-m12-varying-analysis`. The offline factory still created the PSOs:

```text
offline-pso-factory-exact-failure-pairs.json
pipeline_count=188
failure_count=0
skipped_count=14
render_pso_created=174
zero_vertex_output_skipped=14
```

Interpretation:

- The reflected descriptor path is useful: it nearly eliminates the missing-descriptor bucket.
- The new blocker is VS/PS interface compatibility once those PSOs get past descriptor validation.
- Runtime bridge descriptor data appears plausible, so do not churn descriptor ABI first.
- Next behavior candidate should target generated vertex output/user-varying emission or cache freshness for broad `output_v` / `input_v` contracts.
- Keep the descriptor gate off by default until the VS/PS mismatch bucket is reduced and unsafe draw skips return to zero.
