# M12 AC6 Phase 3C Running-Shape Recovery Roadmap

Status: new corrective roadmap after Phase 3A/3B semantic gates passed but Phase 3C launch regressed below Slice-2 running behavior.

This roadmap supersedes any assumption that the 10-hour Phase 3A runtime is ready for AC6 gameplay validation. It does **not** authorize crash-looping AC6. It brings the current runtime back to a Slice-2-equivalent running shape first, then resumes controlled AC6 validation.

Authoritative baseline comparison:

```text
Slice-2 baseline HEAD: 72a706d2aa7e342389620627df23eac2498438a6
Slice-2 runtime: /Users/alexmondello/slice2baseline/latest/runtime/dxmt_m12
Current Phase 3A source HEAD: a909c83d158cdcd0058c51b056ed3c77ae6d782e plus local worktree changes
Current evidence: tools/d3d12-metal-sdk/results/m12-phase3c-ac6-user-approved-retry-proper-20260620-003313/
```

Hard constraint:

> Do not diagnose this by repeated unsafe AC6 launches. Use Slice-2/current binary and source diffs, offline probes, frame-size/binary gates, and one explicitly approved controlled launch only after the new gates are green.

---

## Current failure evidence

Latest proper user-approved launch:

```text
tools/d3d12-metal-sdk/results/m12-phase3c-ac6-user-approved-retry-proper-20260620-003313/
launch log: ~/.metalsharp/compatdata/1888160/logs/launch-1781937195.log
```

Fault:

```text
wine: Unhandled page fault on read access to 0000000011760000
at address 00006FFFDCF846E0 (thread 0854)
```

Symbolication against the staged current `d3d12.dll`:

```text
current d3d12.dll: vendor/dxmt/build-metalsharp-x64/src/d3d12/d3d12.dll
inferred loaded base: 0x6fffdcf00000
RVA: 0x846e0
PE VMA: 0x310bf46e0

symbol:
dxmt::MTLD3D12SwapChain::Present1(unsigned int, unsigned int, DXGI_PRESENT_PARAMETERS const*)
vendor/dxmt/src/d3d12/d3d12_swapchain.cpp:1273
```

Faulting source line in current Phase 3A runtime:

```cpp
WaitForPresentCommandBufferSlot();
```

Important comparison:

```text
Slice-2 MTLD3D12SwapChain::Present1 stack frame: 0x608
Current MTLD3D12SwapChain::Present1 stack frame: 0xb48
```

Evidence files:

```text
tools/d3d12-metal-sdk/results/m12-phase3c-ac6-user-approved-retry-proper-20260620-003313/slice2-current-diff/02-present1-symbol-disasm-summary.txt
tools/d3d12-metal-sdk/results/m12-phase3c-ac6-user-approved-retry-proper-20260620-003313/slice2-current-diff/04-present1-source-summary.txt
tools/d3d12-metal-sdk/results/m12-phase3c-ac6-user-approved-retry-proper-20260620-003313/slice2-current-diff/05-helper-struct-diff.log
```

---

## Root-cause hypothesis to validate first

Slice-2 did not fail in the current early-present area. Current Phase 3A added a heavier present lifetime cage:

```diff
- m_present_queue = wmt_device.newCommandQueue(1);
- auto cmdbuf = m_present_queue.commandBuffer();
+ m_present_queue = wmt_device.newCommandQueue(PresentQueueMaxCommandBuffers());
+ WaitForPresentCommandBufferSlot();
+ auto cmdbuf = commandBufferWithDescriptor(...) or commandBuffer();
```

and changed present tracking from simple command-buffer retention to heavy submission-reference construction:

```diff
- TrackPresentCommandBuffer(cmdbuf);
+ TrackPresentCommandBuffer(
+   cmdbuf,
+   MakePresentSubmissionReferences(res, src_texture, dst_texture, drawable));
```

`D3D12MetalSubmissionReferences` owns multiple `std::vector` and `std::unordered_set` members. Returning/passing this object by value from inside `Present1` likely inflated the O0 Win64 frame and moved stack accesses into a Wine/game thread stack boundary. The crash occurs before the original post-Continue GPU/MMU class can be validated.

This hypothesis must be proved or falsified before touching the original AC6 Continue hazard again.

---

# Phase 0 — Freeze evidence and keep restore paths intact

Goal: preserve both known-good Slice-2 runtime and failed Phase 3A runtime for exact A/B comparisons.

Tasks:

1. Do not delete or overwrite:

```text
/Users/alexmondello/slice2baseline/latest/
tools/d3d12-metal-sdk/results/m12-phase3c-ac6-user-approved-retry-proper-20260620-003313/
/tmp/metalsharp-m12-phase3b-stepwise-slice2-backup-20260620-001953/
```

