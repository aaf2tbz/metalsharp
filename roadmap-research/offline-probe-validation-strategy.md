# Offline Probe Validation Strategy — AC6 Continue Retest Gate

Status: scouting output only. No files edited, no probes added, nothing staged, no game
launched, no processes killed. Every command below is **proposed**, not executed. Treat
this document as the test matrix an implementer should follow *before* any AC6 launch
approval is requested.

Scope: validate the M13.0–M13.6 fixes from
`tools/d3d12-metal-sdk/plans/m12-ac6-postcontinue-gpu-fault-roadmap.md` using **offline
probes only**, with the Slice-2 runbook constraints
(`tools/d3d12-metal-sdk/plans/m12-ac6-slice2-stage-launch-runbook.md`) honored at every
step.

---

## 1. Current baseline (what already exists and passes)

### 1.1 Probes on disk

All under `tools/d3d12-metal-sdk/probes/`. Built by
`tools/d3d12-metal-sdk/scripts/build-probes.sh` and run by
`tools/d3d12-metal-sdk/scripts/run-probes.sh` (matrix enforced by
`tools/d3d12-metal-sdk/scripts/compare-contract.py` and
`tools/d3d12-metal-sdk/scripts/validate-probe-matrix.py`).

Relevant to the post-Continue fault class:

| Probe | File | Covers | Gap vs. M13 |
|---|---|---|---|
| Descriptors | `probes/probe_descriptors/probe_descriptors.cpp` | CBV/SRV/UAV/Sampler/RTV/DSV creation, null views, descriptor copy, root sig v1.0 and v1.1, register-space collision, unbounded range | No replay, no mutation, no lifetime |
| Heap aliasing | `probes/probe_heap_aliasing/probe_heap_aliasing.cpp` | Placed **buffer** pair on same heap offset, aliasing barrier, copy after alias, queue fence completion | Buffers only; no texture aliasing; single queue/list |
| Barriers/render-pass | `probes/probe_barriers_render_pass/probe_barriers_render_pass.cpp` | render-pass split clear/store, copy→SRV transition, single-resource UAV→UAV visibility, present transition | All barriers are ALL_SUBRESOURCES; no cross-queue UAV; no aliasing barrier |
| Command replay | `probes/probe_command_replay/probe_command_replay.cpp` | list reset/close/reuse, multi-list execute, bundle status, ExecuteIndirect dispatch + root constants; draw/draw_indexed are **signature-only** | No actual indirect draw replay; no count-buffer edge cases; no arg-buffer mutation |
| Present windowed | `probes/probe_present_windowed/probe_present_windowed.cpp` | window/swapchain create, distinct backbuffers, frame-latency waitable, color space, fullscreen/windowed, resize, 60-frame present stress, 16-frame compute descriptor stress, RTV/DSV/UAV descriptor mutation snapshot | No in-flight-during-resize; no producer→present ordering stress; no present-path-chooser one-present assertion |

Mini probes (`probes/probe_mini_suite/`): one-purpose cases for create_device,
command_queue, swapchain_present, rtv_clear, compute_dispatch, root_signature, descriptors,
graphics_pso, geometry_shader_pso, mesh_object_shader_pso, texture_sample,
subnautica_geometry_dxil_replay, dxil_texture_color_output, compute_first_use_dispatch.

### 1.2 Results that already prove the Slice 1–5 staging baseline

Most recent fully-green offline evidence bundle (2026-06-19T03:02Z):

- `tools/d3d12-metal-sdk/results/m12-slices1-5-offline-suite-20260619-025544/summary.json` —
  rc=0 for graphics-pso, compute-pso, command-replay, barriers-render-pass,
  resource-views-formats, heap-aliasing.
- `tools/d3d12-metal-sdk/results/m12-slices1-5-windowed-rerun-20260619-025916/probe-present-windowed-metalsharp.json` —
  `pass:true`, 60/60 present stress frames, 16/16 descriptor compute frames, RTV/DSV/UAV
  descriptor mutation checks all true.
- `tools/d3d12-metal-sdk/results/m12-slices1-5-final-hash-sync-20260619-030220/hash-sync-and-mscompatdb.txt` —
  build/runtime/game hashes match for the full Windows DLL set, build/runtime hashes match
  for Unix sidecars, `mscompatdb` search empty.
- `tools/d3d12-metal-sdk/results/m12-slices1-5-preflight-20260619-025806/runtime-preflight-metalsharp.json` —
  `ok:true`.
- `tools/d3d12-metal-sdk/results/probe-command-replay-metalsharp.json` —
  `pass:true`, including `execute_indirect_dispatch:true`; **but**
  `dispatch_root_constants_verified:false` (signature accepted, dispatch executes, root
  constants not honored by replay).

### 1.3 Runtime state at HEAD `a909c83` (latest commit on `fix/m12-shader-probe-lab`)

Inspected in `vendor/dxmt/src`. The roadmap claims "Implemented in current WIP" for
M13.1/M13.3/M13.5, but the actual landed code is narrower:

