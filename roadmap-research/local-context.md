# Local Context Brief — Forward Roadmap after Stage 3D+3E

Compiled 2026-06-19. Read-only scout summary for the next planning agent.
Scope: repo `/Users/alexmondello/metalsharp-m12-lab`, branch `fix/m12-shader-probe-lab`,
AC6 (Armored Core VI, appid 1888160) M12 D3D12→Metal launch work.

---

## 1. Current repo / process state (ground truth)

- **HEAD = `a909c83`** `fix(m12): gate render attachment resource use`.
- **Working tree is CLEAN** (`git diff --stat` empty). The failed Stage 3D+3E patch has
  **already been reverted**. The two new env gates (`DXMT_D3D12_RETAIN_UAV_CLEAR_SNAPSHOTS`,
  `DXMT_D3D12_RETAIN_EXECUTE_INDIRECT_ARGS`) are confirmed **absent from committed HEAD**
  (grep count 0) — they lived only in the reverted working tree.
- Committed stack on top of old Slice-2 (`0573c54`):
  - `a1d7fed` establish AC6 safety diagnostics baseline (Stage 3A scaffolding)
  - `fbf63d6` add present descriptor mutation probe
  - `b09d154` snapshot RTV DSV UAV descriptors (descriptor-snapshot infra)
  - `c3215ec` add default-off replay retention diagnostics (Stage 3B)
  - `a909c83` gate render attachment resource use (Stage 3C) ← HEAD
- **`m12-slice4-recovery-forward-roadmap.md` is STALE**: it names `b09d154` as the
  "stable baseline" and lists 3A/3B/3C as future. In reality 3A/3B/3C are all committed.
  The real current baseline is **a909c83 (3A+3B+3C)**.
- **Live process plurality** (a known tripwire): two backends running simultaneously —
  PID 896 = `/Applications/MetalSharp.app/.../metalsharp-backend` (since 09:43),
  PID 55382 = `./target/release/metalsharp-backend` (the 9277 PR backend), plus
  wineserver 55715 + 2× winedevice. Stale/dueling backends are a launch hazard.
- Slice-3 return point exists: `/Users/alexmondello/slice3baseline/latest`
  (repo head recorded as `a1d7fed`).
- Slice-2 baseline bundle + runbook: `/Users/alexmondello/slice2baseline/latest`.

## 2. Files Retrieved (why each matters)

1. `tools/d3d12-metal-sdk/plans/m12-slice4-recovery-forward-roadmap.md` (full) — prior
   incremental-retention roadmap. Non-negotiable guardrails + per-stage runbook are still
   authoritative, but its baseline commit (b09d154) and stage list are stale.
2. `tools/d3d12-metal-sdk/plans/m12-ac6-postcontinue-gpu-fault-roadmap.md` (full,
   untracked) — the REAL correctness roadmap (M13.0–M13.6). Documents the magenta-frame /
   full-machine GPU firmware deadlock at the post-Continue scene transition and the
   do-not-launch gate. This is the work the retention stages are serving.
3. `/Users/alexmondello/slice2baseline/latest/repo/m12-ac6-slice2-stage-launch-runbook.md`
   (full) — canonical launch procedure, env invariants, hash table, regression tripwires.
4. `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp:8199` — `ExecuteCommandLists` entry,
   i.e. the D3D12 replay path. **All 3C/3D/3E code lives downstream of this and is only
   reached after the D3D12 device/queue exist and the app submits command lists.**
5. `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp:184-198` — committed env gates:
   `DXMT_D3D12_GPU_HANG_SAFETY`, `DXMT_D3D12_STRICT_ARG_BUFFER_RESOURCES`,
   `DXMT_D3D12_REPLAY_RETENTION_DIAGNOSTICS`, `DXMT_D3D12_RETAIN_RENDER_ATTACHMENTS` (3C).
6. `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp:5352-5398` + `:5480` — Stage 3C
   `UseRenderAttachmentResource` / `UseActiveRenderAttachmentResources`, called from
   `EnsureRenderEncoder()`. Narrow, default-off, uses non-materializing `PeekMTLTexture()`.
