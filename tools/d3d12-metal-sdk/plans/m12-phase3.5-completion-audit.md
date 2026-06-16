# M12 Phase 3.5 Completion Audit — Apple Metal Docs Gate

Date: 2026-06-16
Branch: `fix/m12-shader-probe-lab`

## Scope

Phase 3.5 integrated Apple Metal documentation guidance into the M12 diagnostic workflow without making further runtime changes. After an attempted broad diagnostic runtime rebuild broke Subnautica 2 adapter selection, runtime-side instrumentation is explicitly deferred until it can be isolated and bisected.

This audit closes the safe/offline portion of Phase 3.5 and defines the handoff requirements for Phase 4.

## Current known-good M12 runtime set

Use this full hash set for Elden Ring, Subnautica 2, and Armored Core VI validation until an intentional runtime update is proven:

```text
d3d12.dll      2612e228a5efa9d65f6923b3ed1cc50b1c6ce40abb2c0043c51d32ec5b60dd7c
dxgi.dll       dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24
dxgi_dxmt.dll  659ea3c4dddf658038eab67f26e71497ba11a4787e41c636766222ac2d8b028d
winemetal.dll  7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85
winemetal.so   167d16f1280ce4f78f842576758c46cdc6db59c37c2e20aa3b7060fba7f49d58
```

Full-runtime hash gates are available through:

```text
tools/d3d12-metal-sdk/scripts/m12-bounded-launch.sh
tools/d3d12-metal-sdk/scripts/m12-performance-run.sh
```

Required gate flags:

```text
--expect-d3d12-sha
--expect-dxgi-sha
--expect-dxgi-dxmt-sha
--expect-winemetal-dll-sha
--expect-winemetal-so-sha
```

## Phase 3.5 artifacts

Final refreshed reports:

```text
tools/d3d12-metal-sdk/results/phase3.5-apple-metal-docs/final-20260616-011003/metal-errors/metal-errors.md
tools/d3d12-metal-sdk/results/phase3.5-apple-metal-docs/final-20260616-011003/vertex-descriptors/vertex-descriptors.md
tools/d3d12-metal-sdk/results/phase3.5-apple-metal-docs/final-20260616-011003/cache-freshness/cache-freshness.md
tools/d3d12-metal-sdk/results/phase3.5-apple-metal-docs/final-20260616-011003/live-hang/live-hang-analysis.md
```

Tracked tools:

```text
tools/d3d12-metal-sdk/scripts/audit-m12-metal-errors.py
tools/d3d12-metal-sdk/scripts/audit-m12-vertex-descriptors.py
tools/d3d12-metal-sdk/scripts/verify-m12-cache-freshness.py
tools/d3d12-metal-sdk/scripts/analyze-m12-live-hang.py
```

Tracked plan:

```text
tools/d3d12-metal-sdk/plans/m12-apple-metal-docs-integration-plan.md
```

## Final report summary

### Metal error audit

```text
events=921
render_pso_failure=720
metal_library_compile_error=189
msl_ctz_ambiguous=12
```

Interpretation:

- AC6's current known MSL blocker is explicitly classified as `msl_ctz_ambiguous`.
- Subnautica 2 live cache still contains old `.msl.err.txt` artifacts; the cache freshness audit distinguishes stale vs active.
- Older log/corpus errors remain useful as offline regression cases but must not be confused with current smoke status.

### Vertex descriptor audit

```text
elden-ring-live:       render_psos=1216 missing_vertex_msl=23 ok_or_vertex_pulling=1193
subnautica-2-live:     render_psos=1    ok_or_vertex_pulling=1
armored-core-vi-live:  render_psos=483  missing_vertex_msl=10 ok_or_vertex_pulling=473
elden-ring-scratch:    render_psos=1172 missing_vertex_msl=22 ok_or_vertex_pulling=1150
```

Interpretation:

- `missing_vertex_msl` means a PSO manifest references a vertex shader whose `.msl` artifact is absent in that corpus input; it is not the same as runtime `vertex_descriptor_missing`.
- Current Elden Ring/AC6 render PSO corpus evidence remains mostly internally sane.
- Future runtime `vertex_descriptor_missing` or `vs_ps_varying_mismatch` should be correlated with this audit plus reflected Metal attributes and final `MTLVertexDescriptor` dumps.

### Cache freshness audit

