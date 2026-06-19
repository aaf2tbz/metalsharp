# M12 Final Failure Roadmap: D3D12 Metal Command-Buffer Backpressure

Date: 2026-06-18
Branch: `fix/m12-shader-probe-lab`
Primary repro: AC6 world-load hang, no logs/no trace, post-hang LLDB capture

## Executive summary

The real-scene hang is no longer a shader compile/cache problem. Post-hang LLDB caught `GXRenderThread` blocked in Metal while trying to allocate a command buffer:

```text
GXRenderThread
  semaphore_wait_trap
  _dispatch_semaphore_wait_slow
  Metal -[_MTLCommandBuffer initWithQueue:retainedReferences:synchronousDebugMode:]
  IOGPU -[IOGPUMetalCommandBuffer initWithQueue:retainedReferences:synchronousDebugMode:]
  AGXMetalG16G_B0 -[AGXG16GFamilyCommandQueue commandBuffer]
  winemetal.so _MTLCommandQueue_commandBuffer
```

Code inspection found the highest-confidence cause:

```cpp
// vendor/dxmt/src/d3d12/d3d12_command_queue.cpp
m_wmt_queue = wmt_dev.newCommandQueue(1);
```

The D3D12 command queue is limiting Metal to **one in-flight command buffer**. Real scenes submit command-list work where the next `commandBuffer()` call blocks until the prior buffer completes. Menus survive; real-world rendering stalls/hangs.

The fix path is to replace the single-buffer D3D12 Metal queue with a bounded multi-buffer queue plus completion/backpressure accounting and no-log validation. Per current validation preference, start at **16** and only try **32** if 16 still blocks or only delays the hang.

## Evidence to preserve

- Debugger capture dir:
  - `tools/d3d12-metal-sdk/results/ac6-m12-nolog-debugger-ready-windowed-20260618-164900/debugger-capture`
- Key files:
  - `lldb-nointerrupt.txt`
  - `lldb-nointerrupt-extract.md`
  - `sample.txt`
  - `summary.md`
- Current killed game PID became zombie under backend:
  - PID `13081`, parent backend PID `88672`, state `<defunct>`

## Constraints

- Keep live validation no-log/no-trace by default.
- Do not delete shader caches.
- Do not kill Steam/MetalSharp/backend unless explicitly approved.
- PE fallback remains mandatory.
- Native/M12Core gates remain default-off unless explicitly enabled.
- Avoid heavy D3D12 tracing for the final repro.

## Slice 1 — Minimal high-confidence unblock

Goal: remove the accidental single-command-buffer bottleneck.

1. Change D3D12 queue creation from max in-flight `1` to a bounded multi-buffer value.
   - First validation default: `16`.
   - Second-step ceiling if 16 still blocks: `32`, matching the existing `dxmt::CommandQueue` chunk-ring capacity.
   - Add env override for experiments:
     - `DXMT_D3D12_METAL_QUEUE_MAX_INFLIGHT`
     - default `16`
     - clamp range, e.g. `1..64`.
2. Apply only to `MTLD3D12CommandQueue::MTLD3D12CommandQueue` initially.
3. Add a tiny unit/smoke probe or static script check proving the default is not `1`.
4. Build/stage runtime.
5. Validate AC6 no-log/no-trace world load.

Acceptance:
- AC6 no longer blocks immediately in `MTLCommandQueue commandBuffer` during world load.
- If it still hangs, post-hang LLDB should show either a later max-in-flight block or a real GPU/encoder fault elsewhere.

## Slice 2 — Add no-log debugger-visible queue counters

Goal: preserve no-log behavior while making post-hang LLDB captures self-explaining.

Add cheap atomics/counters, no file I/O:

- per D3D12 command queue:
  - queue type
  - configured max in-flight
  - command-buffer create attempts
  - command-buffer create returns
  - commits
  - synchronous waits
  - last command-buffer handle
  - last queue serial
  - last command-list stats summary
- global symbol or static struct readable from LLDB.

No default logging. If an env gate is useful, keep it metadata-only and off by default.

