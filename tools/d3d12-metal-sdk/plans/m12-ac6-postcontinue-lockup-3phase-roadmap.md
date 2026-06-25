# M12 AC6 Post-Continue GPU/MMU/Firmware Lockup — 3-Phase Recovery Roadmap

Status: new forward roadmap. This supersedes title/menu-oriented framing. It does **not** authorize staging or launching AC6.

Current stable baseline:

```text
a909c83 fix(m12): gate render attachment resource use
```

Known AC6 behavior:

- Game launches.
- Title loads.
- Menu loads.
- Loading/title/menu path is not the problem.
- The dangerous failure is only after selecting **Continue** and entering real-world gameplay/loading/rendering/computation.
- The observed failure class was a solid magenta window during the gameplay loading transition followed by GPU page fault / MMU / firmware lockup behavior severe enough to crash the machine.

Core rule for this roadmap:

> Do not use another AC6 Continue crash as the diagnostic mechanism. Implement real correctness fixes, prove them with offline probes and sensitivity tests, then stage only after the evidence bundle is green and the user explicitly approves one controlled retest.

---

## Evidence base

Research artifacts:

```text
roadmap-research/apple-metal-gpu-faults.md
roadmap-research/directx12-resource-hazard-docs.md
roadmap-research/github-ecosystem-gpu-hang-patterns.md
roadmap-research/d3d12core-translation-architecture.md
roadmap-research/offline-probe-validation-strategy.md
```

Local code anchors inspected:

```text
vendor/dxmt/src/d3d12/d3d12_command_buffer_completion.hpp
vendor/dxmt/src/d3d12/d3d12_command_defs.hpp
vendor/dxmt/src/d3d12/d3d12_command_list.cpp
vendor/dxmt/src/d3d12/d3d12_command_queue.cpp
vendor/dxmt/src/d3d12/d3d12_descriptor_heap.hpp
vendor/dxmt/src/d3d12/d3d12_swapchain.cpp
vendor/dxmt/src/winemetal/Metal.hpp
vendor/dxmt/src/winemetal/winemetal.h
vendor/dxmt/src/winemetal/unix/winemetal_unix.c
vendor/dxmt/src/winemetal/winemetal_thunks.c
```

Primary API facts verified locally / directly:

1. Apple Metal headers say `useResource` / `useResources` for render encoders must be called before draw commands that may access argument-buffer resources, and explicitly state that these calls **do not retain** resources. Retention is the caller's responsibility until command execution completes.
2. Apple Metal command-buffer headers expose `MTLCommandBufferErrorPageFault`, `MTLCommandBufferErrorInvalidResource`, `MTLCommandBufferErrorTimeout`, `MTLCommandBufferDescriptor.errorOptions`, and `MTLCommandBufferErrorOptionEncoderExecutionStatus`.
3. Microsoft D3D12 docs confirm resource state management is application/runtime-visible through `ResourceBarrier`, indirect drawing can use GPU-generated command/count buffers, and shader-visible descriptor heaps are referenced by shaders through descriptor tables.
4. Ecosystem patterns from vkd3d-proton / DXVK / MoltenVK converge on the same fix family: per-submission lifetime tracking, descriptor snapshot/ref tracking, explicit barriers/fences, deferred release on GPU completion, and robust present/drawable ordering.

---

## What the current `a909c83` baseline really has

Good and keep:

- RTV/DSV/UAV record-time descriptor snapshots exist for:
  - `OMSetRenderTargets`
  - `ClearRenderTargetView`
  - `ClearDepthStencilView`
  - `ClearUnorderedAccessView*`
- Stage 3C adds default-off render attachment `useResource` declarations:
  - `DXMT_D3D12_RETAIN_RENDER_ATTACHMENTS`
  - `UseRenderAttachmentResource`
  - `UseActiveRenderAttachmentResources`
- Completion-slot backpressure exists for Metal command buffers.

Important gaps:

1. `D3D12MetalCommandBufferCompletionSlot` currently retains only a `WMT::CommandBuffer`.
   It does **not** retain D3D12 resources, WMT buffers, WMT textures, drawables, argument buffers, or transient table slabs until Metal completion.
2. `LogReplayRetentionCandidate(...)` is diagnostic naming, not retention.
3. `useResource` is present in many paths, but it is not a lifetime model.
4. Graphics descriptor-table resources are still resolved from live descriptor heaps in multiple replay paths; RTV/DSV/UAV snapshots do not cover all graphics CBV/SRV/UAV descriptor-table cases.
5. `ExecuteIndirect` currently CPU-maps the argument/count buffers in `d3d12_command_queue.cpp`. D3D12 allows those buffers to be GPU-generated; AC6 world rendering is exactly where GPU-driven indirect args/counts become likely.
6. `ResourceBarrier` replay currently logs/transitions resource-wide state and closes encoders, but there is no complete per-subresource hazard model and no fully-proven UAV/aliasing/cross-queue synchronization model.
7. Present still has a live-present CPU render-wait skip path. That is unsafe for AC6 Continue until producer→present ordering is proven by probes.
8. winemetal exposes command-buffer `status`, `error`, and `logs`, but not a command-buffer descriptor path with `errorOptions = EncoderExecutionStatus`, nor per-encoder error info enumeration.

---

## Ranked root-cause hypotheses for the AC6 Continue lockup

These are not guesses from the title/menu path. They match the post-Continue workload transition and the local code gaps.

### H1 — In-flight resource lifetime / descriptor-backed resource retention gap

Why it fits:

- Solid magenta + page fault/MMU/firmware lockup is consistent with the GPU touching stale or invalid resource memory.
- Menu uses small stable descriptor/resource sets; real-world gameplay streams and recycles descriptors/resources aggressively.
- Current completion slots do not hold all resources referenced by submitted work.
- Apple headers explicitly say `useResource` does not retain.

Likely code areas:

```text
vendor/dxmt/src/d3d12/d3d12_command_buffer_completion.hpp
vendor/dxmt/src/d3d12/d3d12_command_queue.cpp
vendor/dxmt/src/d3d12/d3d12_descriptor_heap.hpp
vendor/dxmt/src/d3d12/d3d12_resource.hpp/.cpp
```

### H2 — Graphics descriptor-table / argument-buffer residency and snapshot gap

Why it fits:

- World rendering expands CBV/SRV/UAV descriptor-table pressure far beyond title/menu.
- RTV/DSV/UAV snapshots are real but too narrow: they do not snapshot every graphics SRV/CBV/UAV table resource used by draws.
- Metal requires explicit `useResource` declarations for argument-buffer resources before draws.

Likely code areas:

```text
vendor/dxmt/src/d3d12/d3d12_command_list.cpp
vendor/dxmt/src/d3d12/d3d12_command_defs.hpp
vendor/dxmt/src/d3d12/d3d12_command_queue.cpp
vendor/dxmt/src/d3d12/d3d12_root_signature.*
```

### H3 — UAV / aliasing / subresource barrier translation is incomplete for world-load compute→graphics chains

Why it fits:

- AC6 Continue enters heavy rendering/computation: culling, streaming, shadow/visibility passes, UAV clears/writes, copy→SRV transitions.
- Current barrier replay is coarse and diagnostic-heavy; it does not prove per-subresource states or cross-queue visibility.
- Metal needs explicit encoder boundaries / fences / events for untracked-resource hazards.

Likely code areas:

```text
vendor/dxmt/src/d3d12/d3d12_command_queue.cpp ResourceBarrier replay
vendor/dxmt/src/d3d12/d3d12_resource.*
vendor/dxmt/src/dxmt/dxmt_command_queue.*
vendor/dxmt/src/winemetal/Metal.hpp
```

### H4 — `ExecuteIndirect` CPU-readback semantics are wrong for GPU-generated world-render args/counts

Why it fits:

- Microsoft docs: indirect command/count buffers can be generated by CPU or GPU.
- Current replay maps argument/count buffers on CPU; if the count/args are produced by earlier GPU work, CPU readback can be stale, impossible, or racey.
- Title/menu may use static/simple indirect data; real-world gameplay is where GPU-driven indirect rendering begins.

Likely code area:

```text
vendor/dxmt/src/d3d12/d3d12_command_queue.cpp ExecuteIndirect case
```

### H5 — Present/drawable lifetime or producer→present ordering can amplify the magenta symptom

Why it fits:

- Magenta is a presentation-visible symptom, not necessarily the root cause.
- Present still has a live-present path that can skip CPU render wait.
- A backbuffer/drawable sampled before producer completion can show solid garbage and stress WindowServer.

Likely code areas:

```text
vendor/dxmt/src/d3d12/d3d12_swapchain.cpp
vendor/dxmt/src/dxmt/dxmt_presenter.*
vendor/dxmt/src/winemetal/unix/winemetal_unix.c
```

---

# Phase 1 — Make GPU work ownership and error reporting structurally safe

Goal: eliminate use-after-free/recycle classes and make command-buffer failures attributable without needing AC6 to hard-crash again.

This is the foundation phase. Do not touch AC6. Do not stage a runtime for gameplay.

## 1A. Implement command-buffer in-flight ownership buckets

Required fixes:

1. Extend `D3D12MetalCommandBufferCompletionSlot` or an adjacent in-flight submission record to hold strong references until Metal completion for:
   - D3D12 resources referenced by replayed packets;
   - WMT buffers;
   - WMT textures;
   - WMT drawables used by present;
   - command/replay transient argument/table buffers;
   - optional command-list/allocator-owned backing objects if reset can recycle memory.
2. Add explicit helper methods in replay state:
   - `RetainSubmissionResource(label, MTLD3D12Resource*)`
   - `RetainSubmissionBuffer(label, WMT::Buffer)`
   - `RetainSubmissionTexture(label, WMT::Texture)`
   - `RetainSubmissionDrawable(label, WMT::MetalDrawable)`
3. Call these helpers at every path that currently only logs `LogReplayRetentionCandidate(...)` or only calls `useResource(...)`:
   - draw/dispatch descriptor-backed resources;
   - root CBV/SRV/UAV resources;
   - RTV/DSV/UAV attachments and clear targets;
   - copy/resolve source/destination resources;
   - barrier referenced resources;
   - `ExecuteIndirect` arg/count buffers;
   - present source/drawable/drawable texture.
4. Release buckets only from Metal command-buffer completion / slot reset after completion.

Non-goals:

- Do not use COM `AddRef` blindly as a substitute for WMT/native Metal object retention unless the ownership path is proven.
- Do not rely on `useResource` for retention.
- Do not add broad default-on logging that changes timing.

## 1B. Add command-buffer error instrumentation to winemetal

Required fixes:

1. Add a winemetal bridge for command-buffer descriptor creation with:
   - `MTLCommandBufferDescriptor`
   - `errorOptions = MTLCommandBufferErrorOptionEncoderExecutionStatus`
   - retained-references option if exposed/available and safe.
2. Preserve existing `commandBuffer()` fast path, but allow M12 checked/offline probes to request descriptor-backed command buffers.
3. Add bounded command-buffer/encoder labels and a small ring of:
   - last N command buffers;
   - encoder type/label;
   - packet kind;
   - descriptor/resource summary;
   - barrier summary.
4. On `WMTCommandBufferStatusError`, report:
   - Metal status;
   - `MTLCommandBufferErrorDomain` code/description;
   - encoder execution status if available.

This is diagnostic instrumentation, but it is safe only after the retention model exists. It is not a replacement for the retention fix.

## 1C. Offline validation gates

Required probes before Phase 2:

```text
probe_command_buffer_error_instrumentation
probe_descriptor_mutation_graphics
```

Required existing checks:

```bash
git diff --check
meson compile -C vendor/dxmt/build-metalsharp-x64 d3d12 dxgi winemetal
./tools/d3d12-metal-sdk/scripts/build-probes.sh
./tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp --command-replay-only
./tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp --present-windowed-only
```

Sensitivity proof:

- In a throwaway branch or env-gated off path, disable the new retention bucket for descriptor-backed graphics resources.
- The new descriptor mutation/lifetime probe must fail.
- Re-enable the fix; the probe must pass.

Exit criteria:

- No AC6 launch.
- No staging for gameplay.
- Evidence bundle proves submitted command buffers hold all referenced resources until completion.

Phase 1C evidence update (2026-06-19):