7. `vendor/dxmt/src/d3d12/d3d12_resource.hpp:92-93` — `PeekMTLBuffer()`/`PeekMTLTexture()`
   non-materializing accessors (Stage 3A scaffolding; the safe alternative to
   side-effecting `GetMTLTexture()`).
8. `vendor/dxmt/src/winemetal/winemetal.h:1167,1229` +
   `vendor/dxmt/src/winemetal/unix/winemetal_unix.c:2333` — the compute `useResource`
   bridge command (`WMTComputeCommandUseResource`) is **already implemented**. The 3E
   patch's `Metal.hpp:588` change only wraps this pre-existing bridge call.
9. `/tmp/metalsharp-m12-slice4-stage3de-runbook-20260619-111244/` — failed 3D+3E evidence:
   `failed-stage3de/stage3de-failed-uncommitted.patch`, `failed-stage3de/regression-snapshot.txt`,
   `ac6-stage3de-launch-monitor.txt`, `ac6-stage3de-launch-response.json`,
   `dry-run-m12-ac6-post-backend-restart.effective-check.json`, build/stage/preflight `*.rc`.
10. `/tmp/metalsharp-m12-slice4-revert-stage3-to-stage2-runbook-20260619-101001/` — the
    OLDER broad Stage-3 failure + the declared-good "restored Stage 2" launch monitor.
11. `~/.metalsharp/compatdata/1888160/logs/launch-1781889403.log` (193 lines) — the 3D+3E
    launch log. Ends at MoltenVK `VkInstance` creation; `mscompatdb: not found` (correct).

## 3. Key code surfaces

### Committed gates (d3d12_command_queue.cpp ~L184-198)
```cpp
getenv("DXMT_D3D12_GPU_HANG_SAFETY")
getenv("DXMT_D3D12_STRICT_ARG_BUFFER_RESOURCES")
getenv("DXMT_D3D12_REPLAY_RETENTION_DIAGNOSTICS")   // Stage 3B (default-off)
getenv("DXMT_D3D12_RETAIN_RENDER_ATTACHMENTS")      // Stage 3C (default-off)
```
The reverted 3D+3E patch added two more of the same static-lambda shape:
`DXMT_D3D12_RETAIN_UAV_CLEAR_SNAPSHOTS`, `DXMT_D3D12_RETAIN_EXECUTE_INDIRECT_ARGS`.

### Stage 3C pattern (the safe template all retention stages copy)
```cpp
void UseRenderAttachmentResource(MTLD3D12Resource *res, const char *label) {
  if (!DXMTD3D12RetainRenderAttachments() || !render_enc.handle || !res) return; // gate
  auto tex = res->PeekMTLTexture();           // NON-materializing
  if (!tex.handle) return;
  render_enc.useResource(tex, Read|Write, WMTRenderStageFragment);
  ...
}
```

### What the failed 3D+3E patch actually did (now reverted)
- Added `NoteUAVClearSnapshotResources()` — diagnostic-only, `Peek`-based, gated (3D).
- Added `UseRender/ComputeExecuteIndirectBufferResource()` — `useResource` on the
  argument/count buffers, gated (3E).
- Changed `ReplayComputeDispatch()` signature (+2 trailing optional `MTLD3D12Resource*`)
  and updated 3 call sites in `ExecuteCommandLists`.
- Added `Metal.hpp` `ComputeCommandEncoder::useResource(Resource, usage)` wrapper around
  the **pre-existing** `WMTComputeCommandUseResource` bridge command.
- Wired diagnostics into the `clear_uav` replay path.
- Net: narrow, default-off, non-materializing, low-risk in isolation. **But 3D and 3E
  were bundled into one launch, violating the roadmap's "one micro-stage per launch".**

### Contrast: the OLDER failed Stage 3 patch (386 lines, also reverted earlier)
Broad retention: `RetainD3D12Resource`/`RetainBuffer`/`RetainTexture` with `AddRef()`,
O(n²) linear dedup loops, **materializing** `GetMTLBuffer()`/`GetMTLTexture()` in
retention/logging paths, completion-slot `std::vector` retention, ~30 call sites in
`ExecuteCommandLists`, plus `d3d12_command_buffer_completion.hpp` changes. This is the
patch the slice4 roadmap explicitly banned. It is NOT what 3D+3E was.

