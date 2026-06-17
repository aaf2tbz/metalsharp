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

Status: complete.

Implemented evidence:

- Added `M12CORE_FEATURE_COMMAND_STREAM_DESCRIPTORS` with `M12CORE_BUILD_ID_HIGH = 0x00000010`.
- Added C/POD `M12CoreCommandStreamDesc` and `M12CoreCommandStreamSummary`.
- Added `m12core_validate_command_stream`.
- Added winemetal PE/unix bridge on append-only unixcall `155`.
- Added bounded post-execution `M12_COMMAND_STREAM_SHADOW` diagnostics.
- Runtime build passed: `./tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`.
- Probe build passed: `./tools/d3d12-metal-sdk/scripts/build-probes.sh`.
- Formatting passed: `clang-format --dry-run --Werror ...`.
- Autoreview passed after aligning swapchain-touch semantics to `swapchain_touched_count`.
- Runtime staged and AC6 dry-run passed: `tools/d3d12-metal-sdk/results/m12-phase9-slice2-dryrun-20260617-161032/dry-run-armored-core-vi-1888160.json`.

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

Status: complete.

Implemented evidence:

- Added `M12CORE_FEATURE_RENDER_PASS_HAZARD_PLANNING` with `M12CORE_BUILD_ID_HIGH = 0x00000011`.
- Added scalar C/POD `M12CoreRenderPassPlanDesc` and `M12CoreRenderPassPlanSummary`.
- Added `m12core_plan_render_pass` for native render-pass classification, hazard scoring, attachment counts, transition counts, descriptor pressure, expected flags, and drift flags.
- Added winemetal PE/unix bridge on append-only unixcall `156`.
- Added `resource_barrier_count` to `D3D12CommandStreamStats`.
- Added bounded post-execution `M12_RENDER_PASS_PLAN` diagnostics beside Slice 2 command-stream shadow diagnostics.
- Runtime build passed: `./tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`.
- Probe build passed: `./tools/d3d12-metal-sdk/scripts/build-probes.sh`.
- Formatting passed: `clang-format --dry-run --Werror ...` for touched Slice 3 files.
- Autoreview returned no findings for ABI/table/fallback/execution-semantics safety.
- Runtime staged to `~/.metalsharp/runtime/wine/lib/dxmt_m12` with `tools/d3d12-metal-sdk/results/stage-runtime-metalsharp.json`.
- `probe_command_replay` passed under staged `dxmt_m12` runtime: `tools/d3d12-metal-sdk/results/probe-command-replay-metalsharp.json`.
- `probe_barriers_render_pass` passed under staged `dxmt_m12` runtime: `tools/d3d12-metal-sdk/results/probe-barriers-render-pass-metalsharp.json`.
- M12 detection probe passed with feature flags `0x00000000000003ff`, core feature flags `0x0001ffff`, and build high `0x00000011`: `tools/d3d12-metal-sdk/results/probe-m12-detection-metalsharp.json`.
- Native render-pass shadow bridge exercised with `DXMT_M12CORE_ENABLE=1`; captured `M12_RENDER_PASS_PLAN ... drift=0x0` lines in `tools/d3d12-metal-sdk/results/probe-barriers-render-pass-m12core-enabled.log`.
- Backend dry-run matrix passed for all five games: `tools/d3d12-metal-sdk/results/m12-phase9-slice3-dryrun-20260617-162500/summary.json`.

Pre-implementation discrepancy check:

- RT/DSV final state is available from `ReplayState::rt_count`, `has_dsv`, `rt_handles[]`, and `dsv_handle`.
- Swapchain target/touch state is available from `HasSwapchainRenderTarget()` and `swapchain_touched_count`.
- Descriptor formats can be summarized as DXGI integer values from `D3D12Descriptor::rtv/dsv`, falling back to zero when unavailable.
- Barrier count is not currently in `D3D12CommandStreamStats`; Slice 3 must add scalar `resource_barrier_count`.
- No Metal encoder/resource/COM handles need to cross ABI; render-pass/hazard descriptor can be scalar-only.

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

## Slice 4 — Core-owned presenter planning + gated native present/blit

Status: complete.

Implemented evidence:

- Added `M12CORE_FEATURE_PRESENT_EXECUTE_PLANNING` with `M12CORE_BUILD_ID_HIGH = 0x00000012`.
- Added scalar C/POD `M12CorePresentExecuteDesc` and `M12CorePresentExecuteSummary`; these carry only counts, flags, dimensions, DXGI format integers, synthetic keys, and queue/work counters.
- Added `m12core_plan_present_execute` for native support/fallback classification of the narrow raw layer/drawable blit path.
- Added append-only winemetal unixcalls `157` (`WMTM12CorePlanPresentExecute`) and `158` (`WMTM12CoreExecutePresentBlit`).
- Added default-off `DXMT_M12CORE_PRESENT_EXECUTE=1` gate; gate-off plans report fallback reason `GATE_DISABLED` and PE path executes unchanged.
- Added gated native raw blit/present execution in winemetal native side only after libm12core scalar plan reports supported; unsupported/native-failure paths fallback whole-present to the existing PE blit+present path.
- Runtime build passed after implementation and log-rate-limit fix: `./tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`.
- Probe build passed: `./tools/d3d12-metal-sdk/scripts/build-probes.sh`.
- Formatting passed: `clang-format --dry-run --Werror ...` for touched Slice 4 files.
- Autoreview found one info-level hot-path log issue; fixed by rate-limiting `M12_PRESENT_EXECUTE_NATIVE`, and re-review returned no findings.
- Runtime staged to `~/.metalsharp/runtime/wine/lib/dxmt_m12`: `tools/d3d12-metal-sdk/results/stage-runtime-metalsharp.json`.
- M12 detection passed with feature flags `0x00000000000007ff`, core feature flags `0x0003ffff`, and build high `0x00000012`: `tools/d3d12-metal-sdk/results/probe-m12-detection-slice4-metalsharp.log`.
- Gate-off forced-raw-blit present probe passed and logged `M12_PRESENT_EXECUTE_PLAN` with `fallback=1` and no `M12_PRESENT_EXECUTE_NATIVE`: `tools/d3d12-metal-sdk/results/probe-present-windowed-slice4-gate-off.log`.
- Gate-on forced-raw-blit present probe passed and logged `M12_PRESENT_EXECUTE_PLAN supported=1 fallback=0` plus `M12_PRESENT_EXECUTE_NATIVE`: `tools/d3d12-metal-sdk/results/probe-present-windowed-slice4-gate-on.log`.
- Backend dry-run matrix passed for all five games: `tools/d3d12-metal-sdk/results/m12-phase9-slice4-dryrun-20260617-164836/summary.json`.

Pre-implementation discrepancy check:

- Existing Slice 1 present planning is scalar-only and logs `M12_PRESENT_PLAN`; it does not make an execution/fallback decision.
- Current `Present1` has two presenter paths: presenter encode path and raw layer/drawable blit path. Only the raw layer/drawable blit path has a small enough surface for a first gated native execution seam.
- True `libm12core` ABI still cannot receive `WMT::`, COM, Objective-C, Metal, drawable, command-buffer, or texture handles.
- The executable native seam therefore must split responsibilities: `libm12core` owns a scalar present-execution decision descriptor; winemetal native unixcall may receive existing `obj_handle_t` Metal handles and perform the raw blit/present only when the scalar native decision says supported and the env gate is enabled.
- Unsupported paths must fallback whole-present to the current PE code path: presenter path, live-present, readback, missing source/drawable, unsupported format, zero dimensions, disabled gate, or native unixcall failure.

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
