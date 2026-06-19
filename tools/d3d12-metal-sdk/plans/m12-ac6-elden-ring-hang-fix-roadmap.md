# M12 AC6 / Elden Ring Hang-Fix Roadmap

Scope: runtime hang fixes only. This does not replace the Slice-2 staging/launch runbook. For rebuild, staging, backend restart, runtime verification, and launch procedure, follow `tools/d3d12-metal-sdk/plans/m12-ac6-slice2-stage-launch-runbook.md` first.

Target hangs:

- Armored Core VI: hangs after clicking `Continue` into the real scene.
- Elden Ring: hangs after selecting character creator `Type A` / `Type B`.

Known baseline facts:

- Stable launch baseline exists and must be preserved before each phase.
- Shader compile/cache hypothesis is retired for the known hang: previous LLDB showed no active shader compiler path.
- Primary symptom was `GXRenderThread` blocked inside Metal command-buffer acquisition:
  - `AGXG16GFamilyCommandQueue commandBuffer`
  - `winemetal.so _MTLCommandQueue_commandBuffer`
- Simple queue depth increase alone was insufficient: queue 16 and queue 32 still reproduced the hang.
- Slice 3A queue-counter instrumentation regressed launch and is retired.
- Future diagnostics should be external-first: no production hot-path counters/logging unless explicitly approved.
- Debugger must not be part of the launch chain. Attach externally only after the game reaches/reproduces the hang.

Validation keyword:

- Use keyword `turtle` to mean: run the external `xcodedbg` hang ping after a phase has been rebuilt, staged with the baseline runbook, launched correctly, and manually driven to the hang point.
- `turtle` is not a launch command and not a staging command. It is the post-reproduction external debugger/snapshot step.

Global phase gates:

1. Start from the stable Slice-2 baseline or restore from `/Users/alexmondello/slice2baseline/latest` if needed.
2. Apply only the phase-specific code changes.
3. Rebuild/stage/verify/launch using the Slice-2 runbook.
4. Launch only with explicit operator approval.
5. Manually reproduce:
   - AC6: click `Continue` and wait for hang.
   - Elden Ring: choose `Type A` or `Type B` and wait for hang.
6. After reproduction, run `turtle` external `xcodedbg` ping.
7. Do not proceed to the next phase until the phase has either:
   - fixed the hang in both target games, or
   - produced a clear external hang signature that justifies the next phase.

## Phase 1 — External hang fingerprint and zero-regression guardrails

Goal: prove the exact post-baseline hang shape for AC6 and Elden Ring without adding hot-path runtime instrumentation, then establish the smallest safe fix boundary.

Implementation work:

- No queue counters.
- No trace-heavy launch env.
- No custom launch route.
- Add only low-risk guardrails if needed, such as compile-time/off-by-default diagnostic helpers that are not active in normal launch.
- Review command-buffer creation/commit/wait paths in:
  - `vendor/dxmt/src/dxmt/dxmt_command_queue.*`
  - `vendor/dxmt/src/d3d12/d3d12_command_queue.*`
  - `vendor/dxmt/src/d3d12/d3d12_fence.*`
  - `vendor/dxmt/src/d3d12/d3d12_swapchain.*`
  - `vendor/dxmt/src/winemetal/**`
- Build a written call-path map from D3D12 submit/present/fence wait to Metal command buffer acquisition.

Validation:

- Rebuild/stage/launch via baseline runbook.
- AC6: reproduce hang after `Continue`.
- Elden Ring: reproduce hang after `Type A` or `Type B`.
- Run `turtle` external `xcodedbg` ping for each hung process.

`turtle` evidence to capture:

- Thread list and backtraces.
- Whether render thread is blocked in `_MTLCommandQueue_commandBuffer` again.
- Whether any thread is draining/completing Metal command buffers.
- Whether any thread is waiting on D3D12 fence/condition variable.
- Whether present/drawable acquisition is involved.
- Whether both games converge on the same blocked call path.

Exit criteria:

- AC6 and Elden Ring have comparable hang fingerprints, or differences are documented.
- Exact source files/functions for Phase-2 fix are identified.
- No new launch regression.
- No `mscompatdb` regression.
- No trace/log-heavy workload used for the visual hang reproduction.

## Phase 2 — Command-buffer lifecycle/backpressure fix

Goal: fix the likely command-buffer acquisition deadlock/backpressure path without reintroducing Slice-3A-style hot-path counters.

Hypothesis:

The real-scene/character-creator transition creates a burst of submit/present/fence work that exhausts or wedges Metal command-buffer availability. The render thread then blocks inside `-[MTLCommandQueue commandBuffer]`, implying command buffers are not completing/releasing fast enough or a wait/present cycle prevents completion progress.

Implementation direction:

- Make command-buffer lifecycle ownership explicit and minimal:
  - creation
  - retention
  - commit
  - completion handler
  - release/reset
- Ensure every created command buffer reaches one of:
  - committed with completion cleanup
  - cancelled/abandoned with cleanup on error path
