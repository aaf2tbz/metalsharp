# M12 Forward Roadmap v2 — Post Stage 3D/3E Reset

Status: forward planning document. Do **not** treat this as approval to rebuild, restage, kill processes, launch AC6, or commit code.

Created: 2026-06-19
Repo: `/Users/alexmondello/metalsharp-m12-lab`
Branch: `fix/m12-shader-probe-lab`
Current committed baseline: `a909c83d158cdcd0058c51b056ed3c77ae6d782e`

```text
a909c83 fix(m12): gate render attachment resource use
c3215ec test(m12): add default-off replay retention diagnostics
b09d154 fix(m12): snapshot RTV DSV UAV descriptors
fbf63d6 test(m12): add present descriptor mutation probe
a1d7fed fix(m12): establish AC6 safety diagnostics baseline
```

Research inputs:

```text
roadmap-research/official-docs.md
roadmap-research/ecosystem-github.md
roadmap-research/local-context.md
```

Authoritative staging/launch runbook remains:

```text
/Users/alexmondello/slice2baseline/latest/repo/m12-ac6-slice2-stage-launch-runbook.md
```

## Executive Summary

Stage 3D+3E is rejected as a bundled forward step and must not be re-applied wholesale. The failed uncommitted patch was preserved under:

```text
/tmp/metalsharp-m12-slice4-stage3de-runbook-20260619-111244/failed-stage3de/stage3de-failed-uncommitted.patch
```

However, research and local evidence show we should **not infer a proven D3D12 replay/lifetime regression** from that run alone:

- the 3D/3E gates were default-off in the effective launch env;
- the AC6 log stopped at early MoltenVK/VkInstance initialization, before D3D12 command queue replay;
- Metal `useResource` is not object retention, and `MTLBlitCommandEncoder` has no `useResource` at all;
- the old broad Stage 3 completion-slot/inflight-ring retention remains rejected independently.

Known AC6 state: title/menu already reaches a visible baseline. The target failure is specifically after selecting **Continue**, when AC6 enters real-world rendering/computation and can trigger the GPU/MMU/firmware lockup.

Therefore v2 pivots from “keep adding retention hooks” to:

1. classify launch/progression phases cleanly before assigning blame;
2. preserve title/menu as a quick sanity gate, not the main problem;
3. focus diagnostics on the Continue → real-world transition;
4. build offline probes for UAV clear / ExecuteIndirect / barriers;
5. design a submission-scoped lifetime model from specs and prior art;
6. only then land one behavior-changing fix at a time.

## Global Guardrails

These apply to every phase.

1. Use `POST /steam/launch-game`; never `/mtsp/prepare`.
2. Use backend `http://127.0.0.1:9277` and `METALSHARP_PORT=9277`; never `PORT=9277`.
3. Stage only to `~/.metalsharp/runtime/wine/lib/dxmt_m12`, never generic `dxmt`.
4. Keep `mscompatdb` absent and out of `WINEDLLOVERRIDES`.
5. Sync the full AC6 DLL set: `d3d12.dll`, `d3d11.dll`, `dxgi.dll`, `dxgi_dxmt.dll`, `d3d10core.dll`, `winemetal.dll`, plus `winemetal.so` and `libm12core.dylib`.
6. No Steam/Wine/wineserver/game/backend process kills without explicit per-action approval.
7. No AC6 visual launch without explicit approval.
8. No commits without explicit approval after visual confirmation.
9. One micro-stage per launch. Do not bundle independent hypotheses again.
10. Default-off behavior must be equivalent unless the phase explicitly says otherwise.
11. No `GetMTLTexture()`/`GetMTLBuffer()` in new retention/logging helpers unless the phase explicitly proves materialization is intended.
12. No completion-slot/inflight-ring lifetime expansion. Release on actual command-buffer completion, not slot reuse.
13. Do not treat `useResource` as retention. It is a hazard/residency declaration for render/compute encoders only.
14. Do not invent blit `useResource`; Metal has no such API on `MTLBlitCommandEncoder`.
15. Preserve evidence under `/tmp` or ignored results dirs; do not commit logs, runtime binaries, shader caches, or raw corpora.