- `git diff --check` passed.
- `meson compile -C vendor/dxmt/build-metalsharp-x64` passed.
- `./tools/d3d12-metal-sdk/scripts/build-probes.sh` passed.
- Temp-only runtime refreshed at `/tmp/m12-phase1-probe-layout-20260619-134855/wine/lib/dxmt_m12`; no Slice-2 or `~/.metalsharp` staging was changed.
- `DXMT_D3D12_COMMAND_BUFFER_ERROR_OPTIONS=1 DXMT_D3D12_SUBMISSION_RETENTION=1 ./tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp --dxmt-runtime /tmp/m12-phase1-probe-layout-20260619-134855/wine/lib/dxmt_m12 --results-dir tools/d3d12-metal-sdk/results/m12-phase1a1b-command-replay-postbuild-20260619-141420 --command-replay-only` passed (`probe-command-replay-metalsharp.json: pass=true`).
- `./tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp --dxmt-runtime /tmp/m12-phase1-probe-layout-20260619-134855/wine/lib/dxmt_m12 --results-dir tools/d3d12-metal-sdk/results/m12-phase1c-render-headless-20260619-141317 --render-headless ...` passed (`probe-render-headless-metalsharp.json: pass=true`) as the safe graphics descriptor-table/offscreen coverage check.
- Added dedicated non-windowed `probe_descriptor_mutation_graphics` and `--descriptor-mutation-graphics-only` runner mode after the temp-runtime windowed descriptor mutation probe timed out. Final result: `DXMT_D3D12_SUBMISSION_RETENTION=1 ./tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp --dxmt-runtime /tmp/m12-phase1-probe-layout-20260619-134855/wine/lib/dxmt_m12 --results-dir tools/d3d12-metal-sdk/results/m12-phase1c-descriptor-mutation-graphics-final-20260619-150358 --descriptor-mutation-graphics-only` passed (`probe-descriptor-mutation-graphics-metalsharp.json: pass=true`) with `rtv_descriptor_snapshot=true`, `dsv_descriptor_snapshot=true`, and `uav_descriptor_snapshot=true`.
- Autoreview passed with no findings after fixing the Wine/DYLD runner wrapper issue, moving `MTLCommandQueue_commandBufferWithDescriptor` to the existing unix-call slot 83, and adding the dedicated descriptor-mutation probe.
- Caveat: `M12_PRESENT_WINDOWED_DESCRIPTOR_MUTATION=1 ... --swapchain-only` against the temp runtime still has a prior timeout/empty JSON record; this is superseded for descriptor snapshot coverage by the green non-windowed descriptor-mutation probe above. No AC6 process was involved and no related Wine/probe process remained afterward.
- Added offline-only sensitivity gate `DXMT_D3D12_REQUIRE_SUBMISSION_RETENTION=1`, which fail-closes resource-bearing submissions if `DXMT_D3D12_SUBMISSION_RETENTION=0` leaves the submission reference bucket empty. This is intentionally diagnostic/offline only and is not enabled by default.
- Retention sensitivity is now distinguished with valid paired JSON evidence against the same temp runtime:
  - Positive: `DXMT_D3D12_REQUIRE_SUBMISSION_RETENTION=1 DXMT_D3D12_SUBMISSION_RETENTION=1 ./tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp --dxmt-runtime /tmp/m12-phase1-probe-layout-20260619-134855/wine/lib/dxmt_m12 --results-dir tools/d3d12-metal-sdk/results/m12-phase1c-retention-required-on-json-20260619-152834 --descriptor-mutation-graphics-only` passed (`probe-descriptor-mutation-graphics-metalsharp.json: pass=true`, `rtv_descriptor_snapshot=true`, `dsv_descriptor_snapshot=true`, `uav_descriptor_snapshot=true`).
  - Negative: `DXMT_D3D12_REQUIRE_SUBMISSION_RETENTION=1 DXMT_D3D12_SUBMISSION_RETENTION=0 ./tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp --dxmt-runtime /tmp/m12-phase1-probe-layout-20260619-134855/wine/lib/dxmt_m12 --results-dir tools/d3d12-metal-sdk/results/m12-phase1c-retention-required-off-json-20260619-152823 --descriptor-mutation-graphics-only` produced `probe-descriptor-mutation-graphics-metalsharp.json: pass=false`, with runtime stderr reporting `submission retention required but no submission references were retained ... dropping command buffer for offline sensitivity gate`.
- Fixed `probe_descriptor_mutation_graphics` JSON emission so non-finite depth readback values are emitted as `null`, keeping negative sensitivity evidence machine-readable.
- Post-gate validation passed: `git diff --check`, `meson compile -C vendor/dxmt/build-metalsharp-x64`, `./tools/d3d12-metal-sdk/scripts/build-probes.sh`, and `bash -n tools/d3d12-metal-sdk/scripts/build-probes.sh tools/d3d12-metal-sdk/scripts/run-probes.sh`.
- Fresh command replay after the retention gate passed: `DXMT_D3D12_COMMAND_BUFFER_ERROR_OPTIONS=1 DXMT_D3D12_SUBMISSION_RETENTION=1 ./tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp --dxmt-runtime /tmp/m12-phase1-probe-layout-20260619-134855/wine/lib/dxmt_m12 --results-dir tools/d3d12-metal-sdk/results/m12-phase1a1b-command-replay-post-retention-gate-20260619-152947 --command-replay-only` (`probe-command-replay-metalsharp.json: pass=true`).
- Focused autoreview after the retention sensitivity gate found no blockers/warnings. It noted only two info-level scope/compatibility items: default-on submission retention applies to all M12 titles unless disabled with `DXMT_D3D12_SUBMISSION_RETENTION=0`, and probe stderr filtering is now opt-in with `D3D12_METAL_SDK_ENABLE_WINE_STDERR_FILTER=1` to preserve temp-runtime `DYLD_LIBRARY_PATH` binding.

---

# Phase 2 — Fix D3D12→Metal hazard semantics for the real Continue workload

Goal: address the actual high-risk world-rendering paths: graphics descriptor tables, `ExecuteIndirect`, UAV/aliasing/subresource barriers, copy→SRV, and present ordering.

## 2A. Graphics descriptor-table snapshot and residency blanket

Required fixes:

1. Snapshot graphics CBV/SRV/UAV descriptor-table contents into command-list/replay-owned packets, not only RTV/DSV/UAV clear/attachment descriptors.
2. For every draw path, collect concrete resources reachable from:
   - graphics root descriptor tables;
   - root CBV/SRV/UAV descriptors;
   - vertex/index buffers;
   - transient argument/table buffers;
   - indirect draw resources;
   - render/depth attachments.