```text
elden-ring-live:       shaders=1619 metallib_older_than_msl=235 dxbc_without_msl=2
subnautica-2-live:     shaders=776  has_msl_error=99 active_msl_error=21 stale_msl_error_older_than_msl=78
armored-core-vi-live:  shaders=556  has_msl_error=4 active_msl_error=4 dxbc_without_msl=1
elden-ring-scratch:    shaders=1611 dxbc_without_msl=2
```

Required cache key contract recorded from Apple `MTLBinaryArchive` / pipeline descriptor guidance:

```text
device_identity
os_version
metal_family_or_sdk
translator_commit_or_epoch
dxbc_sha256
generated_msl_sha256
entry_point
function_constants
render_or_compute_descriptor_state
vertex_descriptor_state
attachment_formats
sample_count
root_signature_hash
```

### Elden Ring live-hang audit

```text
categories=present_progress_observed, submission_without_completion_evidence
max_present_number=960
Apple command-buffer errors near capture=0
pipeline/translation errors near capture=0
```

Interpretation:

- The Elden Ring character-creation issue remains a live no-progress/hang, not a shader compile/PSO failure in current evidence.
- Existing capture lacks enough command-buffer completion/fence/encoder state to reduce it to a queue/wait state.

## Runtime diagnostic regression note

A broad Phase 3.5 runtime diagnostic rebuild was staged and then rolled back. The bad runtime set included:

```text
d3d12.dll      eac6959befe45ce1ce75e2fac9ca75527f432b0304ace296299c05f7f752c6cf
dxgi_dxmt.dll  7776ff3c98182558c1182a26465affe7fe8861f5513f690b2f8290a8ebca2431
winemetal.so   5ec36f518fd99b20f0590f13c5f8938a19019baf32448e7973143335647d1ec6
```

Observed regression:

```text
Subnautica 2: LogD3D12RHI: Error: Failed to choose a D3D12 Adapter
```

The failed run used `dxmt_m12`, but with the bad diagnostic rebuild. The failure occurred before command-buffer execution, so do not over-attribute it to a specific command-buffer trace line. Treat the broad relink/stage itself as unsafe until isolated.

Decision:

```text
No broad runtime diagnostic staging is part of Phase 3.5 completion.
Runtime instrumentation is deferred to tiny isolated patches with full-runtime hash gates and immediate rollback.
```

## Phase 3.5 completion status

Complete:

- Apple Metal docs integration plan exists.
- Metal error audit exists and preserves Apple error domain/code/text/userInfo where visible.
- Vertex descriptor audit exists for D3D12 input layout vs generated MSL/stage-in evidence.
- Cache freshness verifier exists and records Apple-doc-backed cache key requirements.
- Elden Ring live-hang analyzer exists and identifies missing command-buffer evidence.
- Full M12 runtime hash gates exist and prevent mixed-runtime validation runs.
- Phase 4 probe requirements are Apple-doc-backed and listed in the roadmap.

Deferred risks, not blockers for Phase 4 probe planning:

- Runtime does not yet emit all desired command-buffer encoder info / queue-list serials / fence states.
- Runtime does not yet dump final Metal-visible `MTLVertexDescriptor` in every failed/first-use PSO artifact.
- Runtime does not yet enforce the full binary-archive/cache key contract; current verifier is offline/reporting only.
- Visual correctness still requires Phase 5-style pixel/output probes; present/drawn counters are not sufficient.

## Phase 4 handoff requirements

Phase 4 should start with probes informed by this gate, not broad generic D3D12 coverage:

1. Command-buffer diagnostics probe with Apple domain/code/userInfo/encoder info.
2. Resource declaration/hazard probe covering indirect resources, heaps, aliasing, barriers, and `useResource(s)` / `useHeap(s)` behavior.
3. Heap/storage synchronization probe for upload/readback/fence/event ordering.
4. Vertex descriptor reconstruction probe with sparse attributes, multiple slots, per-instance step rates, offsets, and format variation.
5. Binary archive/cache freshness probe for descriptor-affecting state changes and translator-output changes.
6. Capture/validation record probe that explicitly records Metal API Validation/GPU capture/System Trace status.

## Stop rules retained

- Do not mutate live Elden Ring cache with offline tools.
- Do not treat present/drawn counts as visual correctness.
- Do not reopen DX11/PEAK/Schedule I as active focus.
- Do not stage broad runtime tracing builds during Phase 3.5/Phase 4 without isolated proof and full-runtime rollback coverage.
