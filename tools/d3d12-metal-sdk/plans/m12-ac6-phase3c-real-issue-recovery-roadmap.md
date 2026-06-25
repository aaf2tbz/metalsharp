# M12 AC6 Phase 3C Real-Issue Recovery Roadmap

Status: corrective roadmap after the 10-hour Phase 3A/3B work produced strong semantic fixes but Phase 3C still failed at launch/runtime.

This roadmap is **not** a rollback roadmap and **not** a “make it look like Slice-2” roadmap. Slice-2 is useful evidence that a simpler runtime could reach title/menu, but the target here is the actual issue family that motivated Phase 3:

```text
AC6 post-Continue real-world gameplay/loading/rendering/computation can drive invalid GPU memory access / page fault / MMU / firmware lockup class failures.
```

The 10-hour session addressed that real issue through resource lifetime, descriptor-table stability, ExecuteIndirect materialization, barrier replay, and present lifetime work. Those fixes should be preserved unless evidence shows a specific implementation is wrong.

The current Phase 3C failure means: one implementation detail introduced while solving those real hazards now faults before we can validate the original post-Continue hazard. Fix that implementation without deleting the semantic protections.

---

## Ground truth from the 10-hour Phase 3 work

The work was not arbitrary. Each major change targeted a real D3D12→Metal hazard class that can plausibly produce AC6 world-load GPU faults.

### 1. Submission lifetime retention

Why it was done:

- Metal `useResource` is not object retention.
- D3D12 command lists reference resources/descriptors that can be reset/recycled while Metal work is still in flight.
- AC6 post-Continue streaming/world rendering is exactly where descriptor/resource churn increases.

Keep:

```text
D3D12MetalSubmissionReferences
DXMT_D3D12_SUBMISSION_RETENTION
DXMT_D3D12_REQUIRE_SUBMISSION_RETENTION
completion-slot release after Metal completion
```

Validated evidence already exists:

```text
m12-phase1c-retention-required-on-json-20260619-152834  pass=true
m12-phase1c-retention-required-off-json-20260619-152823 pass=false
```

### 2. Graphics descriptor-table snapshots / residency

Why it was done:

- RTV/DSV/UAV snapshots were not enough.
- Real gameplay expands graphics CBV/SRV/UAV descriptor tables far beyond menu/title.
- Metal argument-buffer resources need stable object declarations before draws.

Keep:

```text
DXMT_D3D12_GRAPHICS_DESCRIPTOR_TABLE_SNAPSHOT
record-time graphics descriptor-table snapshots
arg-buffer residency retention for graphics draws
```

Validated evidence already exists:

```text
m12-phase2a-argbuf-residency-graphics-buffer-srv-20260619-164959      pass=true
m12-phase2a-argbuf-residency-graphics-buffer-srv-off-20260619-165216  pass=false
```

### 3. ExecuteIndirect GPU/default/placed materialization

Why it was done:

- AC6 world rendering can use GPU-authored indirect args/count buffers.
- CPU-mapping GPU/default/placed buffers is unsafe or stale.
- Failing open to `MaxCommandCount` can issue garbage draws.

Keep:

```text
DXMT_D3D12_EXECUTE_INDIRECT_GPU_MATERIALIZE
DXMT_D3D12_EXECUTE_INDIRECT_MATERIALIZE_CAP
fail-closed materialization failure behavior
backing-offset fixes
```

Validated evidence already exists:

```text
m12-phase2b-execute-indirect-gpu-materialize-placed-copyfix-20260619-222216 pass=true
m12-phase2b-execute-indirect-gpu-materialize-off-placed-copyfix-20260619-222733 pass=false
```

### 4. Barrier/UAV/aliasing/subresource replay

Why it was done:

- Post-Continue world load enters compute→graphics chains, UAV writes, copy→SRV transitions, aliasing, and subresource transitions.
- D3D12 explicit state/hazard management must be represented before Metal execution.

Keep:

```text
DXMT_D3D12_BARRIER_REPLAY
DXMT_D3D12_REQUIRE_BARRIER_REPLAY
barrier-bearing command-list sensitivity gate
```