Acceptance:
- Post-hang LLDB can show whether we are blocked before/after command-buffer return.
- Counters add negligible overhead and no log/trace output.

## Slice 3 — Completion/backpressure accounting

Goal: make multi-buffer behavior safe and diagnosable instead of simply raising the limit.

1. Track submitted vs retired command buffers in `MTLD3D12CommandQueue`.
2. Retire completed command buffers opportunistically before each new `commandBuffer()` call.
3. Keep references to recent command buffers until their status is terminal if needed for status/error inspection.
4. If in-flight reaches configured max:
   - prefer waiting on oldest tracked command buffer outside Metal command-buffer allocation;
   - record status/error/breadcrumb data in debugger-visible counters;
   - avoid unbounded silent blocking inside `commandBuffer()` when possible.
5. Cover all direct creation sites:
   - `ExecuteCommandLists`
   - `Signal`
   - `Wait`
   - swapchain present queue paths if they also use low max counts.

Acceptance:
- If GPU work stalls, the stall site moves to our explicit throttle/wait point with visible queue state.
- Normal scenes do not flood Metal queues.

## Slice 4 — Real correctness audit if multi-buffer only delays the hang

If AC6/Elden still hang after Slice 1/3, the prior command buffer is likely not completing due invalid GPU work or a fence/event dependency.

Audit in this order:

1. Fence/event command buffers
   - `MTLD3D12CommandQueue::Signal`
   - `MTLD3D12CommandQueue::Wait`
   - ensure waits cannot create a permanent queue dependency cycle.
2. Render encoder lifetime
   - every `EnsureRenderEncoder()` path has a matching `CloseRenderEncoder()` before commit.
   - no encoder remains open across command buffers.
3. Draw/index/resource safety
   - revisit the prior clue: `Resource: Map FAILED - no cpu_addr` near vertex-pull/draw setup.
   - focus files:
     - `vendor/dxmt/src/d3d12/d3d12_resource.cpp`
     - `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp`
     - `vendor/dxmt/src/d3d12/d3d12_vertex_input.hpp`
4. Descriptor/root binding completeness
   - ensure argument buffers do not bind invalid/uninitialized resources in real-world indexed draws.
5. Command buffer error visibility
   - if a command buffer reaches `Error`, capture status/error/logs via counters and optional explicit debug gate.

Acceptance:
- Identify first non-completing command buffer class: fence wait, render pass, indexed draw, compute/dispatch, clear/copy, present.

## Slice 5 — Validation matrix

Use no-log/no-trace launches unless explicitly validating diagnostic gates.

1. AC6 windowed world-load validation.
2. Elden Type A/B / character creator validation.
3. Menu regressions:
   - AC6 menu/title render
   - Elden menu/title render
4. Runtime probes:
   - D3D12 command replay probe
   - graphics PSO probe
   - barriers/render-pass probe
5. Cache preservation:
   - do not wipe `.msl`/`.metallib` caches.

Acceptance:
- Real-scene load progresses past the previous hang.
- No default log/trace output returns.
- Shader compile/cache path remains quiet; no `newLibraryWithSource` blocker.

## First patch target

Start with this exact line:

```cpp
// vendor/dxmt/src/d3d12/d3d12_command_queue.cpp
m_wmt_queue = wmt_dev.newCommandQueue(1);
```

Replace with an env-configurable bounded default of `16`:

```cpp
m_wmt_queue = wmt_dev.newCommandQueue(DXMTD3D12MetalQueueMaxInflight());
```

Then rebuild/stage and run AC6 no-log/no-trace. If 16 still blocks in the same `commandBuffer()` stack, retry with `DXMT_D3D12_METAL_QUEUE_MAX_INFLIGHT=32` before moving to Slice 3/4.

## Slice 1 progress — 2026-06-18

Implemented first-pass queue backpressure fix with the requested starting point:

- Added `DXMT_D3D12_METAL_QUEUE_MAX_INFLIGHT`.
- Default is `16`.
- Clamp is `1..64`.
- `MTLD3D12CommandQueue` now calls:
  ```cpp
  m_wmt_queue = wmt_dev.newCommandQueue(max_inflight);
  ```