## Rejected Designs

### R0 — Old broad Stage 3 retention

Rejected permanently unless a new design proves otherwise.

Forbidden patterns:

- broad `AddRef()`/`Release()` retention on hot replay paths;
- `WMT::Reference` vectors attached to completion slots/inflight ring reuse;
- O(n²) linear dedup on replay hot paths;
- `GetMTLTexture()` / `GetMTLBuffer()` materialization in diagnostics/retention paths;
- barrier/copy/resolve/root-table broad retention without phase-specific proof.

### R1 — Stage 3D+3E bundle

Do not reapply the bundle. If any parts are revisited, split them into independent phases with separate runbook cycles.

Rejected bundle surfaces:

- `DXMT_D3D12_RETAIN_UAV_CLEAR_SNAPSHOTS`
- `DXMT_D3D12_RETAIN_EXECUTE_INDIRECT_ARGS`
- compute `useResource` wrapper + ExecuteIndirect hooks
- UAV clear snapshot diagnostics

The ideas may be re-evaluated only through the v2 phases below.

---

# Phase Group 1 — Baseline and Launch Classification

Goal: prevent early init/EAC/backend noise from being misclassified as D3D12 replay regressions.

## Phase 1A — Freeze Baseline and Evidence Index

Allowed:

- write/update a small roadmap/evidence index only;
- record current HEAD, pushed origin, runbook evidence roots, failed patch paths;
- mark old `m12-slice4-recovery-forward-roadmap.md` stale.

Forbidden:

- no source code changes;
- no rebuild/stage/launch;
- no process kills.

Validation:

- `git rev-parse HEAD` = `a909c83d158cdcd0058c51b056ed3c77ae6d782e`;
- `git status --branch --short --untracked-files=no` has no unexpected tracked changes;
- evidence paths exist or are explicitly marked missing.

Commit suggestion:

```text
docs(m12): reset forward roadmap after retention bundle rejection
```

## Phase 1B — Launch Failure Taxonomy Harness

Goal: classify failures as init/EAC/VkInstance, D3D12 device creation, command replay, present, post-Continue GPU fault, or process exit.

Allowed:

- add a read-only post-launch classifier script that parses launch response, process state, launch log, and optional DXMT logs;
- distinguish “log stopped at MoltenVK VkInstance + GPU memory 0 MB” from “D3D12 replay reached”; 
- classify child PID cycling separately from renderer failure.

Forbidden:

- no game launch in the script unless a separate `--yes-launch` gate exists;
- no kill/restart;
- no trace-heavy defaults.

Validation:

- run classifier on existing evidence:
  - `/tmp/metalsharp-m12-slice4-stage3de-runbook-20260619-111244/ac6-stage3de-launch-monitor.txt`
  - `/Users/alexmondello/.metalsharp/compatdata/1888160/logs/launch-1781889403.log`
- expected output labels the failed run as `init-vkinstance-or-eac`, not `d3d12-replay-regression`, unless new evidence contradicts it.

Commit suggestion:

```text
test(m12): classify AC6 launch failures before replay attribution
```

## Phase 1C — Clean Environment Baseline Run

Goal: reconfirm that committed `a909c83` can visually reach the expected AC6 baseline from a clean operator-approved environment.

Allowed:

- full Slice-2 runbook rebuild/stage/hash/preflight/backend cycle;
- with explicit approval, reconcile stale/dueling backend or Wine state;
- run a 3–5 minute launch monitor after explicit launch approval.

Forbidden:

- no source code changes;
- no process kill without explicit PID-specific approval;
- no diagnostics enabled by default.

Validation:

- full runbook passes;
- AC6 launch uses the runbook inline no-log payload;
- launch monitor records enough time to classify init vs replay vs visual success;
- user visual confirmation is required before calling baseline stable.

Commit suggestion: none unless scripts/docs were added.

## Phase 1D — Backend/Wine State Hygiene Script

Goal: make “two backends / stale wineserver” visible before launch.

Allowed:

- read-only script that reports:
  - all `metalsharp-backend` processes;
  - exact 9277 listener PID;
  - wineserver/winedevice age;
  - AC6/game process presence;
  - active `dxmt_m12` route and `mscompatdb` absence.

Forbidden:

- no kill/restart;
- no launch.

Validation:

- script exits nonzero if multiple backends exist and one is not the 9277 PR backend;
- script prints explicit remediation instructions requiring operator approval.

Commit suggestion:

```text
test(m12): report backend and wineserver launch hygiene
```

---

# Phase Group 2 — Spec and Local Model Audit

Goal: align local code with D3D12 + Metal rules before making behavior changes.

## Phase 2A — Resource Lifetime Contract Note

Allowed:

- write a source-backed design note summarizing:
  - D3D12 descriptor/resource alive-until-fence rule;
  - descriptor heap/source descriptor mutation rules;
  - Metal command-buffer resource lifetime;
  - why `useResource` is not object retention.

Forbidden:

- no code changes;
- no launch.

Validation:

- note cites official docs / research artifacts;
- note explicitly bans completion-slot/inflight-ring retention.

Commit suggestion:

```text
docs(m12): document D3D12 and Metal replay lifetime contracts
```

## Phase 2B — Descriptor Snapshot Completeness Audit

Allowed:

- read-only audit of all command structs that store descriptors or GPU handles;
- verify snapshot contains resource, counter, format/view metadata, CPU/GPU handle semantics;
- list missing snapshot fields without fixing them yet.

Forbidden:

- no retention;
- no descriptor-table scanning changes;
- no runtime behavior changes.

Validation:

- produce table for RTV/DSV/UAV/SRV/CBV/samplers/root descriptors;
- flag commands that dereference live descriptor heap slots during replay.

Commit suggestion:

```text
test(m12): audit descriptor snapshot coverage
```

## Phase 2C — Barrier / State Model Audit

Allowed:

- read-only audit of transition, UAV, aliasing, split barrier, COMMON promotion/decay handling;
- map local code to DirectX spec requirements.

Forbidden:

- no barrier fixes yet;
- no broad resource retention.

Validation:

- classify each barrier type as implemented / partial / missing;
- identify exact minimal probes needed.

Commit suggestion:

```text
test(m12): audit D3D12 barrier replay coverage
```

## Phase 2D — Metal Hazard Tracking Audit

Allowed:

- read-only audit of WMT resource creation paths for tracked/untracked hazard modes;
- identify blit → render/compute, compute → render, render → blit, cross-command-buffer dependencies;
- check whether `MTLFence`/`MTLSharedEvent` wrappers exist or need scaffolding.

Forbidden:

- no new waits/fences yet;
- no useResource-as-retention claims.

Validation:

- produce a dependency matrix: encoder pair, resource type, current mechanism, gap.

Commit suggestion:

```text
test(m12): audit Metal hazard tracking boundaries
```

---

# Phase Group 3 — Default-Off Diagnostics Only

Goal: observe real failing surfaces without changing default behavior.

## Phase 3A — Replay Reachability Breadcrumbs

Allowed:

- default-off or low-volume breadcrumbs that prove whether AC6 reached:
  - D3D12 device creation;
  - command queue creation;
  - first `ExecuteCommandLists`;
  - first render encoder;
  - first present.

Forbidden:

- no retention;
- no materialization;
- no log-heavy defaults for visual launch.

Validation:

- probes or non-AC6 runs can enable diagnostics;
- default-off AC6 full runbook must remain visually stable before commit.

