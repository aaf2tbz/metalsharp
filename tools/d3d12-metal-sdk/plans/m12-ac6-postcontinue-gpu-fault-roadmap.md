# M12 AC6 post-Continue GPU fault roadmap

Status: active implementation roadmap for offline-only work before the next approved AC6 launch. This does not authorize a game launch. After Slice-3, the AC6 post-Continue GPU fault is treated as a real correctness bug, not an optional diagnostic hypothesis.

Baseline return point: Slice-3 snapshot at `/Users/alexmondello/slice3baseline/latest`.

## Current evidence

AC6 now gets past the old Slice-2 blocker and reaches the post-Continue world/scene transition. The failure changed into severe graphical corruption, a solid magenta presentation frame, and a full-machine GPU/WindowServer/firmware deadlock. Read-only recovery evidence collected GPU diagnostic reports with these restart/fault classes:

- `BIF0 page fault`
- `MMU interrupt`
- `firmware-detected lockup`
- `CDM Kill timeout`

This points away from a simple PSO compile/load hang and toward a GPU-visible invalid memory/resource/lifetime/hazard problem in the first heavy 3D scene workload.

## External research conclusions

### Confirmed / high confidence

1. Metal argument-buffer resources need explicit declaration before execution.
   - Installed SDK evidence: `MTLRenderCommandEncoder.h` says `useResource:usage:` declares that a resource may be accessed through an argument buffer and "must be called before encoding any draw commands which may access the resource through an argument buffer".
   - `useHeap:` similarly "must be called before encoding any draw commands which may access the resources allocated from the heap through an argument buffer".
   - Compute encoder headers say the same for dispatches.

2. `useResource` is not a lifetime-retain mechanism.
   - Installed SDK evidence: render encoder comments explicitly say `useResource`/`useResources` do not retain resources; the user must retain resources until the command buffer has executed.
   - Therefore the fix must include both residency/use declarations and command-buffer-completion retention.

3. Heap hazard tracking defaults are dangerous for a translation layer.
   - Installed SDK evidence: `MTLHeap.h` says heap `MTLHazardTrackingModeDefault` is treated as `MTLHazardTrackingModeUntracked`.
   - Heap resources need explicit fences/events or a deliberate tracked/debug mode.

4. Command buffer error instrumentation exists and should be used in safety mode.
   - Installed SDK evidence: `MTLCommandBufferDescriptor.errorOptions = MTLCommandBufferErrorOptionEncoderExecutionStatus` can populate per-encoder execution status, including `Faulted`, `Affected`, and `Pending`.

5. Apple Silicon GPU MMU/firmware faults are credible symptoms of invalid/inaccessible GPU virtual memory.
   - Public Apple docs do not expose AGX firmware internals, but Asahi Linux’s M1 GPU writeup confirms the architecture has a GPU MMU and firmware-managed command/memory model. Treat GPU-event strings as low-level invalid access evidence, then correlate back to the last encoded Metal passes.

### Plausible but not proven

1. A solid magenta frame plausibly means WindowServer/compositor is sampling a dead/uninitialized/corrupt drawable or IOSurface-backed texture, but it is not a formal Apple diagnostic contract by itself.
2. The AC6 transition into the 3D world probably expands descriptor/heap/resource pressure dramatically, making missing residency, stale descriptors, missing barriers, and drawable lifetime bugs much more likely than they were in the menu.

### Rejected / do not rely on

`sudo sysctl wdt_enable=1` is not a validated safety mitigation. I found no Apple-facing evidence that it is a documented Apple Silicon GPU watchdog/page-fault guard. Do not put this in the launch recipe. If ever investigated, it should be a read-only `sysctl -a | grep -i wdt` fact-finding step only, not a standard mitigation.

## Local code conclusions

High-risk areas found in the current Slice-3 code:

1. Descriptor snapshot/lifetime gap.
   - `vendor/dxmt/src/d3d12/d3d12_command_list.cpp` records descriptor handles/pointers for root tables, RTV/DSV/UAV, and barriers.
   - `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp` resolves many descriptor/resource pointers at replay time.
   - Risk: if AC6 mutates or recycles descriptor heap entries between record and execute, replay may see the wrong resource, or fail to retain the originally intended one.

2. Argument-buffer residency is still incomplete as a structural guarantee.
   - Current safety work added compute strict tracking behind `DXMT_D3D12_GPU_HANG_SAFETY` / `DXMT_D3D12_STRICT_ARG_BUFFER_RESOURCES` in `d3d12_command_queue.cpp`.
   - Render paths have many direct `render_enc.useResource(...)` calls, but there is not yet one central invariant that every descriptor-backed resource reachable through graphics argument/root tables is declared and retained before draw.

