# M12 AC6 Slice-4 Recovery Forward Roadmap

Status: Stage 2 restored and visually confirmed stable after reverting failed Stage 3.

Last updated: 2026-06-19

## Current Stable Baseline

Repository:

```text
/Users/alexmondello/metalsharp-m12-lab
branch: fix/m12-shader-probe-lab
HEAD: b09d154e169db8f845079fe8b6aaf5b208d54e3c
b09d154 fix(m12): snapshot RTV DSV UAV descriptors
```

Confirmed commits in the safe stack:

```text
a1d7fed fix(m12): establish AC6 safety diagnostics baseline
fbf63d6 test(m12): add present descriptor mutation probe
b09d154 fix(m12): snapshot RTV DSV UAV descriptors
```

Restored Stage-2 evidence root:

```text
/tmp/metalsharp-m12-slice4-revert-stage3-to-stage2-runbook-20260619-101001
```

Restored Stage-2 validation passed:

- clean DXMT/M12 rebuild: `121/121`
- explicit `d3d11` + `d3d10core`: `39/39`
- probes build
- full stage to `~/.metalsharp/runtime/wine/lib/dxmt_m12`
- full AC6 game-local DLL sync
- hash sync across build/runtime/game
- `mscompatdb` absent
- dry-run/effective env check
- preflight
- backend rebuild/restart on `127.0.0.1:9277`
- AC6 launch visually confirmed by user

Canonical launch command remains:

```bash
curl -sS -X POST 'http://127.0.0.1:9277/steam/launch-game' \
  -H 'Content-Type: application/json' \
  --data-binary @'/Users/alexmondello/slice3baseline/20260619-021124/requests/ac6-m12-canonical-nolog-launch-request.json'
```

## Failed Stage 3 Summary

Failed stage patch was preserved at:

```text
/tmp/metalsharp-m12-slice4-revert-stage3-to-stage2-runbook-20260619-101001/failed-stage3/stage3-failed-uncommitted.patch
```

Failure:

```text
PID 55823 became defunct shortly after launch
log: /Users/alexmondello/.metalsharp/compatdata/1888160/logs/launch-1781884339.log
```

Autoreview conclusion:

- AddRef/Release balance was not obviously broken.
- The likely failure was behavioral lifetime expansion, not a classic double free.
- Main hazards:
  1. D3D12 resources and backing Metal objects were retained in completion slots until slot reuse, extending lifetime across the inflight ring.
  2. O(n^2) linear dedup in retain helpers ran on hot replay paths.
  3. `GetMTLTexture()` was called from retention/logging paths and may lazily materialize textures that previous code did not allocate.

Therefore: **do not commit or reapply Stage 3 as written.**

## Non-Negotiable Guardrails

Every future stage must follow these rules:

1. Start from the last visually confirmed committed baseline.
2. Apply exactly one micro-stage.
3. Keep AC6 launches explicitly user-approved.
4. Use `POST /steam/launch-game`, never `/mtsp/prepare`.
5. Use `METALSHARP_PORT=9277`, not `PORT`.
6. Stage only to `~/.metalsharp/runtime/wine/lib/dxmt_m12`, never generic `dxmt`.
7. Keep `mscompatdb` absent/excluded.
8. Sync the full AC6 DLL set:
   - `d3d12.dll`
   - `d3d11.dll`
   - `dxgi.dll`
   - `dxgi_dxmt.dll`
   - `d3d10core.dll`
   - `winemetal.dll`
   - Unix sidecars `winemetal.so`, `libm12core.dylib`
9. No process termination unless explicitly approved.
10. No commit until:
    - build/runbook validation passes
    - launch is explicitly approved
    - user visually confirms the launch
11. If any stage fails AC6, immediately preserve patch/evidence, revert to last committed visual baseline, rerun the full baseline runbook, and require a fresh visual confirmation before proceeding.

## Required Runbook for Each Stage

For every future micro-stage:

1. Record baseline:
   - `git status --branch --short --untracked-files=no`
   - `git rev-parse HEAD`
   - `git rev-parse origin/fix/m12-shader-probe-lab`
2. Preserve backups:
   - build dir state
   - runtime DLLs/sidecars
   - AC6 game-local DLLs
   - backend binary
   - any `mscompatdb*` files
3. Apply one micro-stage only.
4. Audit diff for forbidden surfaces.
5. Clean rebuild:
   - delete `vendor/dxmt/build-metalsharp-x64`
   - `tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`
   - explicit `meson compile -C vendor/dxmt/build-metalsharp-x64 d3d11 d3d10core`
