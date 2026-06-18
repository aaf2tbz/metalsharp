# M12 Elden Ring Optimization Roadmap

Date: 2026-06-18
Primary target: Elden Ring (`appid=1245620`)

## Baseline

Latest logged 3-minute baseline:

- Run: `tools/d3d12-metal-sdk/results/elden-ring-optimization-baseline-20260618-014434/`
- Report: `tools/d3d12-metal-sdk/results/elden-ring-optimization-baseline-20260618-014434/optimization-baseline-report.md`
- Launch log: `/Users/alexmondello/.metalsharp/compatdata/1245620/logs/launch-1781768684.log`

Observed:

- Launch OK: true
- Presents: 22 over 180 seconds
- Drawn presents: 22
- Graphics PSOs compiled: 718
- Compute PSOs compiled: 0
- Render/compute PSO failures: 0 / 0
- SM50 / DXIL->MSL failures: 0 / 0
- Unixcall failures: 0
- Unsafe draw skips: 0
- Tessellation fallback: 58
- Process CPU average/peak: about 108% / 149%
- RSS: about 1.85-1.99 GiB

Interpretation: correctness is stable; performance is dominated by startup/load pipeline work. This is now an optimization problem, not an ownership/correctness problem.

## Ground rules

- Elden Ring is the only active optimization target for now.
- Preserve live shader caches unless explicitly testing cache invalidation/source-compile behavior.
- Future perf runs are expected to be 3 minutes.
- Logging/tracing/counters should be active for diagnostic runs.
- M12Core must be used by default for dry-runs, live runs, and bounded launch staging.
- No raw cache payloads, logs, generated corpora, screenshots, or result directories are committed.
- Subnautica 2 remains deferred unless explicitly approved.

## Metric gates

For every optimization slice, record:

1. 3-minute bounded run path.
2. Launch log path.
3. Present count / drawn present count.
4. Graphics/compute PSOs compiled.
5. Cache artifacts created.
6. Failure counts: PSO, SM50, DXIL->MSL, unixcall, unsafe draw.
7. Process CPU avg/p95/max and RSS avg/max.
8. Specific hypothesis result: better / neutral / worse.

Primary metric for now: more drawn presents in 180 seconds with no new failures.

Secondary metrics:

- lower PSO compile count during repeat warm-cache runs
- lower CPU p95 during load
- fewer fallback lines when fallback is not required
- stable visual output

## O1 — Measurement harness hardening

Goal: make Elden Ring perf runs comparable.

Tasks:

- Add a dedicated Elden optimization runner or mode that always uses:
  - `dxmt_metal12`
  - M12Core enabled
  - 180-second duration
  - process sampling CSV
  - trace/log/counter capture on
- Emit a compact `optimization-report.md` after every run.
- Include exact environment in each report.
- Add a helper to compare two reports and show deltas.

Acceptance:

- One command produces run + markdown report.
- Delta helper compares baseline vs candidate.

## O2 — Cache warm-start validation and prewarm expansion

Hypothesis: startup is dominated by 700+ graphics PSO compiles; we need to reduce first-load and repeat-load compile work.

Tasks:

- Run two back-to-back Elden 180s logged runs without clearing caches.
- Compare new artifacts and graphics PSO compile counts.
- Verify whether `M12_CACHE_WARM_START` appears and whether shader/pipeline/prewarm work is skipped.
- If warm-start is not active for Elden, add an Elden prewarm profile equivalent to the AC6 canary path.
- Route Elden cache compatibility/invalidation proof through `libm12core` summaries, not PE ad-hoc checks.

Acceptance:

- Repeat warm-cache run shows reduced compile count or explains why not.
- Cache miss/invalidation reason is explicit in logs.

## O3 — PSO worker and async compile matrix

Hypothesis: default `DXMT_D3D12_PSO_WORKERS=1` underfeeds startup pipeline creation.

Tasks:

Run a small controlled matrix, one 180s run each:

- workers=1 async=1 (baseline)
- workers=2 async=1
- workers=4 async=1
- workers=1 async=0
- workers=2 async=0 only if async=1 results are inconclusive

Keep caches preserved and record deltas.

Acceptance:

- Select a better default or prove worker count is not the bottleneck.
- No new PSO/compile failures.

## O4 — Pipeline creation hot-path profiling

Hypothesis: even with cache, PE/D3D12 pipeline creation does too much repeated scalar work.

Tasks:

- Add low-overhead counters/timers, gated by env, around:
  - root signature summary/build binding plan
  - root binding cache lookup
  - shader cache lookup
  - DXIL->MSL lowering
  - Metal library/function creation via M12Core
  - render PSO creation
- Emit aggregate timings at process exit or periodic checkpoints.

Acceptance:

- Report identifies top 3 CPU time sinks during Elden startup.
- Instrumentation can be disabled for normal runs.

## O5 — Root binding / descriptor rebinding reduction

Hypothesis: repeated root binding/descriptor work is contributing CPU overhead during early frame setup.

Tasks:

- Use O4 counters to identify repeated root binding plan misses.
- Expand M12Core root binding cache metadata if needed.
- Avoid PE recomputation when compatibility keys match.
- Preserve PE fallback and COM facade.

Acceptance:

- Reduced root binding plan/build count in repeated run.
- No descriptor/visual regressions.

## O6 — Replay/present PE thinning for hot frames

Hypothesis: current C8/C9 planning is correct but still logs many PE fallback policies; safe packets should move from policy-only to lower-overhead native replay where proven safe.

Tasks:

- Analyze `M12_REPLAY_COVERAGE_THIN_PE` fallback reasons during Elden.
- Identify packet classes that are repeatedly safe and still PE-routed.
- Move one class at a time to native execution behind gates.
- Keep whole-list PE fallback mandatory.

Acceptance:

- Increased native-covered packet count in logs.
- Present count improves or CPU p95 drops.
- No visual/correctness regressions.

## O7 — Present pacing / sync path review

Hypothesis: `sync=1` presents and window handoff/swapchain path may be limiting frame progression once startup is past PSO pressure.

Tasks:

- Compare logs around present intervals and command queue drains.
- Test present-related toggles only after O2/O3 are understood:
  - live present
  - autopresent
  - raw blit vs presenter path
- Keep runs bounded and visual-confirmed.

Acceptance:

- Present path impact is measured separately from PSO startup.

## O8 — Lock in better defaults

Once one or more optimizations improve Elden:

- Update M12 defaults for Elden only if title-specific.
- Update general M12 defaults only if AC6 still passes a short sanity check.
- Commit source/config changes only.
- Leave generated run artifacts uncommitted.

## Near-term next step

Start with O1/O2:

1. Add an optimization report generator so every 3-minute Elden run is comparable.
2. Run an immediate warm-cache repeat with current defaults and compare against baseline.
3. If PSO compile count remains high, move directly to O3 worker/async matrix.