2. Record current hashes for:

```text
vendor/dxmt/build-metalsharp-x64/src/d3d12/d3d12.dll
~/.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-windows/d3d12.dll
/Users/alexmondello/slice2baseline/latest/runtime/dxmt_m12/x86_64-windows/d3d12.dll
```

3. Quiesce stale Wine process contamination before any later approved retest. The current process list contains many old `services.exe`, `plugplay.exe`, and `rpcss.exe` instances from prior launches. A future launch must start from a clean Wine prefix runtime state or explicitly record why background Wine Steam remains required.

Exit criteria:

- Evidence bundle has hashes, process inventory, Slice-2/current DLL paths, and crash symbolication.
- No additional AC6 launch has occurred.

---

# Phase 1 — Binary/source A/B gates for Slice-2 running shape

Goal: add hard gates that would have rejected the current runtime before staging.

## 1A. Present1 frame-size gate

Add an offline binary gate script, for example:

```text
tools/d3d12-metal-sdk/scripts/check-dxmt-symbol-frame.py
```

Required behavior:

- Input: PE DLL path, symbol substring, max allowed frame size.
- Uses `x86_64-w64-mingw32-objdump -d -C`.
- Locates:

```text
dxmt::MTLD3D12SwapChain::Present1(unsigned int, unsigned int, DXGI_PRESENT_PARAMETERS const*)
```

- Extracts prologue `sub $0x..., %rsp`.
- Fails if frame exceeds threshold.

Initial threshold:

```text
max Present1 frame <= 0x700
```

Rationale: Slice-2 is `0x608`; current failed runtime is `0xb48`.

## 1B. Present regression source-contract probe

Extend the existing source-contract present probe:

```text
tools/d3d12-metal-sdk/probes/probe_present_lifetime_resize/probe_present_lifetime_resize.cpp
```

Required checks:

- `Present1` must not construct heavyweight `D3D12MetalSubmissionReferences` temporaries inline via `MakePresentSubmissionReferences(...)` at each commit site.
- Present retention must still retain:
  - backbuffer resource;
  - source texture;
  - destination texture when applicable;
  - drawable;
  - drawable texture.
- Present slot cage must not increase the present queue beyond a bounded slot model without matching fixed storage.
- Resize still drains inflight present command buffers before releasing backbuffers.

Exit criteria:

- Frame-size gate passes for rebuilt current runtime.
- Source-contract probe passes.
- The gate fails when run against the currently failed Phase 3A `d3d12.dll` or with a deliberately too-low threshold.

---

# Phase 2 — Surgical present-path repair

Goal: keep the Phase 2D present lifetime semantics without regressing Slice-2 running shape.

Candidate repair design:

1. Move present submission-reference construction out of the hot `Present1` stack path.
2. Avoid returning/passing `D3D12MetalSubmissionReferences` by value from `MakePresentSubmissionReferences(...)` inside `Present1`.
3. Replace with one of:

### Option A — Fill-by-reference helper

```cpp
void RetainPresentSubmissionReferences(
    D3D12MetalSubmissionReferences &refs,
    MTLD3D12Resource *resource,
    WMT::Texture src_texture,
    WMT::Texture dst_texture = {},
    WMT::MetalDrawable drawable = {});
```

Then allocate the retention object inside the slot/arm path, not as multiple large temporaries in `Present1`.

### Option B — Present-specific lightweight refs

Create a small present-only retention struct with fixed fields:

```cpp
Com<MTLD3D12Resource> resource;
WMT::Reference<WMT::Texture> src_texture;
WMT::Reference<WMT::Texture> dst_texture;
WMT::Reference<WMT::MetalDrawable> drawable;
WMT::Reference<WMT::Texture> drawable_texture;
```

Use this only for present slots, and leave the heavier general submission bucket for command-queue submissions.

### Option C — Split present commit helper

Move the commit/track branches into smaller noinline helpers so `Present1` does not carry every branch’s temporaries in one O0 frame.

Preference order:

1. Option B if present only needs fixed resources.
2. Option C if fastest/least invasive.
3. Option A only if it demonstrably reduces `Present1` frame size.

Required code anchors:

```text
vendor/dxmt/src/d3d12/d3d12_swapchain.cpp
vendor/dxmt/src/d3d12/d3d12_swapchain.hpp
vendor/dxmt/src/d3d12/d3d12_command_buffer_completion.hpp
```

Exit criteria:

- `MTLD3D12SwapChain::Present1` frame returns near Slice-2 shape:

```text
Present1 frame <= 0x700
```

- Present lifetime source-contract probe still passes.
- No regression to retaining drawable/resource/texture until present command-buffer completion.

---

# Phase 3 — Offline validation gauntlet after present-path repair

