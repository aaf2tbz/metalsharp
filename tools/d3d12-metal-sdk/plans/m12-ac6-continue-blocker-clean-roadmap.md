# M12 AC6 Continue Blocker Clean Roadmap

Date: 2026-06-20

## Stable baseline identity

This roadmap starts from the restored exact Slice-3 baseline, not Phase 3C.

- Repo: `/Users/alexmondello/metalsharp-m12-lab`
- Branch: `fix/m12-shader-probe-lab`
- HEAD: `a1d7feda464ee2a96f8d3ef3931a1480564e0d96`
- Commit: `a1d7fed fix(m12): establish AC6 safety diagnostics baseline`
- Runtime path: `~/.metalsharp/runtime/wine/lib/dxmt_m12`
- Backend: `127.0.0.1:9277`
- Route: `POST /steam/launch-game`
- `mscompatdb`: absent

Exact active Slice-3 artifact hashes:

```text
822f30b9bcf0f6f9b841f58c4c7025a2d37d7fb923c47c062db4629bc8e3b82d  d3d12.dll
8e93443f84d49ec088649b5a5f62c9efabd90298550eaf985511eab8085b5885  d3d11.dll
6174e02cf4e45a08dafd8e457054f778b2e784efb5dfa38645492bad2d392881  dxgi.dll
2675439c84887e70cf96fdce9285ff1ce3293147e7015fff06eb42521a353a3b  dxgi_dxmt.dll
b8769b4d197ade16c699bfbc38343a880d6ffde078ae592e7f5d05a47725091e  d3d10core.dll
e6b6eccac5aaa61fa8802cb717599a3e2ce7822744053e33bb8c74e1e9c20001  winemetal.dll
cabb1648712124d556e9143a516dd15170b1140a0b01907bb9bfefd531ce6e91  x86_64-unix/winemetal.so
```

Healthy menu evidence:

- Launch evidence: `tools/d3d12-metal-sdk/results/slice3-restored-runbook-20260620-093912/15-user-approved-launch-slice3-baseline-20260620-094625`
- Healthy diagnostic snapshot: `.../xcodedbg-healthy-menu-20260620-094757`
- Process survived `sample` + LLDB attach/backtrace/detach.
- LLDB stop reason was expected `SIGSTOP`, not a crash.
- Healthy baseline had `LoadProcess_M1..M5`, `GXRenderThread`, `dxmt-encode-thr`, and `dxmt-finish-thr` present and stable.

## Diff/backup facts

There are two useful comparison sources:

1. `backup/pre-slice3-restore-20260620-093808`
   - Focused committed diff against current Slice-3 mainly shows `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp` ExecuteIndirect/materialization work.
2. `/tmp/metalsharp-m12-pre-slice3-restore-20260620-093757/repo/tracked-changes.patch`
   - Captures the broader pre-restore uncommitted tracked patch.
   - Patch touches 18 files, including:
     - `vendor/dxmt/src/winemetal/unix/winemetal_unix.c`
     - `vendor/dxmt/src/d3d12/d3d12_swapchain.cpp/.hpp`
     - `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp/.hpp`
     - `vendor/dxmt/src/d3d12/d3d12_command_list.cpp/.hpp`
     - `vendor/dxmt/src/d3d12/d3d12_resource.hpp`
     - `vendor/dxmt/src/winemetal/Metal.hpp`, `winemetal.h`, thunks

Important interpretation: Phase 3C/AGX work is backed up but not active. Do not reapply it wholesale. Cherry-pick only narrow slices after proving each one from Slice-3.

## Current implementation sites relevant to the Continue blocker

### Immediate Metal allocation / view creation

`vendor/dxmt/src/winemetal/unix/winemetal_unix.c`