- Rebuilt DXMT runtime via:
  ```bash
  ./tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime
  ```
- Staged runtime via:
  ```bash
  ./tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime
  ```
- Built/staged `d3d12.dll` SHA256:
  ```text
  5b69ef127de35054a430f61ceaf1c76f3f68e537bf4a6389406208e411a5e39f
  ```
- Stage manifest:
  ```text
  tools/d3d12-metal-sdk/results/stage-runtime-metalsharp.json
  ```

Next validation step: launch AC6 no-log/no-trace with default max-in-flight 16. If it still blocks in the same `MTLCommandQueue commandBuffer` stack, retry with `DXMT_D3D12_METAL_QUEUE_MAX_INFLIGHT=32` before moving to explicit completion/backpressure accounting.


## Queue16 validation — 2026-06-18

Validation run:

- Run dir:
  ```text
  tools/d3d12-metal-sdk/results/ac6-m12-queue16-nolog-20260618-170545
  ```
- PID:
  ```text
  27908
  ```
- Staged d3d12 SHA256:
  ```text
  5b69ef127de35054a430f61ceaf1c76f3f68e537bf4a6389406208e411a5e39f
  ```
- Capture dir:
  ```text
  tools/d3d12-metal-sdk/results/ac6-m12-queue16-nolog-20260618-170545/debugger-capture
  ```
- Corrected LLDB capture:
  ```text
  tools/d3d12-metal-sdk/results/ac6-m12-queue16-nolog-20260618-170545/debugger-capture/lldb-nointerrupt.txt
  tools/d3d12-metal-sdk/results/ac6-m12-queue16-nolog-20260618-170545/debugger-capture/lldb-nointerrupt-extract.md
  ```

Result: **hang persists at max-in-flight default 16**.

Key stack remains the same:

```text
GXRenderThread
  semaphore_wait_trap
  _dispatch_semaphore_wait_slow
  Metal -[_MTLCommandBuffer initWithQueue:retainedReferences:synchronousDebugMode:]
  IOGPU -[IOGPUMetalCommandBuffer initWithQueue:retainedReferences:synchronousDebugMode:]
  AGXMetalG16G_B0 -[AGXG16GFamilyCommandBuffer initWithQueue:retainedReferences:]
  AGXMetalG16G_B0 -[AGXG16GFamilyCommandQueue commandBuffer]
  winemetal.so _MTLCommandQueue_commandBuffer(obj=0x000000000fba1620) at winemetal_unix.c:1349:31
```

Interpretation: raising D3D12 queue max-in-flight from 1 to 16 did not eliminate the block. Per plan/preference, next retry is max-in-flight 32. If 32 also blocks in the same stack, proceed to Slice 3/4: explicit command-buffer retirement/accounting and first non-completing command-buffer correctness audit.

## Queue32 validation — 2026-06-18

Validation run:

- Run dir:
  ```text
  tools/d3d12-metal-sdk/results/ac6-m12-queue32-nolog-20260618-171419
  ```
- PID:
  ```text
  35570
  ```
- Staged d3d12 SHA256:
  ```text
  5c72578efa1987aef659f152f82799afbacb50e3717cb6cb9e4f60a8013c4bfd
  ```
- Capture dir:
  ```text
  tools/d3d12-metal-sdk/results/ac6-m12-queue32-nolog-20260618-171419/debugger-capture
  ```
- Corrected LLDB capture:
  ```text
  tools/d3d12-metal-sdk/results/ac6-m12-queue32-nolog-20260618-171419/debugger-capture/lldb-nointerrupt.txt
  tools/d3d12-metal-sdk/results/ac6-m12-queue32-nolog-20260618-171419/debugger-capture/lldb-nointerrupt-extract.md
  ```

Result: **hang persists at max-in-flight default 32**.

Key stack remains the same:

```text
GXRenderThread
  semaphore_wait_trap
  _dispatch_semaphore_wait_slow
  Metal -[_MTLCommandBuffer initWithQueue:retainedReferences:synchronousDebugMode:]
  IOGPU -[IOGPUMetalCommandBuffer initWithQueue:retainedReferences:synchronousDebugMode:]
  AGXMetalG16G_B0 -[AGXG16GFamilyCommandBuffer initWithQueue:retainedReferences:]
  AGXMetalG16G_B0 -[AGXG16GFamilyCommandQueue commandBuffer]
  winemetal.so _MTLCommandQueue_commandBuffer(obj=0x000000000fbae8c0) at winemetal_unix.c:1349:31
```

Interpretation: 16 and 32 both block in the same Metal command-buffer allocation stack. This is no longer just a too-low queue-depth issue. The next roadmap step is Slice 3/4: add explicit command-buffer submitted/retired accounting around D3D12 queue work and identify the first command buffer that stops completing, then audit fence/event waits, encoder lifetime, resource/draw correctness, and invalid GPU workload.

## Slice 3 instrumentation implementation — 2026-06-18

Implemented explicit D3D12 queue command-buffer tracking, with no default log/trace output:

- Added `MTLD3D12CommandQueue::CreateTrackedCommandBuffer`.
- Added `MTLD3D12CommandQueue::TrackSubmittedCommandBuffer`.
- Added `MTLD3D12CommandQueue::RetireCompletedCommandBuffers`.
- Added `MTLD3D12CommandQueue::WaitForOldestTrackedCommandBuffer`.
- Direct D3D12 Metal command-buffer sites now use the tracked path:
  - `ExecuteCommandLists`
  - `Signal`
  - `Wait`
- Added LLDB-readable global counters:
  ```text
  g_dxmt_d3d12_queue_debug_counters
  ```
- Counter fields include:
  - create attempts/returns
  - submitted/retired counts
  - explicit oldest waits/returns
  - current/max in-flight
  - last queue/command-buffer handles
  - last site/serial/status
  - wait site/serial/command-buffer/status-before/status-after
- Max-in-flight default currently remains 32 for the next validation run.
- Rebuilt and staged runtime.
- Built/staged `d3d12.dll` SHA256:
  ```text
  0a18f5eceb67d9ef19b5692f209f07786996216af9ff7763e0fdc8397f4b97d0
  ```
- Offline preflight passed:
  - contracts
  - pipeline contract
  - runtime layout
  - shader-engine contract

Next validation: launch AC6 no-log/no-trace. If it hangs, capture LLDB and inspect both the stack and `g_dxmt_d3d12_queue_debug_counters` to determine whether the process is blocked inside:

1. our explicit oldest-command-buffer wait, meaning a submitted command buffer stopped completing; or
2. Metal `commandBuffer()` before our explicit throttle, meaning another queue/site is untracked or Metal is blocking before our tracked in-flight limit.

## Slice 3 tracker hardening — 2026-06-18

The first tracker build launched AC6 to an immediate zombie/no-window state. Focused probes exposed a Wine/MinGW teardown regression:

```text
std::__condvar::~__condvar(): Assertion '__e != 16' failed
```

Hardening change:

- Removed `std::mutex`/C++ container ownership from `MTLD3D12CommandQueue` tracking state.
- Replaced it with:
  - `std::atomic_flag` spinlock
  - fixed POD arrays of retained `obj_handle_t` command-buffer handles
  - explicit `NSObject_retain` / `NSObject_release`
- Rebuilt/staged runtime.
- New built/staged `d3d12.dll` SHA256:
  ```text
  1fd106f26d4907404074b4b755b1567ce8fdb577a4525f50105c877d66bb1d63
  ```
- Focused non-visual queue probe passed cleanly:
  ```bash
  tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp ... --no-mini
  ```
- Probe output:
  ```text
  tools/d3d12-metal-sdk/results/probe-queues-metalsharp.json
  ```

Next validation: relaunch AC6 no-log/no-trace with the raw-handle tracker. If it hangs, capture LLDB and inspect `g_dxmt_d3d12_queue_debug_counters`.

## Slice 3 rollback and readiness reset — 2026-06-18

