# Code Context

## Files Retrieved
1. `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp` (lines 1-220) - M12 safety/env gates and queue configuration knobs.
2. `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp` (lines 2950-3210, 5080-5200, 7890-7985, 8076-8161, 9488-9918, 10160-10250) - replay state, argument-buffer residency tracking, command-buffer inflight tracking, ExecuteCommandLists, barriers, clear/load/store paths, commit/wait.
3. `vendor/dxmt/src/d3d12/d3d12_command_list.cpp` (lines 1-260, 368-404, 430-740) - D3D12 command recording for barriers, descriptor heaps, RTV/DSV/UAV ops, root descriptors/tables.
4. `vendor/dxmt/src/d3d12/d3d12_swapchain.cpp` (lines 1260-1540) - D3D12 Present paths, presenter vs raw blit, drawable acquisition, render wait behavior.
5. `vendor/dxmt/src/d3d12/d3d12_resource.cpp` (lines 1-240) - D3D12 resource allocation, texture/buffer Metal backing, synthetic texture GPU VAs.
6. `vendor/dxmt/src/d3d12/d3d12_descriptor_heap.cpp` (lines 1-124) - CPU/GPU descriptor handles are raw heap pointers; descriptor lifetime model.
7. `vendor/dxmt/src/d3d12/d3d12_heap.cpp` (lines 1-92) - placed heap backing buffer support and limits.
8. `vendor/dxmt/src/dxmt/dxmt_context.hpp` (lines 500-610) - generic dxmt useResource residency helpers.
9. `vendor/dxmt/src/dxmt/dxmt_presenter.cpp` (lines 150-240) - presenter render pass, useResource, drawable lifetime.
10. `vendor/dxmt/src/winemetal/unix/winemetal_unix.c` (lines 990-1035, 3068-3157) - native present execute blit, nextDrawable/texture/layer props bridge.
11. `vendor/dxmt/src/winemetal/Metal.hpp` (lines 610-745) - C++ wrappers for MetalDrawable/Layer/CommandBuffer present.
12. `vendor/dxmt/tests/*` and `tests/*d3d12*` (file listing only) - current offline harness inventory.

## Key Code

### Command queue / command-buffer lifetime
- `MTLD3D12CommandQueue` creates a WMT command queue with env-configured max inflight and a queue-order `m_barrier_event` (`d3d12_command_queue.cpp` lines 7890-7902).
- `WaitForMetalCommandBufferSlot` waits on reused completion slots and logs Metal command-buffer errors (`d3d12_command_queue.cpp` lines 7907-7960).
- `ExecuteCommandLists` creates one Metal command buffer per D3D12 list, replays the bytecode stream, closes render encoders, commits, and only waits synchronously under diagnostics/env gates (`DXMT_D3D12_SYNC_EXECUTE`, readback, final snapshot, AC6 producer diagnostic) (`d3d12_command_queue.cpp` lines 8076-8161, 10160-10222).

### Resource barriers / UAV / aliasing
- D3D12 command list simply records `ResourceBarrier` packets verbatim (`d3d12_command_list.cpp` lines 368-380).
- Replay handles transition barriers by updating a single resource-wide tracked state; BEGIN_ONLY is ignored for state update. UAV and aliasing barriers increment diagnostics and log, then **all barrier types** force `st.CloseRenderEncoder()` and enqueue a Metal signal/wait event if available (`d3d12_command_queue.cpp` lines 9488-9570).
- Gap: no per-subresource state model, no explicit resource alias lifetime/remap handling, no Metal heap/resource hazard transition beyond queue self signal/wait. UAV barriers are ordering barriers, not resource-specific texture/buffer memory fences.

### Argument buffers / useResource / residency
- General dxmt `ArgumentEncodingContext::makeResident` emits render/compute `useResource` when residency state changes per encoder (`dxmt_context.hpp` lines 523-575).
- M12 replay has separate safety: `DXMT_D3D12_GPU_HANG_SAFETY` enables `DXMT_D3D12_STRICT_ARG_BUFFER_RESOURCES`; compute argument-buffer resources are collected by handle+usage (`d3d12_command_queue.cpp` lines 162-179, 2994-3018).
- Compute arg buffer build writes GPU VAs / texture resource IDs for SRV/UAV and tracks descriptor resources only when strict mode is enabled (`d3d12_command_queue.cpp` lines 5080-5175). Render paths issue direct `render_enc.useResource` for UAV/SRV descriptors around lines shown by grep (not fully read: ~3764, ~4410, ~4765, ~5707, ~5730).
- Gap: strict compute arg-buffer useResource is env-gated, not default unless GPU hang safety is on; coverage of resources reachable only through argument buffers should be verified in offline harness. No observed `useHeap` coverage in inspected paths.

### RTV/DSV lifetime and load/store
- `OMSetRenderTargets` closes current encoder and stores raw descriptor handles for later render-pass creation (`d3d12_command_queue.cpp` lines 9571-9635).
- `ClearRenderTargetView` builds a temporary render pass: cleared RTV gets `Clear/Store`; other currently-bound RTVs and DSV are attached with `Load/Store` (`d3d12_command_queue.cpp` lines 9636-9737).
- `ClearDepthStencilView` similarly preserves bound RTVs with `Load/Store` and clears/loads depth/stencil according to flags (`d3d12_command_queue.cpp` lines 9738-9787).
- Gap: target descriptors are raw pointers into descriptor heaps; no AddRef on target resources at command-record time. If AC6 mutates/recycles descriptors between record and execute, replay sees current descriptor contents, not a snapshot.

