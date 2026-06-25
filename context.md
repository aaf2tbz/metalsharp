# Code Context

## Files Retrieved
1. `vendor/dxmt/src/dxmt/dxmt_command_queue.hpp` (lines 93-178, 205-287) - core DXMT command chunk ring, CPU/frame fences, public wait/latency APIs.
2. `vendor/dxmt/src/dxmt/dxmt_command_queue.cpp` (lines 36-222) - global DXMT queue creation, async encode/finish threads, command-buffer allocation/commit/completion backpressure.
3. `vendor/dxmt/src/d3d12/d3d12_command_queue.hpp` (lines 14-88) - D3D12 command queue fields: separate Metal queue, barrier event, inflight env limit.
4. `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp` (lines 7787-7806, 7901-7935, 9940-10170) - D3D12 queue constructor, ExecuteCommandLists commandBuffer path, commit/sync waits, Signal/Wait fence command buffers.
5. `vendor/dxmt/src/d3d12/d3d12_fence.hpp` (lines 14-44) and `vendor/dxmt/src/d3d12/d3d12_fence.cpp` (lines 13-152) - D3D12 fence backed by Metal shared event and CPU waits.
6. `vendor/dxmt/src/d3d12/d3d12_swapchain.hpp` (lines 114-147) and `vendor/dxmt/src/d3d12/d3d12_swapchain.cpp` (lines 61-78, 616-652, 1173-1470) - present queue, present inflight ring, Present1 CPU wait and present command-buffer lifecycle.
7. `vendor/dxmt/src/d3d12/d3d12_resource.hpp` (lines 90-106) - swapchain backbuffer work tracking used by present diagnostics.
8. `vendor/dxmt/src/winemetal/Metal.hpp` (lines 752-827), `vendor/dxmt/src/winemetal/winemetal_thunks.c` (lines 1050-1076), `vendor/dxmt/src/winemetal/unix/winemetal_unix.c` (lines 1333-1366, 4107-4110, 4405-4409) - PE-to-Unix command queue/buffer wrappers where the observed hang lands.
9. `vendor/dxmt/src/util/util_cpu_fence.hpp` (lines 6-30) - atomic CPU fence wait/signal implementation.

## Key Code

### Winemetal command-buffer allocation is a thin blocking call
```cpp
// vendor/dxmt/src/winemetal/Metal.hpp:752-757
class CommandQueue : public Object {
public:
  CommandBuffer commandBuffer() {
    return CommandBuffer{MTLCommandQueue_commandBuffer(handle)};
  }
};
```
```objc
// vendor/dxmt/src/winemetal/unix/winemetal_unix.c:1347-1350
params->ret = (obj_handle_t)[(id<MTLCommandQueue>)params->handle commandBuffer];
```
No local timeout/backpressure/logging exists here; if Metal queue `maxCommandBufferCount` is exhausted, the app thread can block inside `_MTLCommandQueue_commandBuffer` / `AGX... commandBuffer`.

### Global DXMT queue has explicit async chunk backpressure
- `CommandQueue` owns `commandQueue(device.newCommandQueue(kCommandChunkCount))` where `kCommandChunkCount = 32` (`dxmt_command_queue.hpp:91`, `dxmt_command_queue.cpp:36-41`).
- Producer `CommitCurrentChunk()` publishes to encoder, then waits on `chunk_ongoing` before increasing outstanding count (`dxmt_command_queue.cpp:94-114`):
```cpp
ready_for_encode.fetch_add(1, std::memory_order_release);
ready_for_encode.notify_one();
chunk_ongoing.wait(kCommandChunkCount - 1, std::memory_order_acquire);
chunk_ongoing.fetch_add(1, std::memory_order_relaxed);
```
- Encoder thread calls `CommitChunkInternal()`, which allocates `commandQueue.commandBuffer()`, encodes, commits, then notifies finish thread (`dxmt_command_queue.cpp:121-166`).
- Finish thread waits completed command buffers and then signals CPU fence, decrements `chunk_ongoing`, frees allocators (`dxmt_command_queue.cpp:168-222`).
- `PresentBoundary()` separately caps frame latency with `frame_latency_fence_.wait(frame_count - max_latency_)` (`dxmt_command_queue.hpp:247-260`).

Risk: this path has local backpressure, but `ready_for_encode` is advanced before `chunk_ongoing` is incremented; the encode thread can reach `commandBuffer()` before the producer finishes its outstanding-count accounting. Probably not the primary M12 queue hang, but worth noting.

### D3D12 queue has Metal queue max but no local inflight tracking
- Constructor creates a separate Metal queue with env-controlled max (`DXMT_D3D12_METAL_QUEUE_MAX_INFLIGHT`, default 32, clamped 1..64) (`d3d12_command_queue.cpp:144-159`, `7787-7799`):
```cpp
m_metal_queue_max_inflight = DXMTD3D12MetalQueueMaxInflight();
m_wmt_queue = wmt_dev.newCommandQueue(m_metal_queue_max_inflight);
```
- `ExecuteCommandLists()` allocates command buffer immediately per list (`d3d12_command_queue.cpp:7901-7919`), replays commands, commits at `9978-9981`, and only waits if diagnostic/sync envs are enabled (`9982-10001`).
- No ring of submitted D3D12 queue command buffers is tracked. Normal mode sets status to `Committed` without checking completion (`10002-10009`).
- `Signal()` and `Wait()` also allocate independent Metal command buffers from the same `m_wmt_queue` (`10126-10147`, `10151-10170`). These can also block in `commandBuffer()` if the D3D12 queue is saturated.