- Ensure completed handlers cannot block on D3D12 locks needed by submit/present threads.
- Add conservative, production-safe backpressure around command-buffer acquisition:
  - avoid unbounded creation bursts;
  - do not block while holding global queue/device/fence locks;
  - yield/drain outside critical sections before asking Metal for another command buffer.
- Audit fence signal/wait ordering:
  - D3D12 fence waits must not prevent Metal completion callbacks from running;
  - command-buffer completion must be able to advance fence state without lock inversion.
- Audit present path:
  - drawable acquisition/present must not create a cycle where present waits for queue progress while queue progress waits for present/drawable state.

Non-goals:

- No general queue-counter framework.
- No heavy per-submit logging.
- No shader compiler/cache changes.
- No source-compile forcing.
- No restart/prefix reset as part of the fix.

Validation:

- Rebuild/stage/launch via baseline runbook.
- AC6: click `Continue`.
- Elden Ring: choose `Type A` / `Type B`.
- If either still hangs, run `turtle` external `xcodedbg` ping immediately after hang.

Success criteria:

- Best case: AC6 enters the real scene and Elden Ring enters character creator without hang.
- If not fixed: `turtle` must show movement away from `commandBuffer` acquisition blockage or identify the next blocked cycle.
- No launch regression.
- No visible regression before the hang point.
- No hot-path diagnostic regression comparable to Slice 3A.

Rollback criteria:

- Game no longer launches visually.
- `mscompatdb` loads again.
- Backend/runbook verification fails.
- Hang occurs earlier than before.
- New code requires trace/log env to function.

## Phase 2 result — diagnostic success, not a fix

Status: complete as a diagnostic phase; failed as a shippable fix.

Evidence:

- AC6 Phase 1 turtle: `/tmp/m12-turtle-ac6-20260618-203548/summary.md`
  - `GXRenderThread` blocked in Metal `commandBuffer` acquisition.
- Elden Ring Phase 1 turtle: `/tmp/m12-turtle-elden-ring-20260618-205223/summary.md`
  - same command-buffer acquisition signature, plus separate active DXIL→MSL lowering work.
- AC6 Phase 2 turtle: `/tmp/m12-turtle-ac6-phase2-20260618-211547/summary.md`
  - hang moved from opaque Metal `commandBuffer()` to explicit DXMT-side `waitUntilCompleted` on a locally tracked command-buffer slot.
- AC6 Phase 2b turtle: `/tmp/m12-turtle-ac6-phase2b-20260618-213811/summary.md`
  - bounded wait/release fallback returned the hang to opaque Metal `commandBuffer()` acquisition.

Conclusion:

- Queue depth alone is insufficient.
- Bounded polling plus dropping local tracking is insufficient and should not be continued as the fix.
- The useful part of Phase 2 is the explicit lifecycle boundary: when we hold the oldest in-flight command buffer and wait, the hang becomes attributable to completion/forward-progress rather than hidden inside `-[MTLCommandQueue commandBuffer]`.

Research notes used for Phase 3:

- Local Apple SDK `MTLCommandBuffer.h` documents the relevant lifecycle states: `NotEnqueued`, `Enqueued`, `Committed`, `Scheduled`, `Completed`, `Error`.
- `addCompletedHandler:` is the native non-blocking way to receive command-buffer completion; `waitUntilCompleted` is synchronous and should be a backpressure fallback, not the primary progress mechanism.
- Local Apple SDK `MTLCommandQueue.h` documents `MTLCommandQueueDescriptor.maxCommandBufferCount` as the upper bound on uncompleted command buffers. The current winemetal wrapper uses `newCommandQueueWithMaxCommandBufferCount`, so a `commandBuffer()` stall is consistent with exceeding uncompleted-buffer capacity.
- Local web search was unavailable during planning; SDK headers were used as source-of-truth for Metal API semantics.

## Phase 3 — Completion/fence/present forward-progress hardening

Goal: harden the remaining forward-progress path after Phase 2, especially command-buffer completion observation, D3D12 fence waits, present/drawable interactions, and scene-transition command-buffer bursts.

Hypothesis:

If command-buffer lifecycle is correct but hangs remain, the next likely issue is a forward-progress cycle between D3D12 fence waits, swapchain present, drawable availability, and Metal completion delivery. AC6 real-scene transition and Elden Ring character creator may both hit this cycle under heavier present/resource churn.

Implementation direction:

- Separate queue submission progress from present/drawable progress:
  - queue completion should advance fences even if present/drawable state is delayed;
  - present should not hold queue locks while waiting on drawable/display state.
- Add lock-order rules and assertions in debug-only or off-by-default form:
  - no queue lock while waiting on Metal drawable;
  - no fence lock while acquiring Metal command buffer;
  - no callback path re-enters a lock held by submit/present.
- Add minimal forward-progress fallback, if needed:
  - completion-aware drain before blocking acquisition;
  - fail-soft retry path for command-buffer acquisition stalls;
  - avoid indefinite wait under locks.
- Keep all diagnostics off by default and outside the normal visual launch path.
- Preserve PE fallback and native/M12Core gates.