Validated evidence already exists:

```text
m12-phase2c-barriers-uav-aliasing-subresource-coverage-fix-20260619-183024 pass=true
m12-phase2c-barrier-gate-positive-20260619-192239                         pass=true
m12-phase2c-barrier-gate-required-off-20260619-192249                      pass=false
```

### 5. Present/drawable lifetime cage

Why it was done:

- Magenta is presentation-visible even if the root is earlier GPU work.
- Present source texture/drawable/drawable texture must survive until the present command buffer completes.
- Resize/backbuffer recreation must not release backbuffers while present work is in flight.

Keep the semantic goal:

```text
present command buffer retains source resource/source texture/destination texture/drawable/drawable texture until completion
resize drains inflight present work before releasing backbuffers
single ownership of present command buffers
```

But current implementation must be repaired because Phase 3C found an early fault in this area.

---

## Current actual failure to solve first

Latest proper Phase 3C launch evidence:

```text
tools/d3d12-metal-sdk/results/m12-phase3c-ac6-user-approved-retry-proper-20260620-003313/
launch log: ~/.metalsharp/compatdata/1888160/logs/launch-1781937195.log
```

Fault:

```text
wine: Unhandled page fault on read access to 0000000011760000
at address 00006FFFDCF846E0
```

Symbolication against current staged `d3d12.dll`:

```text
dxmt::MTLD3D12SwapChain::Present1(...)
vendor/dxmt/src/d3d12/d3d12_swapchain.cpp:1273
```

Current line:

```cpp
WaitForPresentCommandBufferSlot();
```

The immediate issue is **not yet the original post-Continue GPU/MMU failure**. The current runtime hits an early present-path CPU/Wine access violation before the post-Continue validation can be trusted.

Root working hypothesis:

> Phase 3 present lifetime semantics are correct, but their implementation made the hot `Present1` path too heavy and/or too ABI/stack-sensitive under Wine. The fix is to preserve present lifetime retention while moving heavyweight retention/slot work out of the fragile `Present1` path.

---

# Phase A — Preserve Phase 3 semantics, isolate the present implementation fault

Goal: prove exactly which Phase 3 present implementation choice causes the current early fault, without discarding the rest of Phase 3.

## A1. Freeze current evidence

Record and keep:

```text
current d3d12.dll hash
current d3d12.dll symbolication for 0x6FFFDCF846E0
current Present1 disassembly/prologue
current launch log fault line
current process contamination inventory
```

Existing evidence location:

```text
tools/d3d12-metal-sdk/results/m12-phase3c-ac6-user-approved-retry-proper-20260620-003313/slice2-current-diff/
```

## A2. Add a source/binary gate for the actual failure surface

This gate is not “compare to Slice-2.” It is a runtime-safety gate for the function that actually faulted.

Add:

```text
tools/d3d12-metal-sdk/scripts/check-dxmt-symbol-frame.py
```

Required behavior:

- Input: PE DLL, symbol substring, max frame size.
- Locate `MTLD3D12SwapChain::Present1` in disassembly.
- Extract stack frame allocation.
- Fail when the present function becomes excessively large.

Initial threshold:

```text
Present1 frame <= 0x800
```

Rationale:

- Current failed runtime: `0xb48`.
- A hot COM entrypoint called from Wine/game threads should not carry heavyweight retention containers and multi-branch temporaries directly on stack.
- The threshold is a safety budget for the faulting function, not an aesthetic comparison.

## A3. Add a present hot-path contract probe

Extend:

```text
tools/d3d12-metal-sdk/probes/probe_present_lifetime_resize/probe_present_lifetime_resize.cpp
```

Required assertions:

- `Present1` does not construct heavyweight `D3D12MetalSubmissionReferences` temporaries at every commit branch.
- Present lifetime retention still exists somewhere authoritative.
- Present command-buffer slots retain source/destination/drawable objects until completion.
- Resize still drains before backbuffer release.
- The faulting line class (`WaitForPresentCommandBufferSlot` before command-buffer acquisition) is explicitly covered by source contract.