### D3D12 fences are shared-event backed
- `MTLD3D12Fence` creates and initializes a Metal shared event (`d3d12_fence.cpp:40-47`).
- `GetCompletedValue()` polls `m_shared_event.signaledValue()` (`95-105`).
- `SetEventOnCompletion()` may block indefinitely when `event == nullptr`, or spawns an unbounded waiter thread for non-null Win32 events (`108-145`).
- Queue `Signal/Wait` encode Metal event signal/wait packets, not CPU waits (`d3d12_command_queue.cpp:10126-10170`).

### Swapchain present path can block before its own inflight ring drains
- Swapchain present queue is created with max command buffers = 1 (`d3d12_swapchain.cpp:587-588`; recreated at `1193-1198`).
- Present inflight ring default limit is 2 (`DXMT_D3D12_PRESENT_INFLIGHT_LIMIT`, clamp 0..8) (`61-78`).
- `Present1()` calls `m_present_queue.commandBuffer()` before `TrackPresentCommandBuffer()` has a chance to wait/release old slots (`1173-1198`, commits/tracks at `1338-1343` and `1453-1458`). With queue max = 1, the second present can block inside `commandBuffer()` if the prior present buffer has not completed.
- `TrackPresentCommandBuffer()` waits on/replaces the ring slot after commit (`633-652`), but this is too late to prevent blocking in command-buffer allocation.
- `Present1()` can also block on `dxmt_queue.WaitCPUFence(present_wait_seq)` unless `DXMT_D3D12_LIVE_PRESENT` is enabled (`1243-1267`).

## Architecture

There are three relevant command-buffer producers:
1. **DXMT global queue** (`dxmt_command_queue.*`) used by older/common DXMT encoding. It has async encode/finish threads, a 32-chunk ring, CPU fence completion, allocator reclamation, and frame latency controls.
2. **M12/D3D12 queue** (`d3d12_command_queue.*`) used by `ID3D12CommandQueue::ExecuteCommandLists`, `Signal`, and `Wait`. It creates its own Metal command queue and relies almost entirely on Metal's `maxCommandBufferCount` for backpressure.
3. **M12 present queue** (`d3d12_swapchain.*`) used by DXGI Present. It creates a separate Metal queue with max=1, has a small post-commit inflight ring, and may wait for the global DXMT CPU fence before present.

All three ultimately call `WMT::CommandQueue::commandBuffer()` -> `MTLCommandQueue_commandBuffer()` -> Unix `_MTLCommandQueue_commandBuffer()` -> Objective-C `[queue commandBuffer]`. The wrappers do not add safeguards; any saturation or drawable/present stall appears as a block in winemetal.so/AGX commandBuffer.

## Minimal low-risk Phase 2 fix candidates

1. **Present queue pre-drain before `commandBuffer()`**: in `MTLD3D12SwapChain::Present1`, wait/release the next `m_present_inflight` slot before calling `m_present_queue.commandBuffer()`, or create `m_present_queue` with `maxCommandBufferCount = max(1, PresentInflightLimit())`. This directly targets a likely hang: present queue max=1 but current backpressure runs after commit.
2. **Add local D3D12 queue inflight ring** around `m_wmt_queue.commandBuffer()`/`commit()` in `ExecuteCommandLists`, `Signal`, and `Wait`: before allocating a new Metal command buffer, wait for the ring slot corresponding to `m_metal_queue_max_inflight` (or a smaller env default). This moves blocking to controlled code, enables logging, and avoids opaque AGX commandBuffer stalls.
3. **Lower and log `DXMT_D3D12_METAL_QUEUE_MAX_INFLIGHT` default experimentally**: default 32 may permit a large backlog with no completion visibility. A conservative env-driven default like 3-8 plus warnings when allocation would exceed local ring is low-risk if guarded by env first.
4. **Add timing/logging around all `commandBuffer()` calls**: especially `ExecuteCommandLists`, `Signal`, `Wait`, and `Present1`. If a call exceeds e.g. 100ms, log queue type, inflight counters, present count, last fence seq. This is diagnostic and safe.
5. **Avoid diagnostic sync/readback in hang repros**: `DXMT_D3D12_SYNC_EXECUTE`, swapchain readback, autopresent, final snapshot, AC6 producer diagnostic all force `cmdbuf.waitUntilCompleted()` after commit (`d3d12_command_queue.cpp:9982-9991`). Keep them off unless intentionally debugging.

## Start Here
Open `vendor/dxmt/src/d3d12/d3d12_swapchain.cpp` first. The present path creates a max-1 Metal queue and allocates a new command buffer before its inflight ring applies backpressure, matching the observed block site most closely.