Commit suggestion:

```text
test(m12): add default-off replay reachability breadcrumbs
```

## Phase 3B — Descriptor Mutation Diagnostics

Allowed:

- default-off diagnostics that compare descriptor snapshot against live descriptor only for reporting;
- use non-materializing accessors;
- count mutations per descriptor type.

Forbidden:

- no fallback to live descriptor data in replay;
- no descriptor-table scanning in default path.

Validation:

- probe covers CPU descriptor overwrite after command recording;
- default-off AC6 runbook passes.

Commit suggestion:

```text
test(m12): diagnose descriptor mutation after recording
```

## Phase 3C — UAV Clear Diagnostics v2

Allowed:

- default-off diagnostics only;
- log CPU/GPU handle pair consistency, view dimension, raw/typed/structured classification, clear value width, counter presence, resource state;
- do not call blit `useResource` because none exists.

Forbidden:

- no `DXMT_D3D12_RETAIN_UAV_CLEAR_SNAPSHOTS` behavior from the rejected bundle;
- no CPU-map clear replacement yet;
- no counter retention yet.

Validation:

- offline probes first;
- AC6 only with diagnostics off unless explicitly approved for diagnostic run.

Commit suggestion:

```text
test(m12): add default-off UAV clear semantic diagnostics
```

## Phase 3D — ExecuteIndirect Diagnostics v2

Allowed:

- default-off diagnostics only;
- log command signature argument types, stride, arg/count buffer state, offsets/alignment, command count clamp, root view NULLing needs;
- do not add `useResource` hooks yet.

Forbidden:

- no ICB path;
- no compute wrapper change;
- no argument/count buffer retention.

Validation:

- offline ExecuteIndirect probe matrix must pass;
- default-off AC6 runbook passes.

Commit suggestion:

```text
test(m12): add default-off execute indirect diagnostics
```

## Phase 3E — Encoder Boundary Diagnostics

Allowed:

- default-off diagnostics for encoder transitions where the same resource crosses blit/render/compute boundaries;
- record whether a resource is tracked/untracked if local APIs expose it;
- log missing fence opportunities only.

Forbidden:

- no fences yet;
- no waits;
- no broad hazard changes.

Validation:

- offline probes for blit→compute and compute→render hazards;
- default-off AC6 runbook passes.

Commit suggestion:

```text
test(m12): diagnose Metal encoder boundary hazards
```

---

# Phase Group 4 — Offline Probe Matrix

Goal: prove exact semantics without relying on AC6.

## Phase 4A — UAV Clear Probe Matrix

Allowed:

- add/extend probes covering:
  - raw buffer UAV clear;
  - typed buffer UAV clear;
  - structured buffer UAV clear;
  - zero and nonzero `UINT[4]` / `FLOAT[4]` values;
  - UAV with counter resource;
  - wrong CPU/GPU handle pair negative test.

Forbidden:

- no AC6 dependency;
- no runtime retention changes.

Validation:

- probe verifies byte output against D3D12 semantics as implemented by a reference where possible;
- probe catches the known vkd3d-proton-style wrong CPU descriptor handle failure.

Commit suggestion:

```text
test(m12): cover UAV clear descriptor and value semantics
```

## Phase 4B — ExecuteIndirect Probe Matrix

Allowed:

- add/extend probes covering:
  - draw;
  - indexed draw;
  - dispatch;
  - root constants;
  - root CBV/SRV/UAV;
  - VBV/IBV changes;
  - count buffer clamp;
  - nonzero offsets and stride padding.

Forbidden:

- no AC6;
- no ICB implementation yet.

Validation:

- expected command count and root state after ExecuteIndirect match D3D12 rules;
- root views targeted by command signature are NULL/restored as required.

Commit suggestion:

```text
test(m12): cover execute indirect command signature semantics
```

## Phase 4C — Descriptor Lifetime / Mutation Probe