- `DXMT_D3D12_METAL_QUEUE_MAX_INFLIGHT` (default **32**, clamp 1..64) —
  `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp:200, 8019`. Slice-1 of the
  backpressure roadmap is done.
- `D3D12MetalCommandBufferCompletionSlot` —
  `vendor/dxmt/src/d3d12/d3d12_command_buffer_completion.hpp`. Retains **only** the
  `WMT::CommandBuffer`. Does **not** retain D3D12 resources, WMT textures, WMT buffers,
  drawables, or argument/table buffers. M13.1 "completion retention bucket" is **not**
  structurally implemented; what exists is backpressure, not resource retention.
- `DXMT_D3D12_GPU_HANG_SAFETY` + `DXMT_D3D12_STRICT_ARG_BUFFER_RESOURCES` (compute strict
  arg-buffer `useResource` declaration) — `d3d12_command_queue.cpp:156, 164, 3056`.
  Compute-only. Graphics has no equivalent blanket.
- `DXMT_D3D12_REPLAY_RETENTION_DIAGNOSTICS` (commit `c3215ec`) — default-off **diagnostic
  logging only**. `LogReplayRetentionCandidate` prints a candidate for every copy/resolve/
  barrier/indirect path; it does **not** add the resource to a retention container.
- `DXMT_D3D12_RETAIN_RENDER_ATTACHMENTS` (commit `a909c83`) — default-off
  `UseRenderAttachmentResource` that calls `render_enc.useResource(...)` for active
  RTV/DSV snapshot resources when a render encoder opens. This is a residency declaration,
  not a retention; Apple's `MTLRenderCommandEncoder.h` explicitly states `useResource` does
  not retain.
- RTV/DSV/UAV descriptor snapshots (commit `b09d154`) — `D3D12DescriptorSnapshot` is
  captured at record time and used at replay time for `OMSetRenderTargets` / `ClearRTV` /
  `ClearDSV` / `ClearUAV`. **Confirmed working** by probe_present_windowed passing
  `descriptor_mutation_verified:true`.
- winemetal bridge (`vendor/dxmt/src/winemetal/`) exposes `MTLCommandBuffer_error` (returns
  NSError), `MTLCommandBuffer_logs`, and `MTLCommandBuffer_status`. It does **not** expose:
  - `MTLCommandBufferDescriptor` / `commandBufferWithDescriptor:`
  - `MTLCommandBufferErrorOptionEncoderExecutionStatus`
  - `MTLCommandBufferEncoderInfo` enumeration
  - `setRetainedReferences:` (currently always uses default `[queue commandBuffer]`).
  - `useHeap:` wrappers (`rg useHeap vendor/dxmt/src` returns nothing).

**Net:** the descriptor-snapshot correctness fix is real; the completion-retention and
command-buffer error-instrumentation pieces of M13.0/M13.1 are **not** yet wired up at the
runtime layer, so they cannot be exercised by any probe today.

---

## 2. Slice-2 runbook constraints that gate every probe

From `tools/d3d12-metal-sdk/plans/m12-ac6-slice2-stage-launch-runbook.md` (Absolute
invariants). These apply to probe work too — probes run the same DXMT runtime that AC6
will use:

1. Git HEAD must match the Slice-2 commit (or its successors, with operator approval)
   before any rebuild/stage. Currently `a909c83`.
2. Backup mutable runtime state (`vendor/dxmt/build-metalsharp-x64`,
   `~/.metalsharp/runtime/wine/lib/dxmt_m12`, AC6 game-local DLLs, backend binary,
   any `mscompatdb*` files) into `/tmp` before rebuild/stage. Never delete without
   backup.
3. Stage M12 only into `~/.metalsharp/runtime/wine/lib/dxmt_m12`; **never** into
   `lib/dxmt`. `stage-runtime.sh` hard-fails if the path contains `/lib/dxmt/`.