3. For all resources accessed through Metal argument buffers, call `useResource(..., usage, stages)` before the draw.
4. Retain the same resources in the Phase 1 submission bucket.
5. For null/missing descriptor entries, fail closed in checked/offline mode:
   - substitute known-safe null resources, or
   - skip the draw with explicit counter evidence,
   - but never submit an invalid GPU pointer.

Required probe:

```text
probe_argbuf_residency_graphics
```

Phase 2A evidence update (2026-06-19):

- Added dedicated `probe_argbuf_residency_graphics` and `--argbuf-residency-graphics-only` runner mode.
- Probe behavior: create buffer SRV A containing red and buffer SRV B containing blue, record graphics SRV descriptor table pointing at A, close the command list, overwrite the CPU descriptor to point at B before `ExecuteCommandLists`, then draw/read back. Correct record-time descriptor snapshot/residency must still sample A/red.
- Baseline temp-runtime result proved the Phase 2A gap before the fix: `./tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp --dxmt-runtime /tmp/m12-phase1-probe-layout-20260619-134855/wine/lib/dxmt_m12 --results-dir tools/d3d12-metal-sdk/results/m12-phase2a-argbuf-residency-graphics-baseline-20260619-154516 --argbuf-residency-graphics-only` produced `probe-argbuf-residency-graphics-metalsharp.json: pass=false`, expected `[255,0,0,255]`, observed `[0,0,255,255]`. This confirmed graphics shader-visible descriptor tables were still resolved from the live heap during replay/argument-buffer build.
- Implemented default-on, env-disableable record-time graphics descriptor-table snapshots:
  - `DXMT_D3D12_GRAPHICS_DESCRIPTOR_TABLE_SNAPSHOT=0` disables the fix for sensitivity testing.
  - `DXMT_D3D12_GRAPHICS_DESCRIPTOR_TABLE_SNAPSHOT_CAP` bounds snapshot size (default 256, packet-safe effective cap so the var-sized command stays below the replay corrupt-command guard).
  - `SetGraphicsRootDescriptorTable` now snapshots the active root parameter's declared descriptor-table span into the command stream. Replay stores per-root snapshots and the graphics argument-buffer builder prefers snapshots over live heap descriptors. Snapshot resources are retained through the Phase 1 submission bucket.
- Positive evidence: `DXMT_D3D12_GRAPHICS_DESCRIPTOR_TABLE_SNAPSHOT=1 ./tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp --dxmt-runtime /tmp/m12-phase1-probe-layout-20260619-134855/wine/lib/dxmt_m12 --results-dir tools/d3d12-metal-sdk/results/m12-phase2a-argbuf-residency-graphics-snapshot-on-20260619-155639 --argbuf-residency-graphics-only` produced `probe-argbuf-residency-graphics-metalsharp.json: pass=true`, observed `[255,0,0,255]`.
- Autoreview fix: corrected descriptor-table snapshot span to use `max(range.offset_in_table + range.num_descriptors)` instead of root range count, made record-time GPU descriptor snapshotting go through the command list's currently bound descriptor heaps for bounds-checked `GetDescriptorFromGPUHandle` lookups, and clamped the effective snapshot cap to keep var-sized commands below the replay corrupt-command guard. Snapshot data now carries scalar Metal GPU IDs but refuses to use lossy snapshots as binding data when the descriptor depends on cached Metal texture-view or sampler objects; those descriptors still contribute retention but fall back to live binding until a richer view/sampler snapshot representation exists.
- Final positive buffer-SRV evidence: `DXMT_D3D12_GRAPHICS_DESCRIPTOR_TABLE_SNAPSHOT=1 ... --argbuf-residency-graphics-only` with results at `tools/d3d12-metal-sdk/results/m12-phase2a-argbuf-residency-graphics-buffer-srv-20260619-164959/probe-argbuf-residency-graphics-metalsharp.json` passed, observed `[255,0,0,255]`.
- Sensitivity/off-path evidence: `DXMT_D3D12_GRAPHICS_DESCRIPTOR_TABLE_SNAPSHOT=0 ./tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp --dxmt-runtime /tmp/m12-phase1-probe-layout-20260619-134855/wine/lib/dxmt_m12 --results-dir tools/d3d12-metal-sdk/results/m12-phase2a-argbuf-residency-graphics-buffer-srv-off-20260619-165216 --argbuf-residency-graphics-only` produced `probe-argbuf-residency-graphics-metalsharp.json: pass=false`, observed `[0,0,255,255]`.
- Post-Phase-2A validation passed: `git diff --check`, `meson compile -C vendor/dxmt/build-metalsharp-x64`, `./tools/d3d12-metal-sdk/scripts/build-probes.sh`, and `bash -n tools/d3d12-metal-sdk/scripts/build-probes.sh tools/d3d12-metal-sdk/scripts/run-probes.sh`. The `build-probes.sh` manifest now includes `probe_argbuf_residency_graphics.exe`.
- Final focused autoreview after packet cap, bounds, scalar GPU-ID, lossy view/sampler fallback, and buffer-SRV probe changes returned no findings (`[]`).
- Command replay regression passed after the graphics table snapshot fix: `DXMT_D3D12_COMMAND_BUFFER_ERROR_OPTIONS=1 DXMT_D3D12_SUBMISSION_RETENTION=1 ./tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp --dxmt-runtime /tmp/m12-phase1-probe-layout-20260619-134855/wine/lib/dxmt_m12 --results-dir tools/d3d12-metal-sdk/results/m12-phase2a-command-replay-post-graphics-snapshot-20260619-160525 --command-replay-only` (`probe-command-replay-metalsharp.json: pass=true`). After the span/bounds fix, rerun command replay also passed: `tools/d3d12-metal-sdk/results/m12-phase2a-command-replay-post-autoreview-fix-20260619-161646/probe-command-replay-metalsharp.json: pass=true`. Final command replay after scalar GPU-ID/lossy-fallback handling also passed: `tools/d3d12-metal-sdk/results/m12-phase2a-command-replay-gpuid-fix-20260619-165406/probe-command-replay-metalsharp.json: pass=true`.

## 2B. Replace unsafe `ExecuteIndirect` CPU-readback with a correctness model

Current risk:

`d3d12_command_queue.cpp` maps `argument_buffer` and `count_buffer` on CPU during replay. That is incompatible with GPU-generated indirect args/counts unless a prior GPU completion boundary has made the data CPU-visible and current.

Required fixes, in order:

1. Add explicit classification:
   - CPU-authored indirect args/counts that are safely mappable and not GPU-written in the current in-flight interval;
   - GPU-authored args/counts requiring GPU-side execution.
2. For GPU-authored indirect data, do **not** CPU-readback to decide command count.
3. Implement one of:
   - Metal indirect draw/dispatch path using supported indirect APIs / ICB, or
   - conservative GPU-side command materialization path, or
   - safe serialization fallback that waits for the producer completion before CPU materialization only in offline/compat mode.
4. Retain arg/count buffers until completion.
5. Reset command-signature-mutated bindings after `ExecuteIndirect` according to D3D12 semantics.

Required probe:

```text
probe_execute_indirect_draw_replay
```

Probe must verify:

- draw;
- draw indexed;
- count buffer = 0;
- count buffer = N;
- GPU-authored or mutation-between-record/execute behavior;
- clean handling of unsupported paths without GPU corruption.

Phase 2B evidence update (2026-06-19):

- Added dedicated `probe_execute_indirect_draw_replay` and `--execute-indirect-draw-replay-only` runner mode.
- Baseline CPU-authored evidence against the temp runtime: `./tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp --dxmt-runtime /tmp/m12-phase1-probe-layout-20260619-134855/wine/lib/dxmt_m12 --results-dir tools/d3d12-metal-sdk/results/m12-phase2b-execute-indirect-draw-replay-baseline-20260619-171159 --execute-indirect-draw-replay-only` produced `probe-execute-indirect-draw-replay-metalsharp.json: pass=true`.
- Autoreview fix: corrected the probe to use `MaxCommandCount=1` for single-command argument buffers and `MaxCommandCount=2` only for the two-command `count_n` case, avoiding spec-invalid out-of-bounds indirect argument reads. Rerun evidence: `tools/d3d12-metal-sdk/results/m12-phase2b-execute-indirect-draw-replay-maxcount-fix-20260619-172739/probe-execute-indirect-draw-replay-metalsharp.json: pass=true`.
- Covered cases: direct draw red, indexed draw green, count buffer `0` black/no-draw, count buffer `N=2` blue/second command, and argument-buffer mutation after `Close()` but before `ExecuteCommandLists()` blue/execute-time mutation visible.
- Follow-up GPU/default-heap safety fix: non-CPU-visible argument buffers now classify and skip explicitly; non-CPU-visible count buffers now also classify and skip instead of falling back to `MaxCommandCount` and over-submitting.
- Temp full-runtime validation used `/tmp/m12-phase2b-gpu-unsupported-layout-20260619-175427/wine/lib/dxmt_m12` with the complete DLL/sidecar set overlaid from the current build.
- Extended evidence: `tools/d3d12-metal-sdk/results/m12-phase2b-execute-indirect-gpu-unsupported-info-20260619-175438/probe-execute-indirect-draw-replay-metalsharp.json: pass=true`.
- Extended coverage: `gpu_argument_buffer_unsupported=true` and `gpu_count_buffer_unsupported=true`; both default-heap cases observed black/no-draw and logged fail-closed classifications: `ExecuteIndirect skipped gpu/non-CPU-visible argument buffer` and `ExecuteIndirect skipped gpu/non-CPU-visible count buffer`.
- Original gap was explicit in JSON as `gpu_authored_args=false`; that fail-closed state prevented unsafe CPU readback/over-submit for non-CPU-visible inputs, but hid GPU-driven geometry.
- GPU/default-heap materialization update: `ExecuteIndirect` now materializes non-CPU-visible argument/count buffers by splitting the current Metal command buffer, copying the needed range into a shared staging buffer, waiting for producer completion, replaying the indirect records from materialized bytes, and resuming replay on a fresh Metal command buffer. Env gate: `DXMT_D3D12_EXECUTE_INDIRECT_GPU_MATERIALIZE` defaults on; `DXMT_D3D12_EXECUTE_INDIRECT_MATERIALIZE_CAP` caps staged bytes (default 1 MiB).
- Lifetime/offset fixes for the materialization path: `D3D12MetalSubmissionReferences::Duplicate()` gives intermediate materialization command buffers an independent retain bucket; `MTLD3D12Resource::GetBackingOffset()` is applied to GPU blit offsets for placed resources; `CopyBufferRegion` now also applies source/destination backing offsets so placed default-heap buffers receive uploaded argument/count data at the correct heap suballocation.
- Positive GPU materialization evidence (fresh cloned temp runtime `/tmp/m12-phase2b-gpu-materialize-cloned-layout-20260619-214947/wine/lib/dxmt_m12`): `tools/d3d12-metal-sdk/results/m12-phase2b-execute-indirect-gpu-materialize-placed-copyfix-20260619-222216/probe-execute-indirect-draw-replay-metalsharp.json: pass=true`. Coverage includes `gpu_argument_buffer_materialized=true`, `gpu_count_buffer_materialized=true`, `placed_gpu_argument_buffer_materialized=true`, `placed_gpu_count_buffer_materialized=true`, and `gpu_authored_args=true`.
- Materialization sensitivity-off evidence: `DXMT_D3D12_EXECUTE_INDIRECT_GPU_MATERIALIZE=0` produced `tools/d3d12-metal-sdk/results/m12-phase2b-execute-indirect-gpu-materialize-off-placed-copyfix-20260619-222733/probe-execute-indirect-draw-replay-metalsharp.json: pass=false`, with only GPU/default-heap and placed-GPU materialized cases failing and logs `ExecuteIndirect skipped gpu/non-CPU-visible argument buffer ... materialize=0` / `ExecuteIndirect skipped gpu/non-CPU-visible count buffer ... materialize=0`.
- Command replay regression evidence after materialization/backing-offset fixes: `tools/d3d12-metal-sdk/results/m12-phase2b-command-replay-after-placed-copyfix-20260619-223146/probe-command-replay-metalsharp.json: pass=true`.
- Post-probe validation passed after the final Phase 2B materialization/backing-offset changes: `git diff --check`, `meson compile -C vendor/dxmt/build-metalsharp-x64`, `./tools/d3d12-metal-sdk/scripts/build-probes.sh`, and `bash -n tools/d3d12-metal-sdk/scripts/build-probes.sh tools/d3d12-metal-sdk/scripts/run-probes.sh`.
- Focused autoreview after fixing the spec-invalid `MaxCommandCount` found no blockers/warnings. It noted only an info-level production-scope item: default-on submission retention applies to all M12 titles unless disabled with `DXMT_D3D12_SUBMISSION_RETENTION=0`, which is intentional for the Phase 1 lifetime mitigation.
- Focused autoreview after the GPU/default-heap materialization and placed-resource backing-offset/copy-buffer fixes returned no findings (`[]`): command-buffer split/resume state, submission retention lifetime, Map/Unmap behavior, bounds/caps, and probe validity were reviewed as correct. The unreadable count-buffer path remains intentionally fail-closed.