3. `useHeap` coverage appears absent.
   - `rg useHeap vendor/dxmt/src` does not show active useHeap wrappers/calls.
   - Even if current D3D12 texture placed-resource support maps textures as standalone Metal textures, buffer heaps and any future heap-backed resources need conservative `useResource`/`useHeap`/retention coverage.

4. Barrier model is coarse.
   - `ResourceBarrier` packets are replayed in `d3d12_command_queue.cpp`; transition state is resource-wide, while AC6 likely uses subresource/mip/array transitions heavily.
   - UAV and aliasing barriers currently act mostly as diagnostics plus encoder-close/order events, not a full per-resource/per-subresource hazard model.

5. Present/drawable lifetime has two ownership paths.
   - Native M12 present path in `winemetal_unix.c` can acquire/present the drawable.
   - Fallback/presenter paths in `d3d12_swapchain.cpp` / `dxmt_presenter.cpp` also acquire/present.
   - Need offline proof of exactly one present, drawable retained until command-buffer commit/completion, and no present sampling of a producer backbuffer still being written.

## Working hypothesis for next phase

The most likely root class is not a shader compile stall. It is one or more of:

1. Missing `useResource`/resource retention for descriptor-backed graphics resources used through argument/root-table paths.
2. Stale descriptor handle/resource lifetime mismatch: command list records a descriptor pointer, AC6 mutates/reuses it before execution, and replay binds/retains the wrong texture/buffer.
3. Incomplete UAV/transition/aliasing barrier translation, especially copy-to-SRV and UAV-to-RT/SRV across queues or subresources during world load.
4. Drawable/backbuffer lifetime or present ordering issue during swapchain resize/recreation or first scene present.


## Current WIP progress update

Implemented after Slice-3 baseline:

- Command-buffer completion slots now retain D3D12 resources and WMT buffers/textures/drawables until Metal completion.
- D3D12 replay retains resources for graphics root/table/RTV/DSV state, compute root/table state, copy/resolve/write-immediate paths, barriers, clears, and transient argument/table buffers.
- RTV/DSV/UAV clear/attachment command packets now carry record-time descriptor snapshots and retained snapshot resources. Replay prefers snapshot resources for OMSetRenderTargets/ClearRTV/ClearDSV/ClearUAV, avoiding descriptor mutation retargeting before execute.
- Present now retains source backbuffer, source texture, drawable, and drawable texture until present command-buffer completion. Present no longer skips CPU producer wait in live-present mode.

Remaining before Slice 6 staging:

- Extend/run offline probe coverage for descriptor mutation, RTV/DSV/UAV clear snapshots, barrier transitions, and present stress.
- Build full runtime artifact set and PR backend.
- Stage runtime/backend only; do not launch AC6.

## Do-not-launch gate

No AC6 launch until all required items below are complete and recorded in an evidence bundle. Visual launch remains explicitly unsafe after the magenta/GPU firmware deadlock.

## Milestone M13.0 — safety instrumentation foundation

Goal: make the dangerous resource/lifetime/hazard class observable in offline probes while fixing correctness by default, not by optional safety toggles.

Required changes:

1. Keep diagnostics bounded, but make resource lifetime/residency/present ordering fixes default behavior. Optional env toggles may only add extra logging. Required instrumentation adds:
   - Metal command-buffer descriptors with retained references enabled where available.
   - `MTLCommandBufferErrorOptionEncoderExecutionStatus` where available.
   - Labels for command buffers, encoders, draw/dispatch/pass classes, swapchain present, descriptor stress, and barrier stress.
   - A bounded ring buffer of last N command buffers/encoders/resources/barriers dumped on command-buffer error or timeout.

2. Ensure the WMT/winemetal layer exposes needed command-buffer descriptor/error-options hooks if not already available.

3. Add non-invasive diagnostic counters:
   - resources retained per command buffer
   - descriptor-backed resources declared via `useResource`
   - missing/null descriptor resources by shader stage and descriptor type
   - RTV/DSV/UAV resources retained
   - UAV/aliasing/transition barrier counts by resource and subresource
   - present drawable acquire/present/commit/completion IDs

Validation:

- Build `d3d12`, `dxgi`, `winemetal`.
- Run existing offline `probe_present_windowed` with diagnostics enabled.
- Confirm no generated logs/results are staged.

## Milestone M13.1 — command-buffer completion retention quarantine

Goal: eliminate use-after-free/recycle of Metal resources while GPU work is in flight.

Required changes:

1. Add a command-buffer in-flight resource retention bucket for M12 D3D12 replay. **Implemented in current WIP** via completion slots retaining D3D12 resources plus WMT buffers/textures until Metal completion.
2. Whenever replay resolves a descriptor/resource for a draw/dispatch/copy/clear/render-target/depth-target/present, retain the relevant D3D12 resource and/or WMT resource until Metal command-buffer completion. **Implemented in current WIP** for draw/dispatch/copy/clear/barrier/RTV/DSV/present paths.
3. Treat `useResource` as declaration only, not retention.
4. Add debug assertions/counters for resources used but not retained in safety mode.