4. Quarantine `mscompatdb.so` from active Wine unix route before launch verification.
   Probes intentionally tolerate `mscompatdb` via `run-probes.sh` (it installs a no-op
   `mscompatdb_rules.toml` under the result dir's `MS_ROOT`), but the launch lane
   must not.
5. Use `METALSHARP_PORT`, not `PORT`. Backend baseline hash
   `6fdface8f06740091d0047669ae6abf91ddd8088606ec2ad458ba978b04a7c0b`.
6. No-log/no-trace defaults for any visual launch; `DXMT_D3D12_TRACE=0`,
   `DXMT_WINEMETAL_DEBUG=0`, `DXMT_D3D12_PSO_TRACE=0`, `DXMT_M12CORE_REQUIRED=0`,
   `DXMT_M12CORE_ENABLE=1`.
7. Do **not** launch AC6. Do **not** kill Steam/Wine/wineserver/backend/game. Every
   probe below is offline — it loads the staged DXMT DLLs through `wine` against a
   probe root, not against AC6.
8. Do not commit generated binaries, logs, raw payloads, shader caches, or result
   corpora. Probe JSON outputs are evidence, kept under
   `tools/d3d12-metal-sdk/results/<bundle>/` or `/tmp`, never inside `vendor/`.

Runbook Section 12 (post-launch sanity) is intentionally **out of scope** for this
strategy — that step requires launch approval.

---

## 3. Probe matrix vs. fault class — what is and is not covered

| Fault class (from roadmap §"Working hypothesis") | Existing probe coverage | Missing probe coverage (this section → §4) |
|---|---|---|
| Descriptor snapshot/lifetime | RTV/DSV/UAV mutation snapshot in `probe_present_windowed` (single frame, single mutation). Descriptor creation/null/copy in `probe_descriptors` (no replay). | Graphics root-table SRV/CBV/UAV mutation before execute; descriptor heap recycling stress; destroy-after-record; multi-frame mutation. |
| Graphics argument-buffer resource use | Compute strict mode exists; render-attachment `useResource` exists but is default-off and only declares RTV/DSV. | Graphics SRV/CBV/UAV descriptor-table draw with `useResource` blanket; null/missing-descriptor draw neutralization; mixed VS/PS table. |
| UAV/aliasing/subresource barriers | Single-resource UAV→UAV visibility (same list, same queue); buffer aliasing barrier (single pair). | Cross-queue UAV write→SRV read; subresource (mip/array) transition; texture aliasing barrier; split-list split-queue UAV. |
| ExecuteIndirect arg/count resource correctness | Dispatch + root constants (constants not honored); draw/draw_indexed signature-only; compute-pso count-buffer dispatch exists. | Actual draw/draw_indexed replay verification; count=0 edge; multi-count; args-buffer mutation before execute; VB/IB binding for indirect draws. |
| Present/drawable lifetime | 60-frame present stress, resize, frame-latency waitable, descriptor mutation. | In-flight rendering during resize; native vs fallback present one-present assertion; producer→present ordering with live-present on/off; drawable acquire/present/commit/completion ID counting. |
| Metal command-buffer error instrumentation | **None.** winemetal exposes `error`/`logs`/`status` only. | `MTLCommandBufferDescriptor.errorOptions=EncoderExecutionStatus`; per-encoder `Faulted`/`Affected`/`Pending`; retained-references; bounded ring dump on error. |

---

## 4. Proposed probe additions and command matrix

Each probe below is **proposed**. None are written. Pass criteria, artifacts, and the
staging gate they unlock are listed per-probe. Naming follows the existing convention
(`probe_<area>/probe_<area>.cpp`, registered in `build-probes.sh`, `run-probes.sh`,
`compare-contract.py`, and `validate-probe-matrix.py`).

### 4.1 PROBE-A: `probe_descriptor_mutation_graphics` (M13.1 / M13.3)

**Why:** `probe_present_windowed` only mutates RTV/DSV/UAV on the **copy/clear** path
(no draw). AC6 mutates CBV/SRV/UAV descriptor tables between record and execute on the
**graphics** path; replay must bind the snapshot resources, not the mutated heap.

**Cases:**
1. Record draw with VS+PS root table (CBV+SRV-texture+UAV-buffer ranges), sample SRV into
   RTV. Before execute, overwrite every descriptor in the table with a different resource.
   Verify the RTV pixel matches the **snapshot** resource's content.
2. Same as (1) but mutate only the SRV texture slot. Verify draw sampled the snapshot.
3. Same as (1) but mutate only the UAV buffer slot, then have PS read+write UAV. Verify
   snapshot resource was written.
4. Multi-frame variant: record N command lists up front, recycle descriptor heap entries
   between lists, execute all, verify each list saw its own snapshot.

**Build/run:**
```bash
# After probe source is added under tools/d3d12-metal-sdk/probes/probe_descriptor_mutation_graphics/
# and registered in build-probes.sh + run-probes.sh + compare-contract.py + validate-probe-matrix.py:
tools/d3d12-metal-sdk/scripts/build-probes.sh
tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp \
  --dxmt-runtime "$HOME/.metalsharp/runtime/wine/lib/dxmt_m12" \
  --results-dir tools/d3d12-metal-sdk/results/m13-probe-bundle-<stamp> \
  --descriptor-mutation-graphics-only   # new flag mirroring --heap-aliasing-only
```

**Pass criteria:**
- JSON `pass:true`, every case `pass:true`.
- `graphics_srv_snapshot_verified`, `graphics_uav_snapshot_verified`,
  `multi_frame_snapshot_per_list_verified` all true.
- Reproduce the failure mode by toggling the snapshot path off in a throwaway branch and
  confirming the probe fails — proves the probe is sensitive to the fix.

**Artifacts:** `probe-descriptor-mutation-graphics-metalsharp.json`, the per-case pixel
values, the root-signature blob sizes, and the shader bytecodes used. Keep under the
result bundle dir; do not commit.

**Gate:** M13.1 (completion retention) + M13.3 (RTV/DSV/UAV snapshot) staging gate. AC6
retest is blocked until this probe exists and passes both with the fix on and (in a
throwaway run) fails with the fix off.

### 4.2 PROBE-B: `probe_argbuf_residency_graphics` (M13.2)

**Why:** Compute strict mode is gated by `DXMT_D3D12_STRICT_ARG_BUFFER_RESOURCES`;
graphics has no equivalent. Apple's `MTLRenderCommandEncoder.h` (cited in the roadmap)
requires `useResource` **before** any draw that may access a resource through an
argument buffer.

**Cases:**
1. Graphics PSO with a descriptor table of N SRV textures (N=8,16,32); draw a fullscreen
   quad sampling each; verify each RTV pixel matches the sampled texture. Toggling the
   graphics-argbuf residency blanket off should make this fail on hardware that enforces
   residency.
2. Mixed VS/PS descriptor tables (VS samples textures in vertex shader for vfetch-like
   pattern; PS samples textures normally).
3. Null/missing descriptor inside a table: descriptor is set to a null SRV; safety mode
   should neutralize the draw (skip or substitute) rather than submit an invalid GPU
   pointer. Report `null_descriptor_neutralized:true`.
4. AC6-corpus-inspired pressure: re-use the captured AC6 root-signature reflection JSON
   (already extracted under `tools/d3d12-metal-sdk/results/d3dmetal-root-pipeline-linkage-*`
   and `d3dmetal-root-signature-decode-*`) to build a synthetic root signature with the
   same table shapes; do not launch AC6.

**Build/run:**
```bash
tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp \
  --argbuf-residency-graphics-only
# Optionally with the new env on/off to prove sensitivity:
DXMT_D3D12_STRICT_ARG_BUFFER_RESOURCES=1 tools/d3d12-metal-sdk/scripts/run-probes.sh \
  --profile metalsharp --argbuf-residency-graphics-only
DXMT_D3D12_STRICT_ARG_BUFFER_RESOURCES=0 tools/d3d12-metal-sdk/scripts/run-probes.sh \
  --profile metalsharp --argbuf-residency-graphics-only
```

**Pass criteria:** All cases `pass:true` with the env on. With the env off, case 1
**may** still pass on hardware that does not enforce residency, but case 3 (null
neutralization) must reflect the chosen safety mode.

**Artifacts:** JSON with per-case residency-declaration counts, null-descriptor
neutralization counts, and the synthetic root-signature blob derived from the AC6 corpus.

**Gate:** M13.2 staging gate. Blocks AC6 retest.

### 4.3 PROBE-C: `probe_barriers_uav_aliasing_subresource` (M13.4)

**Why:** `probe_barriers_render_pass` and `probe_heap_aliasing` are single-resource,
single-list, single-queue, all-subresources. AC6 world load hammers cross-queue UAV and
subresource transitions.

**Cases:**
1. Compute queue UAV write → UAV barrier → **graphics** queue SRV read (cross-queue via
   shared fence / shared event). Verify read sees the write.
2. Compute UAV write → close encoder → graphics SRV read on the **same** direct queue
   without a UAV barrier. Verify whether the runtime inserts an implicit barrier or
   whether the read sees stale data (informational; documents current behavior).
3. Subresource transition: 4-mip texture; transition mip 0 to RTV, mip 1 to SRV, mip 2 to
   UAV, mip 3 to copy-dest, in one ResourceBarrier call; verify each mip retains its
   independent state.
4. Aliasing barrier between two placed **textures** on the same heap (currently probe is
   buffers only). Document whether the runtime supports texture aliasing; if not, the
   probe must report `texture_aliasing_supported:false` and still exit 0 if every other
   case passes.
5. Aliasing barrier between a placed buffer and a placed texture at the same offset (the
   dangerous mixed-alias case). Report status; do not require success if the runtime
   rejects it.
6. Split-list split-queue UAV: list A writes UAV on compute queue, list B reads it on
   direct queue, with only a queue-level signal/wait (no ResourceBarrier in B). Verify
   visibility.

**Build/run:**
```bash
tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp \
  --barriers-uav-aliasing-subresource-only
```

**Pass criteria:** Cases 1 and 3 `pass:true` required. Cases 2, 4, 5, 6 may report
unsupported status fields but the probe must still emit a complete JSON and exit 0 if no
required case failed.

**Artifacts:** JSON with per-case before/after state, subresource masks, fence signal/wait
serials, aliasing `pResourceBefore`/`pResourceAfter` handles.

**Gate:** M13.4 staging gate. Required before AC6 retest (the world-load copy/UAV/transition
hazard is a top-three candidate root cause).

### 4.4 PROBE-D: `probe_execute_indirect_draw_replay` (M13.1 indirect arg/count lifetime)

**Why:** `probe_command_replay` only verifies the dispatch path; draw and draw_indexed are
"signature-only" — they prove the signature is created, not that replay reads the args
buffer correctly.

**Cases:**
1. `ExecuteIndirect` with a draw signature; args buffer contains a real
   `D3D12_DRAW_ARGUMENTS`; verify vertex count drawn matches args.
2. Same with `D3D12_DRAW_INDEXED_ARGUMENTS` and a bound IB; verify indexed-vertex count
   drawn matches args.
3. Count buffer = 0 → zero draws submitted, no GPU fault.
4. Count buffer = N (N=4) → exactly N draws submitted; verify via UAV counter in the vertex
   shader.
5. Mutate the args buffer **between record and execute** (write new vertex count via
   `Map`/`Unmap` after `Close` but before `ExecuteCommandLists`). Document whether replay
   uses the snapshot or the live value; the snapshot path is required for AC6-style heap
   recycling.
6. Release-and-recreate the args buffer between record and execute (illegal in D3D12 but
   documents whether the runtime faults cleanly instead of corrupting GPU state).

**Build/run:**
```bash
tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp \
  --execute-indirect-draw-replay-only
```

**Pass criteria:** Cases 1, 2, 3, 4 `pass:true`. Case 5 must report whether snapshot or
live semantics are in effect (`args_buffer_lifetime_semantics:"snapshot"|"live"`). Case 6
must report `args_buffer_release_handling:"clean_fault"|"gpu_corruption"`.

**Artifacts:** JSON with per-case draw counts, args-buffer bytes, count-buffer value,
signature stride.

**Gate:** M13.1 indirect lifetime subgate. Required before AC6 retest.

### 4.5 PROBE-E: `probe_present_lifetime_resize` (M13.5)

**Why:** `probe_present_windowed` resizes **after** a fence wait. AC6 resizes the swapchain
while previous frames are still in flight; the magenta-frame symptom is consistent with
WindowServer sampling a dead drawable.

**Cases:**
1. Submit N render command buffers back-to-back (N=4) with no fence wait between submits,
   then `ResizeBuffers` mid-flight, then continue rendering. Verify no magenta frame, no
   crash, present count continues to advance. Requires the resize-recreate cage from M13.5.
2. Native present path vs fallback present path one-present assertion: encode exactly one
   present per frame and verify `GetLastPresentCount` increments by exactly 1 in both paths.
3. Producer→present ordering stress: in live-present mode (`DXMT_D3D12_PRESENT_LIVE=1`,
   if it exists), submit a slow producer (large compute dispatch) before present; verify
   the drawable texture written by the producer is the one presented.
4. Drawable lifetime counting: label every
   acquire/texture-extract/encode/present/commit/completion with a monotonically
   increasing ID; verify exactly one present per drawable and that the drawable object
   survives until its present command buffer reports `WMTCommandBufferStatusCompleted`.

**Build/run:**
```bash
tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp \
  --present-lifetime-resize-only
# Probe already supports M12_PRESENT_WINDOWED_* env knobs; reuse the same pattern:
M12_PRESENT_LIFETIME_STRESS_FRAMES=120 \
M12_PRESENT_RESIZE_MID_FLIGHT=1 \
  tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp --present-lifetime-resize-only
```

**Pass criteria:** All cases `pass:true`. Case 1 specifically requires that no
`MTLCommandBufferErrorDomain` appears in the probe stderr. Case 4 requires that the
drawable ID counter never reuses an ID for a still-in-flight drawable.

**Artifacts:** JSON with per-frame present counts, drawable IDs, completion status,
resize-during-inflight counter. Capture `harness.stderr` separately so a WindowServer
warning is searchable.

**Gate:** M13.5 staging gate. Required before AC6 retest (the magenta/deadlock symptom is
the highest-cost failure mode).

### 4.6 PROBE-F: `probe_command_buffer_error_instrumentation` (M13.0)

**Why:** The winemetal bridge does not currently expose
`MTLCommandBufferDescriptor`/`errorOptions`. M13.0 requires adding it. This probe proves
the bridge exposes the new surface and that the runtime populates per-encoder status.

**Pre-req:** winemetal must expose `MTLCommandQueue_commandBufferWithDescriptor` (or
equivalent) and `MTLCommandBufferEncoderInfo_enumerate` (or equivalent). This probe will
not build until those land — see `contracts/winemetal-bridge-contract.json` for the
required-PE-exports list (it must be extended).

**Cases:**
1. Create a command buffer via the descriptor path with
   `errorOptions = MTLCommandBufferErrorOptionEncoderExecutionStatus`. Encode a benign
   render pass. Commit, wait. Verify status `Completed` and no encoder reported
   `Faulted`/`Affected`/`Pending`.
2. Intentionally encode an invalid GPU operation that produces a per-encoder fault (e.g.
   draw without binding a required resource, or a shader that aborts). Verify the encoder
   info reports `Faulted` for the offending encoder and `Affected` for downstream
   encoders. (This case may need to be guarded by a runtime capability flag; if the host
   GPU/driver does not produce a per-encoder fault for the chosen trigger, mark the case
   `unsupported` rather than failed.)
3. `setRetainedReferences:YES`: encode a draw referencing a texture; release the CPU
   reference before commit; verify the GPU still sees the texture (no use-after-free).
   Repeat with `setRetainedReferences:NO` and confirm the runtime either faults cleanly
   or that the D3D12 completion-slot retention (PROBE-A) covers the gap.
4. Bounded ring dump on error: trigger a fault; verify the ring of last N command buffers,
   encoders, resources, and barriers is dumped to the probe JSON.

**Build/run:**
```bash
# After winemetal bridge additions land and contracts/winemetal-bridge-contract.json
# is extended:
python3 tools/d3d12-metal-sdk/scripts/check-winemetal-abi.py \
  --profile metalsharp \
  --dxmt-runtime "$HOME/.metalsharp/runtime/wine/lib/dxmt_m12" \
  --results-dir tools/d3d12-metal-sdk/results/m13-probe-bundle-<stamp>
tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp \
  --command-buffer-error-instrumentation-only
```

**Pass criteria:** ABI check passes for the new exports. Cases 1 and 3 `pass:true`. Case 2
must report either `per_encoder_fault_observed:true` or
`per_encoder_fault_unsupported:true` (host-dependent). Case 4 must dump a non-empty ring
on the triggered fault.

**Artifacts:** Updated `contracts/winemetal-bridge-contract.json` with the new required
PE exports; updated `scripts/check-winemetal-abi.py`; the probe JSON; the bounded ring
dump.

**Gate:** M13.0 staging gate. Required before AC6 retest — without this, the next
firmware-level deadlock will produce the same undiagnosable evidence as 2026-06-18/19.

---

## 5. Evidence bundle to assemble before requesting launch approval

A single result bundle (proposed path
`tools/d3d12-metal-sdk/results/m13-evidence-bundle-<stamp>/`) must contain, at minimum:

1. Git baseline proof
   - `git rev-parse HEAD`, `git status --branch --short`, `git ls-remote origin refs/heads/fix/m12-shader-probe-lab`
2. Backup manifest
   - Timestamped `/tmp/metalsharp-m13-<stamp>` backup root with hashes of every artifact
     touched (`vendor/dxmt/build-metalsharp-x64`, `~/.metalsharp/runtime/wine/lib/dxmt_m12`,
     AC6 game-local DLLs, backend binary, any `mscompatdb*`)
3. Build proof
   - `m12-dev.sh build-runtime` log, with explicit verification that `d3d11.dll` and
     `d3d10core.dll` are produced alongside `d3d12.dll` (no partial stage)
4. Hash sync
   - `hash-sync-and-mscompatdb.txt` proving build/runtime/game hashes match for the full
     Windows DLL set, build/runtime hashes match for Unix sidecars, `mscompatdb` empty
5. Backend proof
   - `app/src-rust/target/release/metalsharp-backend` SHA-256, `/status` ok on 9277 with
     `METALSHARP_PORT=9277`
6. winemetal ABI proof (extended for M13.0)
   - `winemetal-abi-metalsharp.json` from `check-winemetal-abi.py` including new
     MTLCommandBufferDescriptor/encoder-info exports
7. Preflight
   - `validate-contracts.py`, `validate-m12-pipeline-contract.py`,
     `preflight-runtime-layout.py`, `validate-shader-engine.py`,
     `validate-probe-matrix.py` all `[PASS]`
8. Probe matrix
   - All of §4.1–§4.6 passing in the bundle dir
   - Re-run of §4.1–§4.6 against a throwaway build with the M13 fix toggled off, showing
     they fail (sensitivity proof)
9. Sensitivity matrix
   - One-page table: probe × (fix on / fix off) × pass/fail
10. Dry-run launch env
   - `/diagnostics/pipeline/dry-run?appid=1888160&pipeline=m12` JSON, with the operator
     asserting the effective env invariants (no `mscompatdb` override, no-log/no-trace
     defaults, M12Core enabled, required off)

No `m12-ac6-launch-canonical.sh` invocation is part of this bundle. Launch is a separate
operator decision (runbook step 11), gated on this bundle plus explicit human approval.

---

## 6. Staging gates (ordered)

Gate ordering follows M13.0 → M13.1 → M13.2 → M13.3 → M13.4 → M13.5 → M13.6 (the roadmap's
"Next implementation order"). Each gate is a hard prerequisite for the next.

| Gate | Required probe(s) | Required runtime change | Blocks AC6 retest? |
|---|---|---|---|
| G-M13.0 winemetal bridge surface | PROBE-F cases 1, 3, 4 | winemetal exposes `MTLCommandBufferDescriptor`, `errorOptions=EncoderExecutionStatus`, `setRetainedReferences`, `MTLCommandBufferEncoderInfo`; ABI contract extended | **Yes** |
| G-M13.1 completion retention | PROBE-A case 4, PROBE-D cases 1–5 | `D3D12MetalCommandBufferCompletionSlot` retains D3D12 resources + WMT textures/buffers/drawables until completion; `DXMT_D3D12_REPLAY_RETENTION_DIAGNOSTICS` becomes a real retention container, not just logging | **Yes** |
| G-M13.2 graphics argbuf residency blanket | PROBE-B cases 1–4 | Generalize compute `ArgumentBufferResourceUse` into render+compute; per-draw `useResource` for graphics tables; null-descriptor neutralization | **Yes** |
| G-M13.3 RTV/DSV/UAV snapshot (already partly landed) | PROBE-A cases 1–3 | Snapshot already captured (commit `b09d154`); retention tied to G-M13.1 | **Yes** |
| G-M13.4 barriers + queue safety | PROBE-C cases 1, 3, 6 | Per-subresource tracking for diagnostics; UAV/aliasing barriers close encoders and emit explicit event/fence; optional queue serialization in safety mode | **Yes** |
| G-M13.5 present/drawable cage | PROBE-E cases 1–4 | Defer swapchain/backbuffer destruction until all in-flight command buffers that reference old backbuffers complete; drawable retained until present completion | **Yes** |
| G-M13.6 pre-launch evidence bundle | §5 bundle assembled | None (documentation/staging only) | **Yes** |

All gates default-off for live launches: any new env toggle (`DXMT_D3D12_RETAIN_RENDER_ATTACHMENTS`,
`DXMT_D3D12_STRICT_ARG_BUFFER_RESOURCES`, the proposed graphics-argbuf and present-cage
envs) may add **logging** only when off; **correctness behavior must be on by default**.
The roadmap is explicit on this: "make resource lifetime/residency/present ordering fixes
default behavior, not by optional safety toggles."

---

## 7. Specific file-level anchors an implementer should edit

The probe validator (`validate-probe-matrix.py`) and comparator
(`compare-contract.py`) will reject a probe that is not registered. Every new probe
must be wired into all of these. None of the edits below are done by this scouting
pass — they are the precise set of changes the next agent must make.

1. `tools/d3d12-metal-sdk/probes/probe_descriptor_mutation_graphics/probe_descriptor_mutation_graphics.cpp` (new file)
2. `tools/d3d12-metal-sdk/probes/probe_argbuf_residency_graphics/probe_argbuf_residency_graphics.cpp` (new file)
3. `tools/d3d12-metal-sdk/probes/probe_barriers_uav_aliasing_subresource/probe_barriers_uav_aliasing_subresource.cpp` (new file)
4. `tools/d3d12-metal-sdk/probes/probe_execute_indirect_draw_replay/probe_execute_indirect_draw_replay.cpp` (new file)
5. `tools/d3d12-metal-sdk/probes/probe_present_lifetime_resize/probe_present_lifetime_resize.cpp` (new file)
6. `tools/d3d12-metal-sdk/probes/probe_command_buffer_error_instrumentation/probe_command_buffer_error_instrumentation.cpp` (new file)
7. `tools/d3d12-metal-sdk/scripts/build-probes.sh` (lines ~83–177): add six `build_probe` blocks, mirroring `probe_present_windowed`'s `-lole32 -luuid -lgdi32` for any probe that creates a window
8. `tools/d3d12-metal-sdk/scripts/run-probes.sh`: add six `RUN_*` flags, six CLI options, six probe-exe paths, six `--*-only` blocks, and six execution blocks modeled on the existing `RUN_PRESENT_WINDOWED` block
9. `tools/d3d12-metal-sdk/scripts/compare-contract.py`: add the new probe names to `REQUIRED_PROBES` (or `OPTIONAL_PROBES` if you want them to start non-blocking)
10. `tools/d3d12-metal-sdk/scripts/validate-probe-matrix.py`: add the new groups to `REQUIRED_GROUPS`
11. `tools/d3d12-metal-sdk/contracts/winemetal-bridge-contract.json`: extend `required_pe_exports` and `required_unix_call_entries` for the M13.0 surface (PROBE-F pre-req)
12. `vendor/dxmt/src/winemetal/winemetal.h`, `winemetal_thunks.c`, `unix/winemetal_unix.c`: add the new bridge functions for PROBE-F
13. `vendor/dxmt/src/d3d12/d3d12_command_buffer_completion.hpp`: extend the slot to retain a vector of D3D12/WMT resources (PROBE-A/D pre-req)
14. `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp`: wire retention into every `LogReplayRetentionCandidate` site so the diagnostic becomes a real retain (PROBE-A/C/D pre-req)
15. `vendor/dxmt/src/dxmt/dxmt_presenter.cpp`: defer swapchain/backbuffer destruction across in-flight command buffers (PROBE-E pre-req)
16. `docs/optimization-roadmap/local-gates.md`: add the new probe names to the graphics-gates block once they are real

---

## 8. Open questions for the supervisor / implementer

1. **Does the Slice-2 commit hash `0573c54...` in the runbook still apply?** Current HEAD
   is `a909c83`, four commits past Slice-2 (`c3215ec`, `b09d154`, `a1d7fed`, `72a706d`).
   The runbook's "Required HEAD" line needs an operator update before any retest request,
   otherwise the verify-git step will hard-fail.
2. **Backend hash drift.** Runbook pins backend SHA
   `6fdface8...`. The `results/host-runtime-metalsharp.json` from 2026-06-19 confirms this
   matches, but if any of §4's runtime changes (G-M13.0, G-M13.1, G-M13.5) touch backend
   behavior (they mostly don't — they're in `vendor/dxmt/`), the hash must be re-pinned in
   the runbook.
3. **Can case 2 of PROBE-F (intentional per-encoder fault) be triggered reliably on the
   target Apple GPU?** If not, the case must be informational, not a gate. The roadmap's
   rejected list warns that `sudo sysctl wdt_enable=1` is not validated — do not lean on
   sysctl for this.
4. **Does the existing completion-slot ring (`m_metal_inflight`, default size 32) need to
   grow to retain more resources, or is per-slot resource retention sufficient?** The
   inflight cap and the retention container size should be the same number.
5. **Texture aliasing support.** PROBE-C case 4 may reveal that placed-texture aliasing is
   unsupported by the current runtime. If so, document it in
   `contracts/risky-stub-ledger.json` and `contracts/unsupported-api-ledger.json`, do not
   silently pass.

---

## 9. Quick-reference command set for a full M13 offline pass

Assuming every probe from §4 is implemented, the full pre-launch offline pass is:

```bash
# 0. Verify Slice-2 / M13 baseline (read-only)
git -C /Users/alexmondello/metalsharp-m12-lab rev-parse HEAD
git -C /Users/alexmondello/metalsharp-m12-lab status --branch --short
find "$HOME/.metalsharp/runtime/wine" -name 'mscompatdb*' -print  # must be empty

# 1. Backup mutable state (no deletion)
BACKUP=/tmp/metalsharp-m13-backup-$(date +%Y%m%d-%H%M%S)
mkdir -p "$BACKUP"
# (runbook step 2 — full backup script TBD)

# 2. Rebuild + stage (with explicit gates, no launch)
tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime
tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime

# 3. Extended winemetal ABI proof (M13.0 surface)
python3 tools/d3d12-metal-sdk/scripts/check-winemetal-abi.py \
  --profile metalsharp \
  --dxmt-runtime "$HOME/.metalsharp/runtime/wine/lib/dxmt_m12" \
  --results-dir tools/d3d12-metal-sdk/results/m13-evidence-bundle-<stamp>

# 4. Full probe matrix including the six new probes
M12_DEV_RESULTS_DIR=tools/d3d12-metal-sdk/results/m13-evidence-bundle-<stamp> \
tools/d3d12-metal-sdk/scripts/m12-dev.sh probes \
  --loader --agility --caps --dxgi --resources --queues --descriptors \
  --shaders --dxil-semantics --shader-corpus --sm66-capabilities \
  --wave-ops --reflection-abi --graphics-pso --compute-pso \
  --command-replay --barriers-render-pass --resource-views-formats \
  --heap-aliasing --mini --winemetal-abi \
  --descriptor-mutation-graphics \
  --argbuf-residency-graphics \
  --barriers-uav-aliasing-subresource \
  --execute-indirect-draw-replay \
  --present-lifetime-resize \
  --command-buffer-error-instrumentation \
  --windowed-present

# 5. Contract + layout + shader-engine gates
M12_DEV_RESULTS_DIR=tools/d3d12-metal-sdk/results/m13-evidence-bundle-<stamp> \
tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight

# 6. Comparator (decides pass/fail across the whole bundle)
python3 tools/d3d12-metal-sdk/scripts/compare-contract.py \
  --profile metalsharp \
  --results-dir tools/d3d12-metal-sdk/results/m13-evidence-bundle-<stamp>

# 7. Probe-matrix validator (CI-side check, run locally)
python3 tools/d3d12-metal-sdk/scripts/validate-probe-matrix.py

# 8. Sensitivity proof (throwaway branch, fix toggled off)
# (Implementer-only: re-run step 4 on a branch where the M13 fixes are reverted one
#  at a time, confirming each probe in §4 fails. Do NOT stage this runtime.)

# 9. Dry-run launch env (no launch)
curl -sS --max-time 20 \
  'http://127.0.0.1:9277/diagnostics/pipeline/dry-run?appid=1888160&pipeline=m12' \
  > tools/d3d12-metal-sdk/results/m13-evidence-bundle-<stamp>/dry-run.json
```

Step 11 of the runbook (canonical launch) is intentionally **not** in this list. It
requires explicit operator approval after this bundle is reviewed.

---

## 10. Summary

- The descriptor-snapshot correctness fix (M13.3) is real and probe-verified.
- The completion-retention (M13.1), graphics argbuf residency (M13.2), barrier/subresource
  expansion (M13.4), present/drawable cage (M13.5), and Metal command-buffer error
  instrumentation (M13.0) are **not** landed in `vendor/dxmt/src/` at HEAD `a909c83`,
  despite the roadmap marking them "Implemented in current WIP". The landed code is
  diagnostic logging plus an optional render-attachment `useResource` declaration.
- Six new probes are required before any AC6 Continue retest request. The current probe
  matrix does not exercise graphics descriptor-table mutation, graphics argbuf residency,
  cross-queue UAV, subresource barriers, texture aliasing, indirect draw replay, in-flight
  resize, or Metal command-buffer error status.
- The Slice-2 runbook constraints (git hash, runtime hashes, `mscompatdb` quarantine,
  `METALSHARP_PORT`, no-log defaults, no kill, no launch, no commit of generated
  artifacts) govern every step of this strategy.
- Launch remains explicitly unsafe until §5's evidence bundle is assembled and reviewed,
  including the sensitivity proof in step 8.