## 4. Why 3D+3E "failed" — the decisive evidence

**The 3D+3E patch logic is almost certainly NOT the proximate cause.** Three independent
facts:

1. **All retention gates were OFF at launch.** `effective-check.json` shows
   `render_attachment_retain_default_off`, `uav_clear_snapshot_default_off`, and
   `execute_indirect_args_default_off` all `true`; `all_pass: true`. The new code paths
   early-return and never execute. Hash sync OK, `mscompatdb` absent, build/stage/preflight
   all `rc=0`.
2. **The replay path was never reached.** The 3D+3E launch log (193 lines) ends at
   MoltenVK `Created VkInstance`. GPU memory stayed **0 MB** for all 16 monitor polls
   (~1:23). No D3D12 device/queue/PSO/draw ever appears. Everything 3C/3D/3E touches lives
   inside `ExecuteCommandLists` (L8199), which is downstream of device creation and only
   runs once the app submits command lists.
3. **The "failure" pattern is identical to a launch that was DECLARED SUCCESS.** The
   restored-Stage-2 monitor (`ac6-stage2-restored-launch-monitor.txt`, 10:14, the run that
   re-established the stable baseline) shows the *exact same* signature: PID in `R` state,
   GPU memory used 0 MB, log stuck at VkInstance, cycling child PIDs (78742→78760→78851…).
   The 3D+3E monitor (48521→48526→48604…) is the same cycling — these are EasyAntiCheat
   launcher (`start_protected_game.exe`) child retries, i.e. normal early init.

**Most likely real causes of the 3D+3E process exit (~1.5 min in):**
- **Premature failure call.** The monitor ran only ~1:23 and the process was still alive
  (`R`). Stage 2 showed the same early pattern and was given more time; the operator likely
  declared 3D+3E failed before init completed.
- **Environmental / EAC exit.** Launch response shows
  `anti_cheat_status: "blocked_pending_vendor_support"` and `eac_toggle_deployed: false`.
  The EAC wrapper can retry-then-exit independently of any D3D12 code.
- **Stale/dueling backend+wineserver state** (see §1). 8-hour-old wineserver plus two
  backends is a known launch hazard.

**Conclusion:** the 3D+3E code is exonerated by the evidence. Treating this as a
retention-code regression would be a misdiagnosis. The blocker is launch reliability /
init environment, not the retention diff.

### Open question the new roadmap must resolve first
There is **no evidence directory for a visual AC6 launch of Stage 3C (a909c83)**. The
postcontinue roadmap's "do-not-launch gate" says no AC6 launch until M13.0–M13.6 are done.
So 3D+3E may have been the **first visual launch attempt since the magenta GPU deadlock**,
committed on offline-probe evidence only. It is unclear whether the committed a909c83
baseline can currently reach the AC6 menu at all. This must be established before reading
any more meaning into the 3D+3E run.

## 5. Architecture / data flow

```
POST /steam/launch-game (backend :9277, METALSHARP_PORT not PORT)
  → Wine + start_protected_game.exe (EAC wrapper)
     → MoltenVK VkInstance (vulkan init)            ← 3D+3E log STOPS here
        → D3D12CreateDevice → MTLD3D12Device
           → CreateCommandQueue → MTLD3D12CommandQueue
              → ExecuteCommandLists (d3d12_command_queue.cpp:8199)
                 → ReplayState replay of recorded command packets
                    → EnsureRenderEncoder()  ← Stage 3C UseActiveRenderAttachmentResources
                    → replay_indirect_draw / draw_indexed  ← 3E hooks (gated, reverted)
                    → ReplayComputeDispatch                 ← 3E hooks (gated, reverted)
                    → clear_uav                             ← 3D diagnostics (gated, reverted)
```
All retention stages operate in the bottom box. They cannot affect the VkInstance-stage
hang observed in the 3D+3E run.

## 6. Constraints & risks (non-negotiable, from slice2 runbook)

- Launch only via `POST /steam/launch-game` on `http://127.0.0.1:9277` (`METALSHARP_PORT`,
  never `PORT`). Never `/mtsp/prepare`.