High-priority retained classes:

- graphics SRV/CBV/UAV descriptor table resources
- compute SRV/CBV/UAV descriptor table resources
- root descriptor resources
- RTV/DSV attachment resources
- clear/copy source/destination resources
- swapchain backbuffers and drawable/present resources
- temporary argument-buffer/table buffers

Offline harness additions:

- Descriptor mutation-after-record for SRV/UAV/RTV/DSV.
- Destroy/release-after-record-before-execute probes where legal in D3D12 behavior.
- Verify command buffer completion releases retained buckets.

## Milestone M13.2 — graphics argument-buffer residency blanket

Goal: make graphics residency as conservative as compute strict mode.

Required changes:

1. Generalize the current compute `ArgumentBufferResourceUse` tracking into a render+compute helper.
2. For every graphics draw path, collect all resources reachable from:
   - vertex/pixel/geometry root tables
   - CBV/SRV/UAV tables
   - root CBV/SRV/UAV descriptors
   - indirect draw argument buffers
   - index/vertex buffers
3. Before each draw or before finalizing the encoder for that draw, call `useResource` with minimal correct usage/stages for all collected resources.
4. Fail closed in correctness-sensitive paths when descriptor metadata cannot be resolved; log and neutralize/skip rather than submitting an invalid GPU pointer. Extra verbosity may remain env-gated.
5. Do not use `useHeap` as the first fix for color attachments unless needed; Apple warns it can decompress all color attachments. Prefer per-resource declarations first.

Offline harness additions:

- Graphics descriptor table SRV texture stress.
- Graphics descriptor table UAV texture/buffer stress.
- Mixed VS/PS descriptor table stress.
- Null/missing descriptor safety-mode draw neutralization test.
- AC6-corpus-inspired PSO + descriptor pressure without launching AC6.

## Milestone M13.3 — RTV/DSV/UAV descriptor snapshot and attachment safety

Goal: remove the most dangerous stale descriptor path for render/depth targets.

Required changes:

1. Snapshot descriptor contents, not only descriptor handles, for:
   - `OMSetRenderTargets`
   - `ClearRenderTargetView`
   - `ClearDepthStencilView`
   - UAV clear commands
2. Retain resources referenced by those snapshots until command-buffer completion.
3. Track attachment identity and texture handle through render-pass creation, clear, draw, store, present.
4. Use command-record descriptor snapshots for RTV/DSV/UAV attachment/clear operations so descriptor mutation before execute cannot silently retarget critical attachments. **Implemented in current WIP** for OMSetRenderTargets, ClearRTV, ClearDSV, and ClearUAV resource selection/retention.

Offline harness additions:

- `OMSetRenderTargets`, then overwrite RTV descriptor before execute.
- `OMSetRenderTargets`, then overwrite DSV descriptor before execute.
- Clear RTV/DSV with currently-bound unrelated RTVs preserved via Load/Store.
- UAV descriptor overwrite before execute.

## Milestone M13.4 — barrier and queue safety expansion

Goal: conservatively serialize and validate AC6-style world-load transitions before trying another game launch.

Required changes:

1. Add per-subresource tracking at least for diagnostics:
   - resource pointer
   - subresource/mip/array range
   - before/after D3D12 states
   - command-list sequence
2. Strengthen UAV and aliasing barriers by default:
   - close current encoder
   - insert explicit Metal event/fence ordering
   - associate barrier with affected resources where possible
   - retain referenced resources across barrier command-buffer completion; consider forcing command-buffer boundaries for UAV/aliasing if offline probes still expose hazards
3. Add copy-to-SRV and copy-to-render-target stress paths.
4. Add an option to serialize graphics/compute/copy queues in safety mode before the next live run.

Offline harness additions:

- Compute UAV write -> UAV barrier -> graphics SRV read.
- Copy texture mips -> transition to pixel shader resource -> draw sample.
- Split command-list and split queue variants.
- Aliasing barrier stress with placed buffers; document unsupported texture aliasing if current implementation cannot model it.

## Milestone M13.5 — present/drawable lifetime and resize cage

Goal: prove magenta-frame paths are not caused by dead drawable/backbuffer lifetime or present ordering.

Required changes:

1. Label and count every drawable acquisition, texture extraction, render/blit encode, present call, commit, and completion.
2. Ensure exactly one owner presents each drawable.
3. Ensure drawable/backbuffer objects survive until present command buffer completion.
4. Force present to wait/order after producer render command buffer completion unless proven ordered by same Metal queue. **Implemented in current WIP** by removing live-present CPU-wait skip and retaining source/drawable objects until present completion.
5. Add resize/recreate cage: defer swapchain/backbuffer destruction until all in-flight command buffers that reference old backbuffers have completed.