## 2C. Barrier / subresource / queue synchronization hardening

Required fixes:

1. Track resource state at least per subresource for diagnostics, and for correctness where current whole-resource state is insufficient.
2. Map D3D12 barriers to real Metal ordering:
   - transition barrier: encoder boundary and state update for the specific subresource/range;
   - UAV barrier: end current encoder and insert a real fence/event ordering point for affected resource or conservative global memory ordering;
   - aliasing barrier: force visibility/invalidation and prevent overlapping use of aliased memory;
   - copy→SRV transition: prove blit writes complete before shader read.
3. Cross-queue path must use queue signal/wait / shared event semantics, not only same-command-buffer signal-then-wait.
4. Record counts and evidence for every barrier class, but keep logging bounded.

Required probe:

```text
probe_barriers_uav_aliasing_subresource
```

Required pass cases:

- compute UAV write → graphics SRV read;
- texture copy→SRV;
- subresource mip transitions;
- texture aliasing reported supported/pass or explicitly unsupported without corruption.

Phase 2C evidence update (2026-06-19):

- Added dedicated `probe_barriers_uav_aliasing_subresource` and `--barriers-uav-aliasing-subresource-only` runner mode.
- Positive evidence against temp runtime `/tmp/m12-phase2b-gpu-unsupported-layout-20260619-175427/wine/lib/dxmt_m12`: `tools/d3d12-metal-sdk/results/m12-phase2c-barriers-uav-aliasing-subresource-coverage-fix-20260619-183024/probe-barriers-uav-aliasing-subresource-metalsharp.json: pass=true`.
- Covered cases: compute UAV write → graphics SRV read (`pixel=[12,34,56,255]`), texture copy→SRV transition (`first_pixel=[11,22,33,255]`), UAV barrier visibility (`values=[47,48,49,50]`), subresource mip transition (`mip1_pixel=[0,177,0,255]`), buffer aliasing barrier (`values=[91,92,93,94]`, `aliasing_supported=true`), present transition roundtrip, render-pass split clear/store, and explicit resolve unsupported status.
- Focused autoreview found no D3D12 correctness issue in the Phase 2C cases or runner wiring; it requested removing an overclaimed `render_target_to_shader_resource_status` coverage field, which was fixed.
- Runner exclusivity audit/fix: every `run-probes.sh` `*-only` block now assigns all 28 `RUN_*` variables, including `RUN_HEAP_ALIASING=0` in `--semantic-only` and `--mini-only`; final focused autoreview returned `[]`.
- Validation passed after Phase 2C probe/wiring: `git diff --check`, `meson compile -C vendor/dxmt/build-metalsharp-x64`, `./tools/d3d12-metal-sdk/scripts/build-probes.sh`, and `bash -n tools/d3d12-metal-sdk/scripts/build-probes.sh tools/d3d12-metal-sdk/scripts/run-probes.sh`.
- Added/defaulted barrier replay sensitivity gates: `DXMT_D3D12_BARRIER_REPLAY` defaults on, and offline-only `DXMT_D3D12_REQUIRE_BARRIER_REPLAY=1` with `DXMT_D3D12_BARRIER_REPLAY=0` drops barrier-bearing command buffers with an explicit log instead of committing unsafe work.
- Fresh full temp runtime for sensitivity: `/tmp/m12-phase2c-barrier-gate-layout-20260619-192224/wine/lib/dxmt_m12`.
- Positive sensitivity evidence: `tools/d3d12-metal-sdk/results/m12-phase2c-barrier-gate-positive-20260619-192239/probe-barriers-uav-aliasing-subresource-metalsharp.json: pass=true`.
- Negative sensitivity evidence: `tools/d3d12-metal-sdk/results/m12-phase2c-barrier-gate-required-off-20260619-192249/probe-barriers-uav-aliasing-subresource-metalsharp.json: pass=false`, with logs `ExecuteCommandLists: barrier replay required but DXMT_D3D12_BARRIER_REPLAY=0 ... dropping command buffer for offline sensitivity gate`.
- Focused autoreview of the barrier sensitivity gate found no blocker/warning for the gate: default behavior is unchanged, the required-off drop path is safe, and positive/negative evidence is credible. It repeated only the already-documented info-level ExecuteIndirect fail-closed tradeoff for unreadable GPU count buffers.
- Cross-queue audit: `MTLD3D12CommandQueue::Signal` and `Wait` use `MTLSharedEvent` via `encodeSignalEvent`/`encodeWaitForEvent`, not CPU-only waits.
- Cross-queue evidence: added `--queues-only` runner mode and ran `probe_queues` against the Phase 2C temp runtime. `tools/d3d12-metal-sdk/results/m12-phase2c-cross-queue-probe-queues-20260619-201356/probe-queues-metalsharp.json: pass=true`, with copy→direct→compute→direct queue Signal/Wait calls all `0x00000000`, fence completions `1/2/3/4`, and readback `verified=true`.
- Default matrix cleanup after autoreview: `probe_descriptor_mutation_graphics` and `probe_argbuf_residency_graphics` now run by default because graphics descriptor-table snapshots are default-on; explicit skip switches `--no-descriptor-mutation-graphics` and `--no-argbuf-residency-graphics` were added.
- Default matrix descriptor evidence against latest Phase 2C temp runtime: `tools/d3d12-metal-sdk/results/m12-phase2c-default-matrix-descriptor-mutation-20260619-202643/probe-descriptor-mutation-graphics-metalsharp.json: pass=true`; `tools/d3d12-metal-sdk/results/m12-phase2c-default-matrix-argbuf-residency-20260619-202649/probe-argbuf-residency-graphics-metalsharp.json: pass=true`.
- WineMetal ABI contract cleanup: added `MTLCommandQueue_commandBufferWithDescriptor`, `_MTLCommandQueue_commandBufferWithDescriptor`, and `unixcall_mtlcommandqueue_cmdbuf_descriptor: 32` to `tools/d3d12-metal-sdk/contracts/winemetal-bridge-contract.json`. Temp-runtime ABI gate passed: `tools/d3d12-metal-sdk/results/m12-phase2c-winemetal-abi-contract-temp-runtime-20260619-202628/winemetal-abi-metalsharp.json: ok=true`. A real prefix check failed earlier only because the live prefix has not been staged with this temp runtime, which is expected under the no-staging constraint.
- Phase 2C offline requirements are complete: dedicated probe coverage, positive evidence, required-off sensitivity evidence, cross-queue shared-event audit, validation, and focused autoreview are all recorded.