- Stage only to `~/.metalsharp/runtime/wine/lib/dxmt_m12` (never generic `lib/dxmt`).
- `mscompatdb` must stay absent; `WINEDLLOVERRIDES` must not contain it.
- Full DLL set must hash-match across build / runtime / game-local: `d3d12, d3d11, dxgi,
  dxgi_dxmt, d3d10core, winemetal` + unix sidecars `winemetal.so`, `libm12core.dylib`.
- No process kills (Steam/Wine/wineserver/backend/game) without explicit per-action approval.
- No commit without operator approval + visual confirmation.
- **One micro-stage per launch** (3D+3E violated this).
- Known-good build path: `rm -rf vendor/dxmt/build-metalsharp-x64`;
  `tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`; then explicit
  `meson compile -C vendor/dxmt/build-metalsharp-x64 d3d11 d3d10core`.

## 7. Safer next investigation phases (recommended order)

### Phase 0 — Exonerate the code; re-establish launchability (NO code changes)
Highest value, lowest risk. Must precede all retention work.
1. Confirm working tree clean at `a909c83` (already true).
2. Run the **full slice2 runbook** from a clean environment: with approval, reconcile the
   dueling backends (stop the stray `.app` backend PID 896 or stop routing to it) and
   restart the wineserver so no multi-hour-stale prefix state remains.
3. Launch AC6 with the canonical no-log request and **monitor ≥3–5 min**, not 1:23.
4. Branch on outcome:
   - If it hangs identically at VkInstance / exits early → **code exonerated**; pivot to
     Phase 1 (environment/EAC). Do NOT touch retention code.
   - If it reaches the menu → a909c83 is the confirmed visual baseline; the 3D+3E rebuild
     may have introduced an init difference worth a narrow bisect (but note: with gates off
     and replay unreached, even this is unlikely to be the retention logic).
5. Add an **init-progress monitor assertion**: flag "GPU mem 0 + log at VkInstance after N
   seconds" as a distinct *init-hang (environmental)* class vs *replay-stage failure*, so
   future env hangs aren't mis-attributed to retention code.

### Phase 1 — Environment / init hygiene (NO D3D12 code changes)
- Guarantee exactly one backend (the 9277 PR backend).
- Fresh wineserver/prefix with approval; verify EAC isn't the exit cause (tail EAC logs,
  confirm child-PID cycling = launcher retry-and-give-up).
- Decide whether a non-EAC / offline launch variant is policy-acceptable before relying on it.

### Phase 2 — Resume gated retention (ONLY after Phase 0 reconfirms launchability)
- **Update the slice4 roadmap doc**: baseline is `a909c83`, not `b09d154`; 3A/3B/3C done.
- Re-attempt 3D and 3E as **separate** single-stage launches (never bundled), each default-off.
- Require a same-day clean-environment baseline launch (Phase 0) immediately before each
  retention launch, because init flakiness confounds attribution.
- The reverted 3D+3E patch is safe to re-apply *in two halves*; it is low-risk in isolation
  (gated, non-materializing, compute-useResource bridge pre-existing).

### Phase 3 — Resume postcontinue GPU-fault work offline (parallel, no launch)
- Follow `m12-ac6-postcontinue-gpu-fault-roadmap.md` M13.0→M13.6 **offline only**.
- The init-hang blocker does not block offline probe progress on descriptor-mutation,
  barrier/subresource, and present/drawable lifetime suites.

## 8. Start here
Open `tools/d3d12-metal-sdk/plans/m12-slice4-recovery-forward-roadmap.md` for the guardrails
and per-stage runbook, then immediately read §4 above: the 3D+3E "failure" is most likely a
premature/env init-hang misread, not a code regression. The first concrete action is
**Phase 0** — re-confirm that the clean `a909c83` tree visually launches AC6 to the menu in a
clean environment with a ≥3–5 min monitor window.

## 9. Supervisor coordination
No decision needed to produce this brief. Flag for supervisor if a future step requires
approval to kill the dueling `.app` backend (PID 896) or restart the stale wineserver,
since the slice2 runbook forbids process kills without explicit per-action approval.