Exit criteria:

- Current failed runtime/source fails the new safety gate.
- The repaired runtime/source passes.
- No AC6 relaunch required.

---

# Phase B — Repair present lifetime implementation, not the semantic fix

Goal: keep the present lifetime cage but make it runtime-safe.

Do **not** delete present retention. Do **not** revert all Phase 3 work. Instead, restructure it.

## B1. Move heavyweight retention construction out of `Present1`

Current pattern to eliminate from `Present1`:

```cpp
TrackPresentCommandBuffer(
    cmdbuf,
    MakePresentSubmissionReferences(res, src_texture, dst_texture, drawable));
```

Problem:

- `D3D12MetalSubmissionReferences` owns multiple vectors/unordered_sets.
- Returning/passing it by value inside `Present1` inflates the hot frame and increases Wine ABI/stack sensitivity.

Preferred fix:

Create a present-specific lightweight retention object, e.g.:

```cpp
struct D3D12MetalPresentReferences {
  Com<MTLD3D12Resource> resource;
  WMT::Reference<WMT::Buffer> resource_buffer;
  WMT::Reference<WMT::Texture> resource_texture;
  WMT::Reference<WMT::Texture> src_texture;
  WMT::Reference<WMT::Texture> dst_texture;
  WMT::Reference<WMT::MetalDrawable> drawable;
  WMT::Reference<WMT::Texture> drawable_texture;
};
```

Then make present slots hold this lightweight object instead of the generic submission bucket, or hold a union/variant-like separated present bucket.

## B2. Keep command-queue submission retention unchanged

The generic `D3D12MetalSubmissionReferences` bucket remains correct for command-list replay because it must handle arbitrary descriptor/resource sets.

Do not weaken:

```text
command queue submission references
descriptor-table resource retention
ExecuteIndirect materialized buffer retention
barrier/copy/resolve resource retention
```

## B3. Keep or simplify present slot waiting deliberately

`WaitForPresentCommandBufferSlot()` is conceptually valid, but it should be safe and small.

Allowed repairs:

- Keep slot wait but move it into a noinline helper whose frame is small.
- Ensure it indexes only the fixed slot array bound.
- Ensure it cannot touch stale slot completion pointers after reset.
- Avoid additional logging/formatting from the hot path unless gated.

Potentially restore present queue sizing to a simpler value only if evidence shows queue depth caused the fault. Queue sizing is secondary; the actual symbolicated fault is the `Present1` hot path.

Exit criteria:

- Present source/drawable retention remains real.
- `Present1` frame safety gate passes.
- Present source-contract probe passes.

---

# Phase C — Validate the actual hazard classes, not just title/menu

Goal: prove the repaired runtime still addresses why Phase 3 existed.

## C1. Re-run all Phase 3 semantic gates

Required positives:

```text
submission retention required ON passes
graphics argbuf residency passes
ExecuteIndirect GPU/default/placed materialization passes
barriers/UAV/aliasing/subresource passes
present lifetime/resize source-contract passes
queues/cross-queue passes
```

Required negatives/sensitivity:

```text
DXMT_D3D12_REQUIRE_SUBMISSION_RETENTION=1 with retention disabled fails as expected
DXMT_D3D12_GRAPHICS_DESCRIPTOR_TABLE_SNAPSHOT=0 fails argbuf residency probe as expected
DXMT_D3D12_EXECUTE_INDIRECT_GPU_MATERIALIZE=0 fails ExecuteIndirect GPU materialization probe as expected
DXMT_D3D12_REQUIRE_BARRIER_REPLAY=1 DXMT_D3D12_BARRIER_REPLAY=0 fails barrier-bearing probe as expected
```

These tests address the real post-Continue issue classes:

- invalid resource lifetime;
- descriptor-table mutation/residency;
- GPU-authored indirect args/counts;
- compute/copy/graphics hazard ordering;
- present/drawable lifetime.

## C2. Add one new present-specific runtime safety probe

Add a probe that exercises present command-buffer slot tracking without AC6:

```text
probe_present_completion_slot_safety
```

Minimum coverage:

- create swapchain/backbuffers;
- submit more presents than inflight limit;
- force slot reuse;
- verify completed slots reset safely;
- verify retained source/drawable objects survive until completion;
- verify no CPU access violation / no command-buffer error;
- run with `DXMT_D3D12_PRESENT_INFLIGHT_LIMIT=1` and default.

If a true windowed probe is flaky, pair it with source-contract checks, but the binary frame gate is mandatory.

## C3. Add binary gates to the normal pre-staging checklist

Required before staging:

```text
check-dxmt-symbol-frame.py d3d12.dll Present1 --max-frame 0x800
check-dxmt-symbol-frame.py d3d12.dll WaitForPresentCommandBufferSlot --max-frame 0x200
```

The goal is not optimization for its own sake; it is preventing the exact kind of Wine/Win64 hot-path stack regression that just blocked Phase 3C.

Exit criteria:

- All Phase 3 semantic gates pass.
- New present safety probe/gate passes.
- Autoreview finds no blocker.

---

# Phase D — Stage only after repaired runtime is semantically and mechanically safe

Goal: stage the repaired runtime without losing runbook discipline.

Required:

```text
clean rebuild
full runtime set only
stage to ~/.metalsharp/runtime/wine/lib/dxmt_m12
copy full Windows DLL set to AC6 Game/
mscompatdb absent
backend rebuilt/restarted on METALSHARP_PORT=9277
preflight ok=true failure_count=0
dry-run ok=true pipeline=m12
```

Additional new requirement:

```text
staged runtime d3d12.dll passes Present1 frame/safety gate
```

Do not launch during this phase.

---

# Phase E — Controlled AC6 validation in two separate steps

Goal: separate “runtime can launch/title/menu” from “post-Continue world-load hazard.”

## E1. Running-shape launch test

Only with explicit user approval.

Use:

```text
POST http://127.0.0.1:9277/steam/launch-game
```

Payload uses backend-supported keys only:

```json
{
  "appid": 1888160,
  "launchMethod": "m12",
  "envOverrides": {
    "METALSHARP_M12_LOG_LEVEL": "none",
    "METALSHARP_M12_LOG_PATH": "none",
    "METALSHARP_M12_TRACE_CAPTURE": "0",
    "METALSHARP_M12_FORCE_DXIL_SOURCE_COMPILE": "0",
    "METALSHARP_M12CORE_ENABLE": "1",
    "METALSHARP_M12CORE_REQUIRED": "0",
    "METALSHARP_M12CORE_DUMP_COUNTERS": "0"
  }
}
```

Success criteria:

- no immediate `Present1`/startup fault;
- process remains alive beyond initial startup;
- title/menu can be reached manually.

Do not press Continue in E1 unless separately approved.

## E2. Post-Continue hazard validation

Only after E1 succeeds and only with explicit user approval.

This is the actual original problem test.

Success criteria:

- Continue/world-load enters gameplay without magenta/GPU/MMU/firmware lockup;
- if failure occurs, classify it against the Phase 3 hazard buckets using collected evidence, not repeated crash loops.

Failure classification buckets:

```text
A. early CPU/Wine crash in startup/present path — not original issue, fix implementation
B. command-buffer Metal error/page fault with encoder status — inspect resource/barrier/descriptor bucket
C. magenta/present symptom with no command-buffer error — inspect producer→present ordering/readback
D. GPU hang/firmware lockup — stop immediately, use offline evidence and crash logs only
```

---

## Definition of done

This roadmap is complete when:

1. Phase 3 semantic fixes remain present and sensitivity-proven.
2. The current early `Present1` fault is fixed without deleting retention/descriptor/barrier/indirect work.
3. New present hot-path safety gates would have rejected the failed runtime.
4. Preflight/dry-run/staging pass with the repaired runtime.
5. One approved AC6 launch reaches title/menu without the current early-present failure.
6. Only then, one separately approved post-Continue validation can determine whether the original GPU/MMU issue is resolved.
