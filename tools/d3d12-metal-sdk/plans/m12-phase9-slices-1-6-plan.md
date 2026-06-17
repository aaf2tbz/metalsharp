# M12/libm12core Phase 9 Slices 1-6 Plan

## Objective

Complete Phase 9 slices 1-6 before slice 7. Slice 7 is intentionally held for user-guided visual confirmation. Each slice must be planned before implementation, must identify discrepancies to rule out, and must leave evidence that maps requirements to artifacts.

## Global rules

1. `libm12core` ABI remains C/POD-only: no STL/C++ objects, COM interfaces, Objective-C/Metal objects, exceptions, virtuals, or ownership semantics across PE/native.
2. PE `d3d12.dll`/`dxgi*.dll` remain Windows ABI shims; `libm12core` becomes the native D3D12 execution core one ownership domain at a time.
3. Every migration seam keeps fallback if `libm12core` is disabled, unavailable, older, or returns invalid output.
4. No raw D3DMetal cache payloads, extracted metallibs, DXBC blobs, or large corpora are committed.
5. Preserve live shader caches unless explicitly validating source-compile fixes.
6. Before any launch, run backend dry-run first. Slice 7 visual confirmation is not performed until user approval.
7. Commit slice-by-slice; run autoreview after substantive source changes.

## Slice 1 — Native planning/classification foundation

Status: complete.

Implemented evidence:

- `0fec647 feat: plan m12 presents in native core`
- `b4754d4 feat: plan m12 command replays in native core`
- `8f63371 docs: record m12 phase9 completion audit`
- `M12CORE_FEATURE_PRESENT_PLANNING`
- `M12CORE_FEATURE_REPLAY_PLANNING`
- unixcalls `153` and `154`
- bounded `M12_PRESENT_PLAN` and `M12_REPLAY_PLAN` diagnostics

Discrepancies ruled out:

- No Metal or COM handles cross the ABI.
- No command execution or presentation behavior moved.
- Fallback is silent when native core is unavailable.

## Slice 2 — Core-owned execution descriptors, shadow-only

Plan before implementation:

- Add a stable C/POD command-stream execution descriptor in `m12core.h`.
- Add `m12core_validate_command_stream` to classify expected execution shape from scalar command stats, final render state, and swapchain-touch metadata.
- Add winemetal PE/unix bridge on the next append-only unixcall ID.
- PE side builds the descriptor from `D3D12CommandStreamStats` and final `ReplayState` after command execution, then logs bounded `M12_COMMAND_STREAM_SHADOW` diagnostics.
- This is shadow-only: PE/DXMT still performs all command decoding, encoder work, resource barriers, command-buffer commits, and synchronization.

Discrepancies to rule out before coding:

- Existing `D3D12CommandStreamStats` has command/draw/dispatch/clear/setup counts: yes.
- Existing `ReplayState` exposes final RT/DSV/descriptor-heap/swapchain-touch state: yes.
- Need no Metal object handles or PE object pointers in the descriptor: achievable by passing counts/flags only.
- Need a bounded log budget before unixcall to avoid hot-path spam: use existing `TakeLogBudget` pattern.

Validation gates:

- Runtime build passes.
- Probe build passes if detection constants change.
- `clang-format --dry-run --Werror` passes for touched C/C++ files.
- Autoreview clean for ABI/table/fallback/execution-semantics safety.
- Stage runtime and dry-run at least AC6 after source changes.

Exit criteria:

- Native core owns the command-stream execution descriptor classification, but PE execution remains unchanged.
- Logs include bounded `M12_COMMAND_STREAM_SHADOW` only when native core is available.

## Slice 3 — Core-owned render-pass and hazard planning

Plan before implementation:

- Add POD render-pass/hazard planning ABI keyed by scalar attachment counts/formats, clear/draw counts, swapchain target flag, DSV flag, and barrier count.
- PE side sends final render-pass summaries to native core at encoder boundary points, initially shadow-only.
- Native core returns expected render-pass classification and hazard score.
- PE compares native plan against its own current encoder decisions and logs drift without changing execution.

Discrepancies to rule out before coding:

- Need attachment format capture without passing descriptors: use DXGI format integers already available from state/descriptors.
- Need barrier count in stats: add count to `D3D12CommandStreamStats` if absent.
- Need no render encoder handle across ABI: descriptor is scalar only.

Validation gates:

- Runtime build, formatting, autoreview.
- `probe_barriers_render_pass` and `probe_command_replay`.
- Backend dry-run AC6/Elden Ring/Subnautica 2.

Exit criteria:

- `libm12core` is authoritative for shadow render-pass/hazard classification.
- No PE execution behavior changes.

## Slice 4 — Core-owned presenter planning + gated present command plan

Plan before implementation:

- Add explicit present-execution gate env var, default off.
- First implement native-owned present command planning, not native Metal execution, for one narrow path: source texture exists, drawable path exists, raw blit classification, no live-present, no MetalFX, no readback diagnostic.
- Native core returns `EXECUTE_SUPPORTED` or `FALLBACK_REASON` based on scalar descriptors.
- PE logs and still executes existing path unless the gate is enabled and the plan is supported; initial gate may remain shadow-only if ABI cannot honestly own native handles yet.

Discrepancies to rule out before coding:

- Cannot pass drawable/texture objects across PE/native ABI.
- True native present execution requires native-owned opaque handles created inside unix/native side, not PE C++ handles.
- If opaque native handles are not already available, Slice 4 completion is a gated command-plan/fallback seam rather than actual Metal present.

Validation gates:

- Runtime build, formatting, autoreview.
- `probe_present_windowed` if safe.
- Backend dry-run all five games.
- No visual launch until user explicitly approves.

Exit criteria:

- Native core owns present execution eligibility/fallback classification for the narrow path.
- Existing presenter/blit execution remains safe and fallback-controlled.

## Slice 5 — Core-owned command replay for mini/probe path

Plan before implementation:

- Add explicit replay execution gate env var, default off.
- Add native-core command support table for a constrained mini/probe subset: clear RTV, OMSetRenderTargets, viewport/scissor, PSO/root signature, VB/IB, draw/draw-indexed.
- PE sends command stream metadata and asks native core if the list is eligible for native replay.
- Until native-owned handles are proven, native core only returns eligibility/unsupported reason; PE path remains the executor.
- If native-owned handles are proven available, enable actual execution only for a mini/probe path and only behind the gate.

Discrepancies to rule out before coding:

- Existing command stream contains PE command structs and pointers; cannot hand entire stream to native core for execution without a POD translation layer.
- Need either translated POD command packets or native-owned handles before actual native replay.
- Unsupported command lists must fallback whole-list, not partial mixed execution.

Validation gates:

- Runtime build, formatting, autoreview.
- Mini/probe command replay tests.
- Backend dry-run all five games.

Exit criteria:

- Native core owns replay eligibility/support classification for mini/probe command lists, with a default-off gate for future execution.

## Slice 6 — Game shadow + opt-in native execution readiness for one bounded title

Plan before implementation:

- Do not perform visual Slice 7 confirmation.
- Stage runtime and dry-run first.
- Use AC6 as first bounded title only if user approval is explicit; otherwise stop at dry-run and readiness artifacts.
- Collect shadow logs for slices 2-5: command-stream descriptor, render-pass/hazard plan, present eligibility, replay eligibility.
- If bounded launch is approved, run only in shadow/default PE execution unless a prior gate has been proven by probes.

Discrepancies to rule out before coding:

- Current objective asks to wait on Slice 7 for visual confirmation, not necessarily to launch games blindly.
- A bounded launch without visual confirmation can prove log/runtime stability but not final visual correctness.
- Any native execution gate must remain default off unless probe evidence proves it.

Validation gates:

- Runtime build, formatting, autoreview.
- Stage runtime.
- Dry-run all five games.
- If explicitly approved: bounded AC6 launch with logs, no visual pass/fail claim.

Exit criteria:

- Slices 1-5 are implemented and staged.
- Slice 6 readiness/shadow evidence exists for game matrix dry-runs and, if approved, one bounded non-visual AC6 run.
- Slice 7 remains blocked awaiting user visual confirmation.