6. Build probes.
7. Stage full runtime to `dxmt_m12` and AC6 game-local.
8. Verify hashes across build/runtime/game.
9. Verify `mscompatdb` absent.
10. Run dry-run/effective env check.
11. Run preflight.
12. Rebuild/restart backend on `127.0.0.1:9277`.
13. Final readiness sweep:
    - backend status OK
    - no AC6/game process running unless user expects one
    - `mscompatdb` absent
14. Ask user for explicit launch approval.
15. Launch with canonical request.
16. Light monitor only.
17. Wait for user visual confirmation.
18. Commit only the exact micro-stage files after confirmation.

## Forward Stages

### Stage 3A — Retention Redesign Scaffolding Only

Goal: add safe primitives needed for any future retention work, without changing runtime behavior.

Allowed changes:

- Add non-materializing resource accessors, e.g. `PeekMTLTexture()` / `PeekMTLBuffer()` or equivalent.
- Add comments/tests/probe assertions documenting that `GetMTLTexture()` is side-effecting and must not be used in retention/logging paths.
- No replay-path retention.
- No completion-slot retention.
- No present-path changes.

Forbidden changes:

- No `AddRef()` retention.
- No `WMT::Reference` retention vectors.
- No calls that can lazily allocate textures in new paths.

Validation target:

- Must be behaviorally identical to Stage 2.
- AC6 must visually confirm stable before commit.

Commit name if passed:

```text
fix(m12): add non-materializing resource peek helpers
```

### Stage 3B — Retention Instrumentation Behind Default-Off Gate

Goal: learn which resources might need retention without retaining them by default.

Allowed changes:

- Add a default-off env gate, for example:

```text
DXMT_D3D12_REPLAY_RETENTION_DIAGNOSTICS=0
```

- When disabled, byte-for-byte runtime behavior should remain equivalent except harmless env parsing.
- When enabled in probes only, log candidate resources that would have been retained.
- Record whether backing objects already exist via non-materializing accessors.

Forbidden changes:

- No default-on retention.
- No AC6 launch with diagnostics enabled unless explicitly approved as a diagnostic run.
- No lazy texture materialization.

Validation target:

- Default-off AC6 visual launch must match Stage 2.
- Optional probe-only diagnostic runs may be used before any game run.

Commit name if passed:

```text
test(m12): add default-off replay retention diagnostics
```

### Stage 3C — Minimal Render Attachment Lifetime Fix

Goal: address only render-pass RTV/DSV attachment lifetime, if evidence shows it is needed.

Allowed changes:

- Retain/use only already-materialized textures for active RTV/DSV attachments.
- Prefer Metal encoder/resource usage semantics over COM AddRef where possible.
- Release on actual command-buffer completion, not slot reuse.
- Keep behind a default-off env gate at first:

```text
DXMT_D3D12_RETAIN_RENDER_ATTACHMENTS=0
```

Forbidden changes:

- No barrier/copy/execute-indirect/UAV broad retention.
- No root-table retention.
- No `GetMTLTexture()` from retention helpers.
- No inflight-ring lifetime extension.

Validation target:

1. Default-off run must remain stable.
2. If enabled, run probes first.
3. Only then ask for AC6 launch approval.

Commit name if passed:

```text
fix(m12): retain render attachments without materializing resources
```

### Stage 3D — Minimal UAV Clear Snapshot Lifetime Fix

Goal: protect only replayed UAV clear snapshot resources, if evidence shows this is required.

Allowed changes:

- Use Stage-2 descriptor snapshots as the source of truth.
- Retain only the exact resource/counter already present in the snapshot.
- Do not materialize textures or buffers.
- Prefer existing bound Metal resource semantics where possible.
- Default-off env gate initially:

```text
DXMT_D3D12_RETAIN_UAV_CLEAR_SNAPSHOTS=0
```

Forbidden changes:

- No per-barrier retention.
- No copy/resolve/execute-indirect retention.
- No broad descriptor-table scanning.

Validation target:

- Default-off AC6 confirms stable before testing enabled mode.
- Enabled mode must be probe-tested before any game launch.

Commit name if passed:

```text
fix(m12): narrowly retain UAV clear snapshot resources
```

### Stage 3E — ExecuteIndirect Argument/Count Lifetime Fix

Goal: address only indirect argument/count resources, if needed.

Allowed changes:

- Retain/use the argument and count buffers only for command buffers that encode indirect execution.
- Use non-materializing buffer access.
- Release on actual command-buffer completion.
- Default-off gate initially:

```text
DXMT_D3D12_RETAIN_EXECUTE_INDIRECT_ARGS=0
```

Forbidden changes:

- No texture retention.
- No render attachment retention in this stage.
- No barrier/copy/resolve retention.
- No root-table retention.

Validation target:

- Probe validation first.
- AC6 only after explicit approval.

Commit name if passed:

```text
fix(m12): narrowly retain execute indirect buffers
```

### Stage 4A — Barrier Safety Audit Only

Goal: inspect transition/UAV/aliasing barrier behavior without changing lifetime.

Allowed changes:

- Add counters or probe-only assertions for barrier resources.
- Add diagnostic logging behind a default-off gate.
- Do not retain barrier resources.

Forbidden changes:

- No `RetainResourceAndBacking()` equivalent for barriers.
- No `GetMTLTexture()` calls from barrier diagnostics.
- No state mutation beyond existing behavior.

Validation target:

- Default-off AC6 must remain visually stable.

Commit name if passed:

```text
test(m12): add default-off barrier safety diagnostics
```

### Stage 4B — Barrier Safety Fixes, One Type at a Time

Goal: if diagnostics reveal a real barrier bug, fix only that type.

Split into separate substages:

- 4B.1 transition barriers
- 4B.2 UAV barriers
- 4B.3 aliasing barriers

Rules:

- One barrier type per commit.
- No lifetime retention unless proven necessary and separately gated.
- No broad resource retention.

Validation target:

- Each barrier substage gets a full runbook cycle and user visual confirmation.

### Stage 5A — Present/Drawable Diagnostics Only

Goal: revisit present-path Slice-4 work without importing its timing changes.

Allowed changes:

- Add default-off diagnostics for drawable/backbuffer lifetime.
- Add probe-present-windowed coverage.
- No present wait/timing changes.
- No new blocking waits.

Forbidden changes:

- No changes to present wait policy.
- No broad swapchain behavior changes.
- No AC6 launch with diagnostics enabled unless explicitly approved.

Validation target:

- Default-off AC6 must remain stable.
- Probe-present-windowed must pass.

Commit name if passed:

```text
test(m12): add default-off present lifetime diagnostics
```

### Stage 5B — Present Drawable/Backbuffer Lifetime Fix

Goal: apply only the minimal present lifetime fix supported by Stage 5A diagnostics.

Allowed changes:

- Retain only already-existing drawable/backbuffer objects.
- Release on actual command-buffer completion.
- Keep any experimental path env-gated until AC6 proves stable.

Forbidden changes:

- No present timing/wait changes.
- No sleeps or forced waits.
- No broad resource retention.

Validation target:

- Probe-present-windowed first.
- Full runbook.
- Explicit AC6 launch approval.
- User visual confirmation before commit.

Commit name if passed:

```text
fix(m12): retain present drawable lifetime narrowly
```

### Stage 6 — Root Table / Replay State Retention Experiments

Goal: quarantine the riskiest Slice-4 surface.

Default position: **do not apply broad root-table retention.**

If ever revisited:

- Must be default-off env gated.
- Must be probe-only first.
- Must never be enabled in AC6 without explicit diagnostic approval.
- Must avoid scanning entire descriptor tables by default.
- Must avoid retaining resources across the inflight ring.
- Must avoid lazy texture materialization.

Possible gate:

```text
DXMT_D3D12_EXPERIMENTAL_ROOT_TABLE_RETENTION=0
```

Commit only if a narrow, proven, default-safe subset exists.

## Stage Failure Protocol

If a future stage fails:

1. Do not commit.
2. Preserve:
   - uncommitted patch
   - launch response
   - launch log
   - process state
   - hash/preflight evidence
3. Do not kill processes unless explicitly approved.
4. Revert only the failed stage files.
5. Rebuild/stage the last committed visual baseline through the full runbook.
6. Ask for explicit launch approval.
7. Require user visual confirmation before any new forward work.
8. Record the failure in this roadmap or a linked failure note.

## Immediate Next Recommendation

Do not attempt another retention implementation yet.

Next safe action should be:

1. Commit nothing; Stage 2 is already committed and restored.
2. Leave failed Stage 3 as preserved evidence only.
3. Start with Stage 3A, the non-materializing accessor/scaffolding stage.
4. Run the full runbook and visually confirm AC6 again before any behavior-changing retention work.