### Phase 3a — Completion-aware command-buffer slots

Purpose:

- Replace Phase 2b's "bounded wait then forget" behavior with real Metal completion observation.
- Keep the explicit Phase 2 backpressure point so any remaining hang is captured at a known, owned command buffer rather than hidden inside the next `commandBuffer()` allocation.
- Avoid always-on hot-path counters; use completion handlers and only limited warning logs when a slot forces a synchronous wait.

Source changes:

- Add a winemetal ABI hook:
  - `MTLCommandBuffer_addCompletedSignal(cmdbuf, serial_ptr, completed_ptr, status_ptr, value)`
  - unix side installs `addCompletedHandler:` and atomically writes completion serial/status into caller-owned slot state.
  - callback is serial-guarded so a late handler cannot complete a reused slot.
  - appended at unix-call code `171`; no existing unix-call numbers are changed.
- Add shared D3D12 helper:
  - `vendor/dxmt/src/d3d12/d3d12_command_buffer_completion.hpp`
  - owns per-slot command-buffer reference, serial, completion serial, and completion status.
- Update D3D12 queue slots:
  - `ExecuteCommandLists`, `Signal`, and `Wait` arm completion handlers before `commit()`.
  - `AcquireMetalCommandBuffer()` drains the exact oldest slot first.
  - pending slots are not released unless completed or synchronously waited.
- Update present slots similarly:
  - presenter/no-drawable/blit present command buffers are tracked before commit.
  - present drain uses completion-handler state first, then synchronous wait only if the slot is still pending.

Expected result:

- If completion handlers fire normally, slot cleanup occurs without `waitUntilCompleted`, reducing pressure before the next Metal allocation.
- If Metal/AGX still stops making progress, the next turtle should land at the explicit completion-tracked wait again, not at an unowned `commandBuffer()` stall.
- If it still lands inside `commandBuffer()`, the remaining issue is earlier than D3D12 queue/present slot accounting, likely inside Metal queue submit/drawable path or another command queue not covered by Phase 3a.

Validation after rebuild/stage/backend restart:

1. AC6 first, with explicit launch approval.
2. Manual `Continue` reproduction.
3. If hung, `turtle` immediately.
4. Do not launch Elden Ring unless explicitly approved due prior PC crash/stress risk.

Validation:

- Rebuild/stage/launch via baseline runbook.
- AC6: click `Continue`; confirm real scene progresses.
- Elden Ring: choose `Type A` and `Type B`; confirm character creator progresses.
- Run `turtle` if either game hangs or stalls.
- If both games pass, run one additional clean no-log launch for each target to confirm repeatability, with explicit operator approval.

Success criteria:

- AC6 no longer hangs after `Continue`.
- Elden Ring no longer hangs after `Type A` / `Type B`.
- Repeat run confirms the fix is not a one-off.
- No staging/baseline regression.
- No mscompatdb regression.
- No dependency on debug/tracing env.

Final evidence package:

- Commit(s) containing only the runtime fix and minimal docs/tests.
- Before/after `turtle` summaries for AC6 and Elden Ring.
- Baseline runbook verification output for each validation run.
- Launch response summaries showing canonical M12 no-log route.
- Operator visual confirmation notes.

## `turtle` external xcodedbg protocol

Use only after the game is already launched normally and the hang is reproduced.

Required behavior:

- Attach from outside the process.
- Do not alter launch env.
- Do not relaunch under debugger.
- Do not kill the process unless explicitly approved after capture.
- Capture enough thread/backtrace state to compare against prior hang signatures.

Conceptual command shape:

```bash
# Pseudocode. Use the real xcodedbg wrapper/tooling available in the environment.
turtle <pid-or-process-match> --snapshot --threads --bt-all --output /tmp/m12-turtle-<game>-<timestamp>
```

Minimum output:

- process PID/name
- timestamp
- all thread backtraces
- highlighted render thread / GXRenderThread if present
- Metal/AGX frames
- D3D12/DXMT/winemetal frames
- fence/present/wait frames

Interpretation rules:

- If blocked in `AGXG16GFamilyCommandQueue commandBuffer`, Phase 2 remains the primary path.
- If blocked in drawable/present acquisition, Phase 3 present/drawable split becomes primary.
- If blocked in D3D12 fence wait while completion callbacks are unable to run, prioritize fence/callback lock inversion.
- If shader compiler threads dominate, re-check assumptions, but do not switch to cache/source-compile changes without explicit evidence.

## What not to do

- Do not resurrect Slice 3A queue counters.
- Do not add always-on hot-path telemetry.
- Do not use trace-heavy launch env for the visual pass/fail test.
- Do not use `/mtsp/prepare`.
- Do not use retired deployment scripts.
- Do not clear shader caches unless explicitly testing cache/source-compile behavior.
- Do not kill Steam/Wine/backend/game processes without explicit approval.
- Do not claim success from `mini_rc=0`, spawned PID, or dry-run alone.
- Do not proceed without operator visual confirmation for real-scene/character-creator progress.