- `_MTLDevice_newTexture` around line 1530
- `_MTLBuffer_newTexture` around line 1547
- `_MTLTexture_newTextureView` around line 1593
- `_MTLCommandBuffer_blitCommandEncoder` around line 1789
- `_MTLCommandBuffer_computeCommandEncoder` around line 1796
- `_MTLCommandBuffer_renderCommandEncoder` around line 1804
- `_MTLCommandBuffer_blitCommandEncoderWithSampleBuffers` around line 4468

All of these currently call Objective-C Metal immediately on the calling Wine/native thread.

### D3D12 descriptor texture view creation

`vendor/dxmt/src/d3d12/d3d12_device.cpp`

- `CreateDescriptorTextureView(...)` around line 889 calls:
  - `resource->GetMTLTexture()`
  - `base.newTextureView(...)`
- This means SRV/UAV/RTV/DSV descriptor creation can synchronously call `_MTLTexture_newTextureView` on whichever game/loader thread is updating descriptors.

### D3D12 placed resources / heap path

`vendor/dxmt/src/d3d12/d3d12_heap.cpp`

- `MTLD3D12Heap` currently creates a backing `MTLBuffer` only for buffer-compatible heaps.

`vendor/dxmt/src/d3d12/d3d12_device.cpp`

- `CreatePlacedResource(...)` uses heap backing only when:
  - `desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER`
  - heap has a Metal buffer
- Placed textures currently become standalone `MTLD3D12Resource` textures, not texture views over a shared heap backing.

`vendor/dxmt/src/d3d12/d3d12_resource.cpp`

- Texture resources call `wmt_device.newTexture(tex_info)` immediately in `InitializeResource(...)`.
- `GetMTLTexture()` lazily creates a texture if missing, also immediately.

### Command buffer ownership / submission

`vendor/dxmt/src/d3d12/d3d12_command_queue.cpp`

- `AcquireMetalCommandBuffer(...)` obtains `m_wmt_queue.commandBuffer()`.
- `ExecuteCommandLists(...)` acquires one Metal command buffer per D3D12 command list, replays all commands into it, then commits/tracks it.
- Current design is queue-centric; it does not yet isolate CPU-thread-local command buffers for every producer thread.

## Working hypothesis

The restored Slice-3 runtime can boot AC6, render menus, and tolerate normal Metal activity. The Continue transition is different: AC6 starts real-world streaming, which combines:

1. bursty placed/aliased heap resource creation,
2. descriptor view churn (`newTextureView` / `newTexture` / `newTextureFromBuffer`),
3. PSO/DXIL lowering on `LoadProcess_M*` threads,
4. active render/encode work on `GXRenderThread` / `dxmt-encode-thr`, and
5. Metal command encoder creation/commit pressure.

The prior Phase 3C crash evidence fits this model:

- old present crash was fixed separately and is not the current target,
- later startup blank-window crash hit AGX unfair-lock recursion during `_MTLDevice_newTexture`, with neighboring threads in blit encoder creation,
- after broad AGX serialization on Phase 3C, failure moved to DXIL lowering, suggesting serialization changed progress/failure shape rather than solving the whole Continue path.

Therefore the clean strategy is not a broad Phase 3C reapply. It is to start from Slice-3 and add progressively narrower safety mechanisms around Metal resource/view allocation and command-buffer/encoder ownership.

## Non-goals

- Do not reapply Phase 3C wholesale.
- Do not enable the old broad AGX serialization patch as the first production fix.
- Do not launch AC6 without explicit approval.
- Do not use `/mtsp/prepare` or `PORT`; use `METALSHARP_PORT=9277` and `POST /steam/launch-game`.
- Do not introduce `mscompatdb`.

## Roadmap

### Slice A — Baseline capture and post-Continue repro index

Goal: preserve the exact known-good menu baseline and collect one controlled Continue failure capture before changing behavior.

Work:

1. Keep active runtime at exact Slice-3 hashes.
2. Add no code changes.
3. With explicit user approval, launch AC6 once and capture:
   - healthy menu snapshot,
   - user Continue moment timestamp,
   - bounded post-Continue `sample`, LLDB backtrace, process status, and log tails.
