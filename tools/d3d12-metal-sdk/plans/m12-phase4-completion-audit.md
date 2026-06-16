# M12 Phase 4 Completion Audit — D3D12/DXGI Behavior Gauntlet

Date: 2026-06-16
Branch: `fix/m12-shader-probe-lab`

## Objective

Complete Phase 4 as a no-game, no-runtime-staging D3D12/DXGI runtime behavior gauntlet informed by Phase 3.5 Apple Metal diagnostics guidance.

Phase 4 completion does **not** claim visual correctness, game correctness, AC6 MSL `ctz` repair, or broad runtime instrumentation. Those remain later/parallel work.

## Completion verifier

Tracked verifier:

```text
tools/d3d12-metal-sdk/scripts/audit-m12-phase4-completion.py
```

Generated audit artifacts:

```text
tools/d3d12-metal-sdk/results/m12-phase4-completion/phase4-completion-audit.md
tools/d3d12-metal-sdk/results/m12-phase4-completion/phase4-completion-audit.json
```

Verifier result:

```text
ok=true
```

## Required proof artifacts

Core runtime behavior gauntlet:

```text
tools/d3d12-metal-sdk/results/m12-runtime-gauntlet/20260616-013615/runtime-gauntlet-summary.md
ok=true
probe_json_count=13
failure_count=0
```

PSO runtime behavior gauntlet:

```text
tools/d3d12-metal-sdk/results/m12-runtime-gauntlet/20260616-014142/runtime-gauntlet-summary.md
ok=true
probe_json_count=15
failure_count=0
```

Runtime hashes verified by the audit:

```text
d3d12.dll               2612e228a5efa9d65f6923b3ed1cc50b1c6ce40abb2c0043c51d32ec5b60dd7c
dxgi.dll                dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24
dxgi_dxmt.dll           659ea3c4dddf658038eab67f26e71497ba11a4787e41c636766222ac2d8b028d
winemetal.dll           7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85
dxmt_m12/winemetal.so   167d16f1280ce4f78f842576758c46cdc6db59c37c2e20aa3b7060fba7f49d58
wine/winemetal.so       167d16f1280ce4f78f842576758c46cdc6db59c37c2e20aa3b7060fba7f49d58
```

## Prompt-to-artifact checklist

| Requirement | Evidence | Status |
|---|---|---:|
| Phase 4 runs without launching games | `m12-runtime-gauntlet.sh`; summaries `20260616-013615`, `20260616-014142`; audit `capture-validation-record` | complete |
| Phase 4 does not stage runtime DLLs | audit `capture-validation-record`; full known-good hashes in both summaries | complete |
| Full runtime hash gate is used | audit `phase4-core-green`; hashes match known-good set | complete |
| Captures stdout/stderr/logs per probe | runtime gauntlet result dirs include `run-probes.stdout`, `run-probes.stderr`, per-probe JSON | complete |
| Classifies probe failures separately from game failures | `runtime-gauntlet-summary.json`, `probe-failures.md` | complete |
| Core D3D12/DXGI behavior passes | `20260616-013615/runtime-gauntlet-summary.md` | complete |
| PSO behavior passes | `20260616-014142/runtime-gauntlet-summary.md` | complete |
| Command-buffer diagnostics probe | `probe-command-replay-metalsharp.json`, `probe-queues-metalsharp.json`; audit `command-buffer-diagnostics` | complete |
| Resource declarations/hazards/indirect resources | `probe-descriptors`, `probe-barriers-render-pass`, `probe-command-replay`, `probe-compute-pso`; audit `resource-hazard-declarations` | complete |
| Heap/storage synchronization | `probe_heap_aliasing`, `probe-resources`, `probe-queues`; audit `heap-storage-synchronization` | complete |
| Vertex descriptor reconstruction | `probe_graphics_pso` sparse/explicit/high-slot/per-instance cases; audit `vertex-descriptor-reconstruction` | complete |
| Binary archive/cache freshness inputs | `probe_graphics_pso` cached-blob observation + descriptor-affecting PSO cases, `probe_compute_pso`, Phase 3.5 cache-key contract; audit `binary-cache-freshness` | complete |
| Capture/validation record | audit `capture-validation-record` explicitly states no-game, no runtime staging, validation/capture/System Trace status | complete |

## Phase 4 work completed

Added/updated:

```text
tools/d3d12-metal-sdk/scripts/m12-runtime-gauntlet.sh
tools/d3d12-metal-sdk/scripts/audit-m12-phase4-completion.py
tools/d3d12-metal-sdk/scripts/run-probes.sh
tools/d3d12-metal-sdk/scripts/build-probes.sh
tools/d3d12-metal-sdk/probes/probe_heap_aliasing/probe_heap_aliasing.cpp
tools/d3d12-metal-sdk/probes/probe_dxgi_factory/probe_dxgi_factory.cpp
tools/d3d12-metal-sdk/probes/probe_device_caps/probe_device_caps.cpp
tools/d3d12-metal-sdk/probes/probe_graphics_pso/probe_graphics_pso.cpp
tools/d3d12-metal-sdk/probes/probe_compute_pso/probe_compute_pso.cpp
```

Key corrections:

- DXGI probe now targets `dxgi_dxmt.dll` directly, avoiding a Wine/bootstrap DXGI false negative.
- Device-caps probe now separates hard minimums from observation-only capability policy.
- Graphics PSO probe covers sparse slots, explicit offsets, high input slots, packed formats, multiple buffers, and per-instance step rates.
- Compute PSO expected values now include the intentional sampled texture contribution.
- Heap aliasing probe covers placed resources at the same offset, explicit aliasing barrier, copy/readback, and fence completion.

## Residual risks outside Phase 4

- Visual/pixel correctness remains Phase 5.
- AC6 `ctz` MSL lowering remains translation repair work, not Phase 4 runtime behavior-gauntlet work.
- Elden Ring character-creation hang still needs live failing-scenario diagnostics; broad runtime instrumentation remains prohibited unless isolated/hash-gated.
- Runtime command-buffer NSError/userInfo logging is deferred until a failing command-buffer scenario exists and must be added as tiny isolated patches only.

## Conclusion

Phase 4 is complete as scoped: the D3D12/DXGI no-game runtime behavior gauntlet is implemented, hash-gated, audited, and green on the restored known-good M12 runtime.