The first two Slice 3 tracker builds are **superseded** and must not be used as
the starting point for another game launch.

What happened:

1. The initial C++ tracker used `std::mutex` and C++ container/reference state in
   `MTLD3D12CommandQueue`.
2. Non-visual probes exposed MinGW/Wine teardown assertions:
   ```text
   std::__condvar::~__condvar(): Assertion '__e != 16' failed
   ```
3. The raw-handle tracker removed those C++ queue-tracker objects and passed a
   focused queue probe, but AC6 still instant-zombied/no-windowed launch.
4. A broader offline probe pass still showed repeated `std::__condvar` teardown
   assertions from the existing async pipeline compiler path, so probe exit code
   alone is not a sufficient readiness signal.
5. The Slice 3 tracker/counter code was removed completely. The staged runtime is
   back to the proven Slice 2 surface: queue-depth only, default 32.

Current Slice 2 source state:

- `DXMT_D3D12_METAL_QUEUE_MAX_INFLIGHT` exists.
- Default is `32`.
- Clamp is `1..64`.
- `MTLD3D12CommandQueue` calls:
  ```cpp
  m_wmt_queue = wmt_dev.newCommandQueue(m_metal_queue_max_inflight);
  ```
- No `g_dxmt_d3d12_queue_debug_counters` symbol is present.
- No tracked command-buffer helpers are present.

Readiness evidence for the reset runtime:

- Rebuilt/staged/game-local AC6 `d3d12.dll` SHA256:
  ```text
  aec964aa63bdede2828f915f58591b406a810cb2a70e182ebba223e04087cbbb
  ```
- Dry-run evidence:
  ```text
  tools/d3d12-metal-sdk/results/slice2-readiness-dryrun-20260618-180137/dry-run-armored-core-vi-1888160.json
  ```
- Dry-run result:
  ```text
  ok=true
  pipeline=m12
  missing=[]
  ```
- Runtime layout/preflight result:
  ```text
  tools/d3d12-metal-sdk/results/slice2-readiness-dryrun-20260618-180137/runtime-preflight-armored-core-vi.json
  ok=true
  failure_count=0
  ```
- PR backend restarted on `127.0.0.1:9277` before validation.
- AC6 Slice 2 launch used the rollback hash and did not instant-zombie:
  ```text
  tools/d3d12-metal-sdk/results/ac6-m12-slice2-rollback-nolog-20260618-180454
  pid=78644
  ```

## Revised Slice 3 plan — do not implement until all preconditions are met

Slice 3 must be redesigned as a staged offline-first diagnostic, not as a broad
runtime tracker inserted directly into AC6. The goal remains to identify the
first non-completing Metal command buffer, but the implementation must not
introduce process-lifetime, teardown, allocation, or logging regressions.

### Non-negotiable constraints

- No AC6/Elden launch until all offline gates below are green and the user gives
  explicit approval.
- No default logs or traces.
- No shader-cache deletion.
- No generated `.metallib`, `.air`, DXBC blobs, raw D3DMetal payloads, or result
  corpora in commits.
- PE fallback and M12Core gates remain preserved.
- Do not use `std::condition_variable` in new Slice 3 code.
- Do not add new static/global C++ objects with non-trivial destructors.
- Do not add `std::mutex`, `std::array<WMT::Reference<...>>`, `std::vector`, or
  other C++ ownership containers to command-queue lifetime-sensitive state.
- Do not hold locks while calling Metal/Objective-C methods, `waitUntilCompleted`,
  `status`, `error`, `commit`, `commandBuffer`, `retain`, or `release`.
- Do not add a global counter struct containing `std::atomic` fields if that
  struct has non-trivial static initialization/destruction risk. Prefer plain
  C-compatible storage or function-local explicitly initialized POD.

### Slice 3 objective

Determine whether the hang is caused by:

1. Metal queue allocation backpressure before a command buffer is returned;
2. a specific submitted command buffer that never reaches a terminal state;
3. a fence/event dependency cycle involving `Signal`/`Wait` command buffers;
4. invalid GPU work in render/compute command buffers; or
5. another untracked Metal command queue/site outside `MTLD3D12CommandQueue`.