4. Write a compact failure index mapping:
   - fault thread,
   - top native frames,
   - active `LoadProcess_M*` states,
   - Metal allocation/encoder frames,
   - whether failure is AGX lockup, Wine page fault, or hang.

Acceptance:

- Menu still loads before Continue.
- Capture distinguishes user-kill from crash/hang.
- Continue failure is documented under the Slice-3 runbook result dir.

### Slice B — Instrumentation-only Metal pressure counters

Goal: prove the real-world loader pressure pattern without changing runtime behavior.

Work:

1. Add thread-aware counters/logging in `winemetal_unix.c` around:
   - `_MTLDevice_newTexture`,
   - `_MTLBuffer_newTexture`,
   - `_MTLTexture_newTextureView`,
   - `_MTLCommandBuffer_blitCommandEncoder`,
   - `_MTLCommandBuffer_computeCommandEncoder`,
   - `_MTLCommandBuffer_renderCommandEncoder`,
   - `_MTLCommandBuffer_commit`.
2. Log bounded fields only:
   - pthread id / Mach tid if available,
   - thread name,
   - call type,
   - dimensions/format for texture calls,
   - wait/duration in microseconds,
   - in-flight overlap counters.
3. Add env gate:
   - `DXMT_WINEMETAL_PRESSURE_TRACE=1`
   - default off.
4. Build, stage, preflight, and run one approved Continue capture.

Acceptance:

- No functional behavior changes with trace off.
- With trace on, menu still loads.
- Continue capture identifies whether `LoadProcess_M*` is concurrently performing `newTexture/newTextureView/newTextureFromBuffer` while render/encoder calls are active.

### Slice C — Minimal AGX allocation critical section

Goal: test the smallest viable serialization that targets Apple resident-memory-manager races without serializing all rendering.

Work:

1. Implement a scoped native mutex in `winemetal_unix.c` around only:
   - `_MTLDevice_newTexture`,
   - `_MTLBuffer_newTexture`,
   - `_MTLTexture_newTextureView`.
2. Wrap Objective-C calls with `@try/@finally` so the mutex cannot remain locked on exception.
3. Add runtime mode:
   - `DXMT_WINEMETAL_AGX_ALLOC_GUARD=0|1`
   - default initially off for probes; stage with explicit env enabled for AC6 test.
4. Preserve counters:
   - total guarded calls,
   - total wait time,
   - max wait time,
   - thread names observed.

Acceptance:

- Existing offline probes pass.
- Menu still loads with guard off and on.
- Post-Continue either advances further or fails with a different, clearly captured fault.
- If it fails, failure must not be a lock leak/deadlock from the guard.

### Slice D — Add encoder creation guard only if Slice C still shows AGX encoder collision

Goal: expand serialization only if instrumentation proves encoder allocation overlaps resource/view allocation at failure.

Work:

1. Guard only encoder creation calls, not command buffer commit/wait:
   - `_MTLCommandBuffer_blitCommandEncoder`,
   - `_MTLCommandBuffer_computeCommandEncoder`,
   - `_MTLCommandBuffer_renderCommandEncoder`,
   - `_MTLCommandBuffer_blitCommandEncoderWithSampleBuffers`.
2. Runtime mode:
   - `DXMT_WINEMETAL_AGX_ENCODER_GUARD=0|1`
3. Keep allocation and encoder guards independently toggleable.

Acceptance:

- No deadlocks.
- No regression to menu/startup.
- Continue failure shape improves or conclusively rules out encoder creation race.

### Slice E — Lazy D3D12 descriptor texture-view materialization

Goal: stop arbitrary worker threads from synchronously calling `newTextureView` during descriptor updates.

Work:

1. Extend `D3D12Descriptor` with a pending texture-view description:
   - base `ID3D12Resource*`,
   - format,
   - texture type,
   - mip range,
   - slice range,
   - swizzle,
   - generation/state flag.
2. Change `CreateDescriptorTextureView(...)` so it records pending metadata instead of immediately calling `base.newTextureView(...)` when deferral is enabled.
3. Materialize pending views only from command replay/binding paths before use:
   - graphics `ApplyRootBindings(...)`,
   - compute descriptor binding,
   - RTV/DSV render pass setup.
4. Protect materialization with the allocation guard from Slice C.
5. Runtime mode:
   - `DXMT_D3D12_DEFER_TEXTURE_VIEWS=0|1`
   - default off until validated.

Acceptance:

- Descriptor semantics preserved: same format/range/swizzle as immediate path.
- Menu still renders.
- Offline descriptor mutation probe covers deferred SRV/UAV/RTV/DSV paths.
- Continue no longer shows `LoadProcess_M*` directly allocating texture views during descriptor updates.

### Slice F — Texture resource allocation deferral for placed/streaming resources

Goal: defer costly texture allocation from loader threads when D3D12 resource creation does not immediately require a Metal texture handle.

Work:

1. Add optional lazy texture creation to `MTLD3D12Resource` for texture resources:
   - store `WMTTextureInfo`,
   - do not call `newTexture` in constructor when deferral is enabled,
   - create under guard in `GetMTLTexture()` on replay/render thread.
2. Start with default heap / placed texture resources only; do not defer swapchain/backbuffer or resources with immediate clear/copy requirements until covered.
3. Add counters for deferred vs immediate resource creation.
4. Runtime mode:
   - `DXMT_D3D12_DEFER_TEXTURE_ALLOCATION=0|1`

Acceptance:

- Menu still renders.
- Resource lifetime remains valid.
- Probe covers texture creation, descriptor binding, clear/copy, render target use.
- Continue capture shows texture allocation moving away from `LoadProcess_M*` spikes or at least serialized safely.

### Slice G — Per-thread command buffer staging / ordered queue submission

Goal: remove shared command-buffer/encoder collision risk by giving producer threads isolated Metal command buffers, then submitting in D3D12 queue order.

Work:

1. Introduce a per-CPU-thread command-buffer context for replay-time encoding.
2. Keep D3D12 ordering at `ExecuteCommandLists(...)` boundaries:
   - each command list gets an isolated command buffer,
   - command buffers are committed in the original `ExecuteCommandLists` order,
   - queue fence/completion tracking remains centralized.
3. Do not share a single live encoder across command lists or threads.
4. Runtime mode:
   - `DXMT_D3D12_THREAD_LOCAL_COMMAND_BUFFERS=0|1`

Acceptance:

- Existing queue/fence probes pass.
- Existing command replay probe passes.
- No command buffer lifetime regressions.
- Continue capture eliminates blit/render encoder collision as a failure mode.

### Slice H — Placed texture alias correctness / heap overlay model

Goal: improve D3D12 placed texture semantics after stabilization, not as the first crash fix.

Work:

1. Audit AC6 placed resources created after Continue:
   - heap flags,
   - texture dimensions/formats,
   - aliasing barriers,
   - reuse patterns.
2. Decide whether to implement real placed texture overlays using buffer-backed textures where legal, or a safer alias epoch model.
3. Ensure aliasing barriers invalidate/recreate affected views at controlled drain points.

Acceptance:

- Correctness validated by aliasing/UAV barrier probes.
- Does not reintroduce worker-thread `newTextureView` storms.

## Recommended next action

Do Slice A first: one controlled post-Continue repro from exact Slice-3, with the already-captured healthy menu snapshot as the comparison anchor. Then implement Slice B instrumentation-only. Only after the data confirms the overlap should we enable Slice C allocation serialization.

This keeps the path clean: evidence first, smallest guard second, descriptor deferral third, command-buffer architecture last.