## 2D. Present/drawable lifetime cage

Required fixes:

1. Treat live-present CPU render-wait skip as unsafe for AC6 Continue until proven; default the candidate lane to ordered producer→present.
2. Retain source backbuffer, source texture, drawable, and drawable texture until present command-buffer completion.
3. Ensure exactly one path presents each drawable:
   - presenter path, or
   - raw blit path, or
   - native M12Core present path,
   - never multiple.
4. Resize/recreate cage: old backbuffers/drawables cannot be destroyed/reused until all in-flight command buffers referencing them complete.

Required probe:

```text
probe_present_lifetime_resize
```

Phase 2D evidence update (2026-06-19):

- Runtime resize/recreate cage added in `vendor/dxmt/src/d3d12/d3d12_swapchain.cpp`: `ResizeBuffers` now calls `DrainPresentCommandBuffers(true)` before releasing/recreating swapchain backbuffers, so resize cannot drop old backbuffer/drawable ownership while present command buffers are still in flight.
- Present ordering/lifetime audit is source-backed by `probe_present_lifetime_resize` after the windowed runtime variant stalled before JSON in the local Wine/WindowServer path. The source-contract probe verifies:
  - `DXMT_D3D12_LIVE_PRESENT` remains explicit opt-in and default present waits for producer queue work;
  - source backbuffer/resource, source texture, destination texture, drawable, and drawable texture are retained through present command-buffer completion;
  - raw/native/presenter present ownership is mutually classified, with raw `presentDrawable` skipped after native present execution;
  - resize drains present inflight slots before backbuffer replacement;
  - present completion slots wait/observe completion before retained references are reset.
- Positive evidence: `tools/d3d12-metal-sdk/results/m12-phase2d-present-lifetime-resize-source-contract-pass-20260619-210345/probe-present-lifetime-resize-metalsharp.json: pass=true`.
- Windowed attempt retained as non-authoritative environment evidence: `tools/d3d12-metal-sdk/results/m12-phase2d-present-lifetime-resize-timeout-20260619-205124/` timed out before JSON immediately after D3D12 queue creation; no AC6 launch was performed.
- Validation after Phase 2D passed: `git diff --check`, `meson compile -C vendor/dxmt/build-metalsharp-x64`, `./tools/d3d12-metal-sdk/scripts/build-probes.sh`, and `bash -n tools/d3d12-metal-sdk/scripts/build-probes.sh tools/d3d12-metal-sdk/scripts/run-probes.sh`.

Exit criteria for Phase 2:

- All new probes pass.
- Existing probe suite remains green.
- Sensitivity tests demonstrate at least the descriptor/retention and barrier probes fail when the corresponding fix is disabled.
- No AC6 launch has occurred.

---

# Phase 3 — Candidate build, full staging gate, then one explicitly-approved AC6 Continue retest

Goal: only after Phase 1/2 correctness is proven offline, create a candidate runtime and ask whether a single controlled Continue retest is acceptable.

## 3A. Pre-stage evidence bundle

Required contents:

```text
commit hash
full git diff summary
build logs
probe JSON results
sensitivity-test results
runtime DLL/sidecar hash manifest
mscompatdb absence proof
backend status proof on METALSHARP_PORT=9277
exact env payload
known residual risk statement
rollback path to a909c83 / Slice-2 baseline
```

Required commands/checks:

```bash
git diff --check
meson compile -C vendor/dxmt/build-metalsharp-x64
./tools/d3d12-metal-sdk/scripts/build-probes.sh
./tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp
python3 tools/d3d12-metal-sdk/scripts/preflight-runtime-layout.py --pipeline m12
python3 tools/d3d12-metal-sdk/scripts/verify-m12-runtime-hashes.py ...
```

Phase 3A evidence update (2026-06-19):