### Design principle

Implement this as a **compile-time small C/POD diagnostic layer** with explicit
opt-in runtime behavior:

- default runtime behavior must be identical to Slice 2 except cheap POD counter
  writes;
- no ownership retention in the first sub-slice;
- no explicit waiting/throttling in the first sub-slice;
- no command-buffer status polling before proving probes and shutdown remain
  clean;
- add one capability at a time with a named acceptance gate.

### Revised Slice 3 sub-slices

#### 3A — POD counters only, no retention, no waits

Purpose: prove we can expose no-log LLDB-readable state without perturbing
lifetime or launch.

Implementation scope:

- Add a C-compatible global symbol, for example:
  ```cpp
  extern "C" DXMTD3D12QueueDebugCounters g_dxmt_d3d12_queue_debug_counters;
  ```
- The struct must be plain integral fields only (`uint64_t`, `uint32_t`).
- Use compiler/Win32 interlocked or `__atomic_*` operations on fields; do not put
  `std::atomic` members inside the exported struct.
- Counters only:
  - queue ctor count
  - queue handle
  - configured max-in-flight
  - `commandBuffer()` call count per site
  - `commandBuffer()` return count per site
  - null command-buffer count per site
  - commit count per site
  - sync wait count/return count for already-existing sync paths only
  - last site
  - last command-buffer handle observed
- Sites:
  - `ExecuteCommandLists`
  - `Signal`
  - `Wait`
- Do **not** retain command buffers.
- Do **not** inspect status/error.
- Do **not** add throttling.
- Do **not** alter command-buffer creation call order.

Offline acceptance before any game launch:

- Build succeeds.
- `nm` shows exactly the expected exported counter symbol and no Slice 3 helper
  symbols.
- `m12-dev preflight` passes.
- Focused `probe_queues` passes.
- Full mini/probe logs are scanned for new assertions; existing known async PSO
  `std::__condvar` lines must be classified separately and must not increase in
  scope/frequency for queue-only probes.
- Run repeated probe process startup/shutdown loop, at least 10 iterations, with:
  - no instant process aborts;
  - no new MinGW destructor assertions in queue-only/device-only probes;
  - no non-zero exit caused by queue instrumentation.
- Backend dry-run for AC6 passes after staging.
- Game-local and runtime `d3d12.dll` hashes match.

Game-launch acceptance, only after approval:

- AC6 launches visibly (or at least does not instant-zombie) with no logs/traces.
- If hang reproduces, LLDB can read the POD counters and show whether the block
  is before/after `commandBuffer()` return.

#### 3B — Non-owning fixed ring of observed handles

Purpose: capture recent command-buffer handles and sites without changing object
lifetime.

Implementation scope:

- Fixed-size POD ring in queue object or global debug storage.
- Store handle values only (`obj_handle_t` / integer cast), serials, sites, and
  commit markers.
- No retain/release.
- No status polling.
- No waits.
- No locks if all writes are queue-thread local; otherwise use a minimal
  spin-claim for index reservation only and release before doing any Metal work.

Offline acceptance:

- Same as 3A.
- Additional static audit proves no `NSObject_retain`, `NSObject_release`,
  `waitUntilCompleted`, `status`, or `error` calls were added in Slice 3B.
- Probe logs show no startup/shutdown regression.

Game-launch acceptance, only after approval:

- If hang reproduces, LLDB ring data identifies the last site/handle/serial seen
  before the blocked `commandBuffer()` call.

#### 3C — Optional status sampling, debugger-gated only

Purpose: inspect terminal state of previously committed buffers without forcing
completion or altering scheduling.

Implementation scope:

- Add a runtime env gate, default off, e.g.:
  ```text
  DXMT_D3D12_QUEUE_DEBUG_STATUS_SAMPLE=1
  ```
- When enabled, sample status only at safe points where a command buffer handle is
  already in scope and no locks are held.
- Do not retain handles unless a separate retention sub-slice has passed.
- Do not wait.
- Record status numerically only.

Offline acceptance:

- Gate off: binary behavior and probe logs match 3B.
- Gate on: focused queue/command replay probes pass repeatedly.
- No command-buffer lifetime crashes under repeated process teardown.

Game-launch acceptance, only after approval:

- Prefer gate off for first launch; gate on only if 3A/3B data is insufficient
  and user approves the diagnostic perturbation.

#### 3D — Retention and explicit oldest wait, last resort

Purpose: move the stall from Metal `commandBuffer()` allocation to our own
visible wait point only after non-owning diagnostics prove this is necessary.

Do not implement this until 3A-3C evidence shows command-buffer allocation
backpressure remains unexplained.

Implementation requirements:

- No C++ containers with destructors.
- Fixed raw handle ring only.
- Retain after commit only, release only after terminal status or queue teardown.
- Never hold the ring lock while calling Metal methods.
- Explicit oldest wait is behind an opt-in env gate and off by default.
- Destruction must release retained handles without waiting.
- Must include an emergency disable:
  ```text
  DXMT_D3D12_QUEUE_DEBUG_TRACKING=0
  ```

Offline acceptance:

- Repeated queue/command replay probes with tracking on and off.
- Repeated process teardown loop proves no destructor assertions or zombies caused
  by the tracker.
- Static symbol audit confirms no `std::condition_variable` additions and no new
  dynamic container ownership in command queue.
- Backend dry-run and runtime hashes pass.

Game-launch acceptance, only after approval:

- First launch with retention enabled but explicit oldest wait off.
- Only enable explicit oldest wait after a post-hang LLDB capture proves Metal is
  silently blocking in `commandBuffer()` and non-owning data cannot identify the
  first non-completing buffer.

### Required offline command sequence before any future Slice 3 launch

Run and save outputs under a new `tools/d3d12-metal-sdk/results/slice3-readiness-*`
directory:

```bash
./tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime
./tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime
./tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight
./tools/d3d12-metal-sdk/scripts/m12-dev.sh mini
./tools/d3d12-metal-sdk/scripts/m12-dev.sh probes
python3 tools/d3d12-metal-sdk/scripts/preflight-runtime-layout.py \
  --profile armored-core-vi \
  --game-dir "/Volumes/AverySSD/SteamLibrary/steamapps/common/ARMORED CORE VI FIRES OF RUBICON/Game" \
  --results-dir <slice3-readiness-dir>
curl -fsS "http://127.0.0.1:9277/diagnostics/m12/dry-run?appid=1888160" \
  | python3 -m json.tool > <slice3-readiness-dir>/dry-run-armored-core-vi-1888160.json
```

Then perform explicit audits:

```bash
# No old failed tracker helpers unless intentionally in the current sub-slice.
x86_64-w64-mingw32-nm -C ~/.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-windows/d3d12.dll \
  | rg 'g_dxmt_d3d12_queue_debug_counters|CreateTrackedCommandBuffer|TrackSubmittedCommandBuffer|RetireCompletedCommandBuffers|WaitForOldestTrackedCommandBuffer|LockInflight|UnlockInflight'

# No new condition-variable usage attributable to Slice 3.
x86_64-w64-mingw32-nm -C ~/.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-windows/d3d12.dll \
  | rg 'condition_variable|__condvar|pthread_cond'

# Probe logs must be searched, not just exit-code checked.
rg -n 'std::__condvar|Assertion|abort|Unhandled|Traceback|failed to launch|defunct' \
  <slice3-readiness-dir> tools/d3d12-metal-sdk/results/probe-*metalsharp*.json
```

### Slice 3 commit policy

- Commit 3A separately from 3B/3C/3D.
- Each commit must include only source/docs/tests; no generated results.
- Each commit message must identify the sub-slice, e.g.:
  ```text
  chore: add m12 queue debug counters
  ```
- Do not proceed from one sub-slice to the next without a written acceptance note
  in this roadmap.

### Current next action

Do **not** start Slice 3 implementation yet. The next action is a design review of
3A only: confirm the exported POD counter ABI, exact counter fields, and exact
call-site increments before editing source.