Offline harness additions:

- Windowed present stress with resize/recreate during in-flight rendering.
- Native present path vs fallback path one-present assertion.
- Backbuffer producer->present ordering stress with live-present enabled/disabled.

## Milestone M13.6 — pre-launch evidence bundle

Required before asking for the next AC6 launch approval:

1. Fresh build of runtime from current tree.
2. Offline probe pass with the new default correctness fixes, plus bounded diagnostics enabled where useful:
   - descriptor mutation-after-record suite
   - graphics descriptor SRV/UAV suite
   - compute descriptor suite
   - RTV/DSV/UAV descriptor snapshot suite
   - barrier/aliasing/subresource suite
   - present/drawable/resize suite
3. `git diff --check` and compile checks.
4. Snapshot updated runtime as a temporary candidate, not replacing Slice-3 until a live run proves it improves safety.
5. A written launch preflight that includes:
   - current commit
   - runtime hashes
   - `mscompatdb` absence
   - exact env toggles
   - risk note that AC6 may still hard-lock the machine

## Next implementation order

1. Implement M13.1 resource-retention buckets first. This is foundational because `useResource` does not retain.
2. Implement M13.2 graphics residency blanket next. This directly addresses the strongest Metal API evidence.
3. Implement M13.3 descriptor snapshots for RTV/DSV/UAV. This addresses the strongest local code hazard.
4. Implement M13.4 conservative barrier/queue safety. This addresses world-load copy/UAV/transition hazards.
5. Implement M13.5 present/drawable cage. This addresses the magenta-frame symptom and reduces compositor-facing risk.
6. Only then stage a candidate runtime and ask whether one controlled AC6 launch is worth the residual risk.

## Source anchors

External / SDK:

- `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/Metal.framework/Headers/MTLRenderCommandEncoder.h`
- `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/Metal.framework/Headers/MTLComputeCommandEncoder.h`
- `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/Metal.framework/Headers/MTLCommandBuffer.h`
- `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/Metal.framework/Headers/MTLHeap.h`
- `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/Metal.framework/Headers/MTLTexture.h`
- Asahi Linux M1 GPU background: `https://asahilinux.org/2022/11/tales-of-the-m1-gpu/`

Local code:

- `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp`
- `vendor/dxmt/src/d3d12/d3d12_command_list.cpp`
- `vendor/dxmt/src/d3d12/d3d12_device.cpp`
- `vendor/dxmt/src/d3d12/d3d12_resource.cpp`
- `vendor/dxmt/src/dxmt/dxmt_context.hpp`
- `vendor/dxmt/src/d3d12/d3d12_swapchain.cpp`
- `vendor/dxmt/src/dxmt/dxmt_presenter.cpp`
- `vendor/dxmt/src/winemetal/unix/winemetal_unix.c`
- `vendor/dxmt/src/winemetal/Metal.hpp`

## Final Slice 1-6 staging evidence (2026-06-19)

Completed for the active offline/staging goal; this is **not** launch approval.

- Full DXMT/M12 build: `meson compile -C vendor/dxmt/build-metalsharp-x64` passed; final `d3d12` rebuild passed.
- Probe bundle rebuild: `tools/d3d12-metal-sdk/scripts/build-probes.sh` passed.
- Focused windowed present/descriptor mutation probe: `tools/d3d12-metal-sdk/results/m12-slices1-5-windowed-rerun-20260619-025916/probe-present-windowed-metalsharp.json` reports `pass: true`, 60/60 present stress frames, 16/16 descriptor compute frames, and RTV/DSV/UAV descriptor mutation checks true.
- Offline D3D12 suite: `tools/d3d12-metal-sdk/results/m12-slices1-5-offline-suite-20260619-025544/summary.json` reports rc=0 for graphics PSO, compute PSO, command replay, barriers/render-pass, resource views/formats, and heap aliasing.
- Runtime/game staging: `tools/d3d12-metal-sdk/results/m12-slices1-5-final-hash-sync-20260619-030220/hash-sync-and-mscompatdb.txt` shows build/runtime/game hashes match for the full Windows DLL set, build/runtime hashes match for Unix sidecars, and `mscompatdb` search is empty.
- Runtime layout preflight: `tools/d3d12-metal-sdk/results/m12-slices1-5-preflight-20260619-025806/runtime-preflight-metalsharp.json` reports `ok: true`.
- PR backend: `app/src-rust/target/release/metalsharp-backend` built with SHA-256 `6fdface8f06740091d0047669ae6abf91ddd8088606ec2ad458ba978b04a7c0b`; backend is listening on `127.0.0.1:9277` with `METALSHARP_PORT=9277` and `/status` reports `ok: true`.

Stop point: AC6 launch intentionally not performed; user is required for launch and visual confirmation.