### Resource allocation / heap aliasing
- Buffers are standalone Metal buffers or placed slices of a D3D12 heap backing buffer (`d3d12_resource.cpp` lines 71-128; `d3d12_heap.cpp` lines 14-36).
- Textures are always standalone Metal textures with synthetic D3D12 GPU VAs; old fake backing buffer was removed (`d3d12_resource.cpp` lines 130-189).
- Placed heaps only allocate a shared backing Metal buffer for buffer-only heaps; texture aliasing/placed textures are not a true Metal heap alias model in the inspected code.

### Present / drawable lifetime
- Present waits for the previous render queue seq unless `LivePresentEnabled()` skips the CPU wait (`d3d12_swapchain.cpp` lines 1292-1321).
- Presenter path acquires drawable inside `Presenter::encodeCommands`, encodes present quad render pass with `useResource(backbuffer/gamma_lut)`, then caller schedules `presentDrawable` and commits (`d3d12_swapchain.cpp` lines 1339-1388; `dxmt_presenter.cpp` lines 168-220).
- Raw path calls `m_layer.nextDrawable()`, optionally executes native M12 present blit or a WMT blit encoder, then presents drawable unless native path already did (`d3d12_swapchain.cpp` lines 1395-1504; `winemetal_unix.c` lines 990-1035).
- Bridge `MetalLayer_nextDrawable` and `MetalDrawable_texture` are thin casts/calls; layer props set device/format/size/display sync on main thread but not maximumDrawableCount or presentsWithTransaction (`winemetal_unix.c` lines 3068-3157; `Metal.hpp` lines 610-745).
- Gap: native present execute owns both blit and `presentDrawable`; fallback C++ owns present. Need ensure never double-present and that drawable object remains retained across unixcall/WMT wrapper boundaries.

## Architecture
D3D12 app calls are recorded by `MTLD3D12GraphicsCommandList` into a custom byte stream. `MTLD3D12CommandQueue::ExecuteCommandLists` replays that stream into WMT/Metal command encoders, using descriptor-heap pointer handles to resolve resources at replay time. Resource barriers mostly close encoders and order the Metal command buffer with a queue event; resource state is a coarse D3D12 state field on `MTLD3D12Resource`. Swapchain `Present` is outside normal ExecuteCommandLists replay: it acquires a new command buffer, optionally waits for render completion, obtains a CAMetalDrawable either via `Presenter` or raw layer path, encodes blit/quad, presents, commits, and advances the current backbuffer.

## Current safety coverage
- Inflight Metal command-buffer slot throttling and error logging.
- Queue-order event inserted for every D3D12 ResourceBarrier replay.
- Env-gated GPU hang safety/strict arg-buffer resource tracking and barrier diagnostics.
- Draw safety/vertex range safety defaults exist elsewhere in `d3d12_command_queue.cpp`.
- Present can force CPU wait unless live-present mode is enabled; present command buffers are tracked in swapchain slots.
- RTV/DSV clear paths preserve other bound attachments with Load/Store.

## Gaps / likely AC6 lockup suspects
1. **Descriptor snapshot/lifetime:** root tables, RTV/DSV/UAV descriptors are stored as raw heap pointers; command recording does not snapshot descriptor contents or retain resources.
2. **Subresource/aliasing barriers:** transition state is resource-wide; UAV/aliasing barriers are generic queue self-fences. No true aliasing model for texture heaps or per-subresource hazards.
3. **Argument buffer residency:** strict tracking is env-gated for compute arg buffers; render coverage is ad hoc direct useResource. Missing resources hidden in argument buffers can cause GPU page faults.
4. **Placed texture/heap model:** textures are standalone, synthetic GPU VA resources; D3D12 heap aliasing semantics for render/depth targets are not represented.
5. **Present lifetime:** two present ownership paths (native execute vs C++ fallback) and thin drawable bridge need harness validation for no double-present/use-after-drawable and no missing retain.
6. **Live present render wait:** if live mode skips CPU render wait, present may sample/copy backbuffer before producer command buffer completion unless Metal queue ordering is otherwise sufficient.

## High-priority offline harness targets
1. Descriptor mutation-after-record: record draw/dispatch with SRV/UAV/RTV descriptor A, mutate heap slot to B before ExecuteCommandLists, assert replay uses intended D3D12 semantics or intentionally documents current behavior.
2. Compute arg-buffer residency: descriptor-table SRV/UAV textures and buffers only referenced through argument buffers; run with strict safety off/on and assert `useResource` emission before dispatch.
3. UAV barrier stress: write UAV in compute, UAV barrier, read as SRV/RTV in graphics; texture and buffer variants; same command list and split command lists.
4. Aliasing barrier stress: placed resources sharing heap offsets, especially RT/depth/UAV transitions; include texture alias attempts to expose unsupported semantics.
5. Per-subresource transition test: different mips/array slices in different states, then render/read one slice while another is UAV-written.
6. RTV/DSV descriptor lifetime: OMSetRenderTargets then descriptor overwrite/free before execute; clear/draw must not silently target wrong Metal texture.
7. Present ownership harness: raw native present execute vs fallback blit vs presenter path; verify exactly one `presentDrawable`, drawable texture retained until commit, no present without completed producer when live-present enabled.
8. Depth target lifetime/load-store: clear depth + render color preserving DSV, then rebind as SRV/UAV-like read path; validate no stale/undefined depth attachment.

## Start Here
Open `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp` first. It contains the replay engine where command-list descriptors/resources become Metal encoders, barriers, useResource calls, and command-buffer commits; most AC6 GPU-fault hypotheses converge there.