Allowed:

- record commands using descriptors, mutate/free descriptor source slots, then execute;
- verify replay uses captured snapshot rather than live slot.

Forbidden:

- no broad descriptor heap retention yet.

Validation:

- probe fails on live-slot reread;
- probe passes with snapshot-only replay.

Commit suggestion:

```text
test(m12): prove descriptor snapshots survive source mutation
```

## Phase 4D — Barrier and Aliasing Probe Matrix

Allowed:

- probes for transition, UAV, aliasing, split barrier, COMMON promotion/decay;
- separate each barrier type.

Forbidden:

- no unified barrier rewrite in one commit.

Validation:

- each probe has deterministic pass/fail output and no AC6 requirement.

Commit suggestion:

```text
test(m12): cover D3D12 barrier replay semantics
```

## Phase 4E — Metal Encoder Hazard Microprobes

Allowed:

- native/Metal-side probes for:
  - blit fill → compute read;
  - compute write → render read;
  - render write → blit read;
  - tracked vs untracked resources if possible;
  - fence/shared-event variants.

Forbidden:

- no game launch;
- no production code path changes.

Validation:

- determine which dependencies Metal auto-tracks and which require explicit fence/event.

Commit suggestion:

```text
test(m12): probe Metal encoder hazard boundaries
```

---

# Phase Group 5 — Submission-Scoped Lifetime Architecture

Goal: design and test correct lifetime management without hot-path broad retention.

## Phase 5A — Pending Submission Resource Set Design

Allowed:

- introduce an internal design doc or inert structs for a per-submission resource set;
- key resources by actual command-buffer completion serial/fence value;
- no active replay integration yet.

Forbidden:

- no completion-slot reuse lifetime;
- no O(n²) hot-path dedup;
- no behavior change.

Validation:

- unit tests for dedup complexity and retirement order if scaffolding exists;
- autoreview focused on “not wired into replay”.

Commit suggestion:

```text
refactor(m12): model submission-scoped resource retirement
```

## Phase 5B — Completion Timeline / Event Scaffolding

Allowed:

- add or audit monotonic completion serial handling;
- prove callback/complete-signal retirement can release per-submission state;
- keep inactive by default.

Forbidden:

- no resource retention yet;
- no waits on every submit;
- no dummy signal/wait pairs.

Validation:

- synthetic command buffers retire in-order and out-of-order as expected;
- backend/replay behavior unchanged.

Commit suggestion:

```text
refactor(m12): scaffold command buffer completion retirement
```

## Phase 5C — Descriptor Snapshot Ownership Model

Allowed:

- design exactly which objects a recorded command owns:
  - descriptor heap or copied descriptor contents;
  - `ID3D12Resource` data resource;
  - UAV counter resource;
  - Metal backing object if already materialized.

Forbidden:

- no root-table broad scan;
- no materialization for ownership discovery.

Validation:

- table maps every command type to ownership fields;
- no source changes unless pure comments/static assertions.

Commit suggestion:

```text
docs(m12): define descriptor snapshot ownership model
```

## Phase 5D — Resource Tracker Probe-Only Integration

Allowed:

- integrate resource tracker only in probes or behind a default-off env gate;
- store already-known resources from command snapshots;
- retire on actual completion.

Forbidden:

- no AC6 with tracker enabled without explicit diagnostic approval;
- no broad table scan;
- no completion-slot/inflight-ring release.

Validation:

- offline probe confirms resources stay alive until completion and retire after;
- default-off AC6 runbook passes.

Commit suggestion:

```text
test(m12): validate submission-scoped resource tracker in probes
```

---

# Phase Group 6 — One Behavior Fix at a Time

Goal: land only fixes proven by diagnostics/probes.

## Phase 6A — UAV Clear Semantics Fix

Allowed:

- fix only proven UAV clear semantic mismatches:
  - CPU/GPU descriptor pair handling;
  - 16-byte clear pattern handling;
  - resource state validation;
  - typed/raw/structured behavior.

Forbidden:

- no counter retention unless Phase 4A proves it;
- no ExecuteIndirect changes;
- no barrier rewrite.

Validation:

- Phase 4A probe matrix passes;
- default-off/no-log AC6 visual launch after full runbook.

Commit suggestion:

```text
fix(m12): match D3D12 UAV clear semantics narrowly
```

## Phase 6B — UAV Counter Handling Fix

Allowed:

- if proven necessary, handle UAV counter resource lifetime/hazard for descriptor-table UAVs only.

Forbidden:

- no root UAV counter handling; D3D12 root UAVs cannot have counters;
- no generic UAV retention.

Validation:

- counter-specific probe passes;
- AC6 only after explicit approval.

Commit suggestion:

```text
fix(m12): handle UAV counter resources narrowly
```

## Phase 6C — ExecuteIndirect Semantics Fix

Allowed:

- fix only proven ExecuteIndirect semantic gaps:
  - argument/count buffer state validation;
  - count clamp;
  - stride/offset alignment;
  - root view NULLing/restoration;
  - root constants and VBV/IBV updates.

Forbidden:

- no ICB/batching in this phase;
- no argument/count lifetime retention;
- no texture/render attachment changes.

Validation:

- Phase 4B probe matrix passes;
- default-off AC6 visual launch after full runbook.

Commit suggestion:

```text
fix(m12): match execute indirect replay semantics narrowly
```

## Phase 6D — ExecuteIndirect Batching / ICB Prototype

Allowed:

- offline prototype of either:
  - looped indirect replay;
  - Metal ICB encoding;
  - batched state expansion inspired by vkd3d-proton/Dawn.

Forbidden:

- no AC6 default path change;
- no performance claims without measurements.

Validation:

- offline probe compares correctness and timing for each path;
- choose path per signature complexity, not globally.

Commit suggestion:

```text
test(m12): prototype execute indirect batching paths
```

## Phase 6E — Barrier Fixes, One Type Per Commit

Subphases:

- `6E.1` transition barriers
- `6E.2` UAV barriers
- `6E.3` aliasing barriers
- `6E.4` split barriers
- `6E.5` COMMON promotion/decay

Allowed:

- fix one barrier type at a time, only after Phase 4D evidence.

Forbidden:

- no all-barrier rewrite;
- no broad lifetime retention as a barrier substitute.

Validation:

- relevant probe passes;
- full runbook and AC6 launch approval for each behavior change.

Commit suggestion examples:

```text
fix(m12): order UAV barriers without broad retention
fix(m12): handle aliasing barriers with explicit Metal hazards
```

## Phase 6F — Present / Drawable Lifetime Diagnostics and Fix

Allowed:

- revisit present/drawable lifetime only after replay and barrier diagnostics;
- default-off diagnostics first;
- any fix must retain only already-existing drawable/backbuffer objects and release on completion.

Forbidden:

- no present timing/wait policy changes;
- no sleeps;
- no forced waits.

Validation:

- probe-present-windowed first;
- full AC6 runbook with approval.

Commit suggestion:

```text
test(m12): diagnose present drawable lifetime
fix(m12): retain present drawable lifetime narrowly
```

---

# Phase Group 7 — AC6 Continue / Real-World GPU-Fault Work

Goal: attack the real target failure: AC6 already reaches title/menu, then risks GPU/MMU/firmware lockup after **Continue** enters real-world rendering/computation.

## Phase 7A — Title/Menu Sanity Gate

Purpose: quick confirmation that the known-good pre-Continue state still works after a rebuild/stage. This is not the main bug.

Allowed:

- clean runbook launch at current committed baseline;
- short monitor sufficient to reach title/menu;
- user visual confirmation.

Forbidden:

- no diagnostics unless explicitly approved;
- no code changes;
- do not spend roadmap time treating title/menu as unknown unless it regresses.

Validation:

- reaches expected visible title/menu baseline;
- launch classifier records phase reached as `title-menu-ok`.

## Phase 7B — Continue → Real-World Transition Bounded Run

Allowed:

- after 7A sanity confirmation, run the exact Continue path under bounded monitor;
- collect process/log evidence only;
- classify the first post-Continue real-world rendering/computation failure point.

Forbidden:

- no lldb/sysdiagnose unless explicitly approved;
- no repeated GPU-fault reproduction loops without approval/cooldown;
- do not relabel a post-Continue lockup as title/menu failure.

Validation:

- classify hang/fault precisely: transition loading, first world render, compute/dispatch, present, barrier/hazard, or firmware/MMU lockup.

## Phase 7C — Post-Continue Fault Capture

Allowed:

- if approved, collect lldb backtraces, WindowServer/GPU logs, Metal error logs, and DXMT breadcrumbs.

Forbidden:

- no broad code changes during capture;
- no repeated GPU-fault runs without cooldown/approval.

Validation:

- capture identifies last successful replay stage and last command class.

## Phase 7D — Fault-Specific Fixes

Allowed:

- apply one fix at a time from Phase 6 evidence to the command class implicated by 7C.

Forbidden:

- no speculative retention bundle;
- no root-table broad scan unless fault evidence specifically proves root-table lifetime.

Validation:

- offline probe first;
- AC6 bounded run after full Slice-2 runbook and launch approval.

---

# Phase Group 8 — Process and Documentation Hygiene

## Phase 8A — Retire Stale Roadmaps

Allowed:

- mark old `m12-slice4-recovery-forward-roadmap.md` as superseded by this v2 roadmap;
- keep it as historical evidence.

Forbidden:

- do not delete evidence paths.

Validation:

- no ambiguity about active roadmap.

Commit suggestion:

```text
docs(m12): mark stale slice4 roadmap superseded
```

## Phase 8B — Evidence Index

Allowed:

- maintain a short index of:
  - stable baselines;
  - failed patches;
  - runbook evidence roots;
  - launch logs;
  - visual confirmations.

Forbidden:

- no raw logs committed;
- no copied `/tmp` manifests wholesale.

Validation:

- future agents can find the last known-good and last known-bad state quickly.

Commit suggestion:

```text
docs(m12): index AC6 M12 baseline evidence
```

## Phase 8C — Roadmap Review Gate

Allowed:

- run `autoreview` or reviewer agents on this roadmap before implementation;
- update phase order if reviewers find contradictions.

Forbidden:

- no implementation before roadmap review if the next phase is behavior-changing.

Validation:

- review findings resolved or explicitly deferred.

Commit suggestion:

```text
docs(m12): refine forward roadmap from review
```

---

# Failure Protocol

If any future phase fails:

1. Stop immediately.
2. Preserve:
   - patch/diff;
   - launch response;
   - launch log;
   - process state;
   - runbook evidence;
   - classifier output.
3. Do not kill processes unless explicitly approved.
4. Revert only the failed phase files.
5. Restore last committed visual baseline through the full Slice-2 runbook.
6. Ask for explicit launch approval.
7. Require user visual confirmation before proceeding.
8. Update this roadmap/evidence index with the failure classification.

# Immediate Recommendation

Start with **Phase 1B** and **Phase 1D**, then use **Phase 7A** only as a quick sanity gate before moving to **Phase 7B**.

Reason: title/menu is already known to load; the important target is the Continue → real-world GPU/MMU/firmware lockup. We still need launch/progression classification so future failures are not misattributed to the wrong phase.

After 1B/1D, run **Phase 7A** to confirm the title/menu baseline, then **Phase 7B** to capture the real post-Continue failure. Only then proceed to Phase Group 2/3 diagnostics targeted at the implicated command classes.