- Pre-stage evidence bundle created at `tools/d3d12-metal-sdk/results/m12-phase3a-prestage-evidence-bundle-20260619-233844/`. This is offline/temp-runtime only; no live runtime staging and no AC6 launch were performed.
- Bundle includes: commit/branch metadata, `git diff --check`, diff stat/name-status, build logs, probe JSON summary, sensitivity references, runtime DLL/sidecar SHA-256 manifest, `mscompatdb` absence proof, backend status proof on `METALSHARP_PORT=9277`, exact env payload, known residual risks, and rollback path to `a909c83` / Slice-2 baseline.
- Corrected runtime preflight passed: `tools/d3d12-metal-sdk/results/m12-phase3a-prestage-evidence-bundle-20260619-233844/preflight-corrected/runtime-preflight-metalsharp.json: ok=true, failure_count=0`. The initial `preflight/` run used the temp DXMT clone as `--wine-runtime` and failed only because that clone does not contain the full Wine builtin tree; `preflight-corrected/` uses the temp DXMT runtime plus read-only installed Wine runtime `/Users/alexmondello/.metalsharp/runtime/wine` and is authoritative.
- Runtime hash manifest: `tools/d3d12-metal-sdk/results/m12-phase3a-prestage-evidence-bundle-20260619-233844/runtime-sha256-manifest.tsv`, covering `d3d12.dll`, `d3d11.dll`, `dxgi.dll`, `dxgi_dxmt.dll`, `d3d10core.dll`, `winemetal.dll`, `winemetal.so`, and `libm12core.dylib` from temp runtime `/tmp/m12-phase2b-gpu-materialize-cloned-layout-20260619-214947/wine/lib/dxmt_m12`.
- Focused offline gauntlet passed as individual bounded probes: `tools/d3d12-metal-sdk/results/m12-phase3a-focused-offline-gauntlet-20260619-232742/`, with pass/ok JSONs for WineMetal ABI, queues, descriptor mutation graphics, graphics argbuf residency, ExecuteIndirect GPU/default/placed materialization, barrier/render-pass, UAV/aliasing/subresource barriers, command replay, resource-views-formats, heap aliasing, and present lifetime/resize source contract. Two probes returned shell `timeout` only after writing `pass=true` JSON during Wine cleanup; their JSONs are authoritative.
- One-shot default runner attempt `tools/d3d12-metal-sdk/results/m12-phase3a-full-default-probes-prestage-20260619-231203/` is non-authoritative because it timed out during Wine/probe cleanup after only ABI JSON; the per-probe gauntlet supersedes it.
- Backend proof: `tools/d3d12-metal-sdk/results/m12-phase3a-prestage-evidence-bundle-20260619-233844/backend-9277-proof.txt` shows `METALSHARP_PORT=9277` `/status` returned `ok=true` with backend pid/version. `/health` returns an application-level not-found JSON and is not used as the authoritative status endpoint.
- Final autoreview reported no blocker/warning; it noted two intentional info-level caveats: record-time graphics descriptor snapshots can be disabled with `DXMT_D3D12_GRAPHICS_DESCRIPTOR_TABLE_SNAPSHOT=0` if a title regresses, and ExecuteIndirect count-buffer materialization failure is fail-closed rather than falling back to `MaxCommandCount`.
- After final autoreview, an ExecuteIndirect log-only diagnosability change updated `d3d12.dll`; the temp candidate runtime was refreshed, `runtime-sha256-manifest.tsv` regenerated (`d3d12.dll sha256=b092c3d3c7efaea90cdccfd9ae5fce7d26b282602ac2561c84f674ce2e9ded3c`), corrected preflight rerun (`ok=true`), and ExecuteIndirect positive/sensitivity probes rerun: `tools/d3d12-metal-sdk/results/m12-phase3a-execute-indirect-after-final-logfix-20260619-235752/probe-execute-indirect-draw-replay-metalsharp.json: pass=true`; `tools/d3d12-metal-sdk/results/m12-phase3a-execute-indirect-materialize-off-after-final-logfix-20260620-000252/probe-execute-indirect-draw-replay-metalsharp.json: pass=false` as expected.
- Phase 3B approval was granted on 2026-06-20 for runtime + AC6 game-local Windows DLL staging under the Slice-2 runbook, but not for AC6 launch.
- Phase 3B staging evidence: `tools/d3d12-metal-sdk/results/m12-phase3b-stage-verify-20260620-001148/`. Backup root: `/tmp/metalsharp-m12-phase3b-stage-backup-20260620-001148/`.
- Staged full runtime set into `~/.metalsharp/runtime/wine/lib/dxmt_m12`: Windows DLLs `d3d12.dll`, `d3d11.dll`, `dxgi.dll`, `dxgi_dxmt.dll`, `d3d10core.dll`, `winemetal.dll`; Unix sidecars `winemetal.so`, `libm12core.dylib`. Copied the approved Windows DLL set into the AC6 game directory per Slice-2 hash-sync requirement.
- Phase 3B verification passed: `hash-sync.json: ok=true` for build/runtime/game Windows DLLs and build/runtime Unix sidecars; `preflight/runtime-preflight-metalsharp.json: ok=true, failure_count=0`; `mscompatdb-found-after.txt` is empty/absent; backend `/status` on `127.0.0.1:9277` returned `ok=true`.
- Backend dry-run (`diagnostics/pipeline/dry-run?appid=1888160&pipeline=m12`) returned `ok=true`, route `pipeline=m12`, `WINEDLLPATH` under `dxmt_m12/x86_64-windows`, `DYLD_LIBRARY_PATH` starting with `dxmt_m12/x86_64-unix`, and `DXMT_WINEMETAL_UNIXLIB=winemetal.so`. The dry-run base includes trace/log defaults; the actual launch remains blocked pending explicit approval and must use the runbook no-log `POST /steam/launch-game` env overrides.

Use the authoritative Slice-2 runbook constraints:

```text
/Users/alexmondello/slice2baseline/latest/repo/m12-ac6-slice2-stage-launch-runbook.md
```

Hard constraints:

- Use `POST /steam/launch-game`, not `/mtsp/prepare`.
- Use `METALSHARP_PORT=9277`, not `PORT`.
- Route through `~/.metalsharp/runtime/wine/lib/dxmt_m12`.
- Keep `mscompatdb` absent/excluded.
- Stage full runtime DLL/sidecar set only.
- Do not kill Steam/Wine/backend/game processes without explicit approval.
- Do not commit without explicit approval.
- Do not launch without explicit approval.

## 3B. Candidate staging only after approval

If the user approves staging:

1. Back up current runtime/game-local DLL state.
2. Build from the exact reviewed commit.
3. Stage the full AC6 DLL set:
   - `d3d12.dll`
   - `d3d11.dll`
   - `dxgi.dll`
   - `dxgi_dxmt.dll`
   - `d3d10core.dll`
   - `winemetal.dll`
   - `winemetal.so`
   - `libm12core.dylib`
4. Verify hashes and runtime layout.
5. Preserve evidence root under `/tmp` or `tools/d3d12-metal-sdk/results/<stamp>`.

## 3C. Retest protocol only after explicit visual launch approval

Retest target:

```text
AC6 Continue → real-world gameplay/loading/rendering/computation
```

Not the target:

```text
title/menu launch
```

Retest design:

1. Quick title/menu sanity gate only to confirm no regression.
2. One controlled Continue attempt.
3. No repeated crash loops.
4. No heavy tracing by default; no-log payload remains the default unless user explicitly approves diagnostic mode.
5. If the game reaches real-world gameplay without magenta/MMU/firmware lockup, preserve evidence and stop.
6. If it fails, do not immediately rerun. Preserve logs/reports, restore baseline if needed, and inspect Phase 1 command-buffer error evidence / Phase 2 probe coverage gap before trying again.

---

## Implementation order summary

1. **Phase 1 first:** retention buckets + command-buffer error options + graphics descriptor mutation/lifetime probe.
2. **Phase 2 second:** graphics argbuf residency/snapshots + `ExecuteIndirect` correctness + barrier/subresource/queue synchronization + present cage.
3. **Phase 3 last:** only after offline evidence, stage candidate and ask for one approved AC6 Continue retest.

This roadmap is intentionally not a “diagnose by crashing AC6” roadmap. The next AC6 Continue test is allowed only after the code has been changed in ways that directly address the likely page-fault/MMU lockup classes and after offline probes prove those fixes are active.