Goal: prove we did not break the Phase 3A semantic fixes while restoring Slice-2 running shape.

Required commands:

```text
git diff --check
meson compile -C vendor/dxmt/build-metalsharp-x64
./tools/d3d12-metal-sdk/scripts/build-probes.sh
bash -n tools/d3d12-metal-sdk/scripts/build-probes.sh tools/d3d12-metal-sdk/scripts/run-probes.sh
```

Required probes/gates:

```text
--present-lifetime-resize-only
--queues-only
--descriptor-mutation-graphics-only
--argbuf-residency-graphics-only
--execute-indirect-draw-replay-only
--barriers-uav-aliasing-subresource-only
```

Required sensitivity checks to preserve previous 10-hour work:

```text
DXMT_D3D12_REQUIRE_SUBMISSION_RETENTION=1 positive/negative proof
DXMT_D3D12_GRAPHICS_DESCRIPTOR_TABLE_SNAPSHOT=0 negative proof
DXMT_D3D12_EXECUTE_INDIRECT_GPU_MATERIALIZE=0 negative proof
DXMT_D3D12_REQUIRE_BARRIER_REPLAY=1 DXMT_D3D12_BARRIER_REPLAY=0 negative proof
```

Required new binary gate:

```text
check-dxmt-symbol-frame.py d3d12.dll Present1 --max-frame 0x700
```

Exit criteria:

- All existing Phase 3A semantic probes remain green.
- New Present1 frame gate is green.
- Autoreview has no blockers for present-path repair.

---

# Phase 4 — Rebuild/restage with corrected running-shape gates

Goal: repeat the Slice-2 staging runbook, but only after the new binary/source gates pass.

Required staging discipline:

- Use current source after repair.
- Rebuild DXMT cleanly.
- Stage full runtime set only:

```text
d3d12.dll d3d11.dll dxgi.dll dxgi_dxmt.dll d3d10core.dll winemetal.dll winemetal.so libm12core.dylib
```

- Stage to:

```text
~/.metalsharp/runtime/wine/lib/dxmt_m12
```

- Copy full Windows DLL set to AC6 `Game/`.
- Keep `mscompatdb` absent.
- Rebuild/restart PR backend on:

```text
METALSHARP_PORT=9277
```

- Verify:

```text
preflight ok=true failure_count=0
dry-run ok=true pipeline=m12
WINEDLLPATH contains dxmt_m12/x86_64-windows
DYLD_LIBRARY_PATH starts with dxmt_m12/x86_64-unix
DXMT_WINEMETAL_UNIXLIB=winemetal.so
WINEDLLOVERRIDES does not contain mscompatdb
```

Exit criteria:

- Hash sync build/runtime/game is clean.
- Preflight and dry-run pass.
- Present1 frame gate is recorded against the staged runtime DLL, not just build output.
- No AC6 launch yet.

---

# Phase 5 — One controlled AC6 running-shape retest

Goal: prove the runtime is at least back to Slice-2 running shape before resuming post-Continue validation.

Prerequisites:

- Explicit user approval.
- Clean Wine process state captured.
- Backend `127.0.0.1:9277/status ok=true`.
- No-log payload uses backend-supported keys only:

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

Launch endpoint:

```text
POST http://127.0.0.1:9277/steam/launch-game
```

Success threshold for this phase only:

- Process does not immediately fault in `Present1` / early startup.
- Title/menu path reaches at least the known Slice-2 running behavior.
- If it fails, capture one launch log and stop.

Non-goal:

- Do not press Continue in this phase unless the user explicitly approves a separate Phase 6 post-Continue retest.

---

# Phase 6 — Resume original post-Continue GPU/MMU validation

Only after Phase 5 proves the runtime has recovered Slice-2 running shape:

- Reuse the original post-Continue roadmap constraints.
- One controlled user-approved Continue retest maximum.
- If magenta/GPU fault recurs, classify against the Phase 3A semantic buckets:
  - resource lifetime;
  - graphics descriptor snapshots;
  - ExecuteIndirect GPU materialization;
  - barrier/subresource/UAV/aliasing;
  - present producer ordering.

Do not conflate an early `Present1`/startup crash with the original post-Continue world-load GPU/MMU class.

---

## Definition of done for this roadmap

This roadmap is complete only when:

1. The current runtime no longer faults in early `MTLD3D12SwapChain::Present1`.
2. Current `d3d12.dll` passes the Present1 frame-size gate against the staged runtime.
3. All prior Phase 3A semantic probes and sensitivity checks still pass.
4. Slice-2 runbook staging verification passes.
5. One user-approved AC6 launch reaches Slice-2-equivalent title/menu running behavior without immediate early-present crash.

Only then can the original AC6 Continue GPU/MMU investigation resume.
