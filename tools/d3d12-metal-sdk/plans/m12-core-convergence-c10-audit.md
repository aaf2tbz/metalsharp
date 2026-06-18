# M12 Core Convergence C10 Completion Audit

Date: 2026-06-18
Branch: `fix/m12-shader-probe-lab`

## Objective restatement

Complete the C-roadmap visual confirmation gate after C9 ownership/checkpoint work, one game at a time, with explicit user confirmation before proceeding.

## User instruction

- Elden Ring first, then wait for user visual confirmation.
- After Elden Ring confirmation, continue to the next game.
- Armored Core VI loaded visually.
- User explicitly declined a Subnautica run for now because it is too slow and is not needed for the ownership milestone.
- User declared: "C-roadmap is complete per my instruction."

## Visual confirmation evidence

| Game | Run | Launch result | Metrics | User visual result | Status |
|---|---|---|---|---|---|
| Elden Ring | `tools/d3d12-metal-sdk/results/m12-c10-visual-elden-ring-20260618-011350/elden-ring-20260618-011350/summary.md` | `launch_ok=True` | 22 presents / 22 drawn, 0 PSO failures, 0 compile failures, 0 unixcall failures, 0 unsafe draw skips | Confirmed visual output | PASS |
| Armored Core VI | `tools/d3d12-metal-sdk/results/m12-c10-visual-armored-core-vi-20260618-011732/armored-core-vi-20260618-011732/summary.md` | `launch_ok=True` | 21 presents / 21 drawn, 0 PSO failures, 0 compile failures, 0 unixcall failures, 0 unsafe draw skips | Confirmed visual output | PASS |
| Subnautica 2 | Not run | Deferred by explicit user instruction | Separate/future compute/unsafe-draw investigation remains | Not applicable | DEFERRED |

## Log scan notes

- Elden Ring launch log: `/Users/alexmondello/.metalsharp/compatdata/1245620/logs/launch-1781766839.log`
  - No M12 failure lines found in the scan.
  - Only notable diagnostic: `mscompatdb:error: couldn't find KeServiceDescriptorTable`.
- Armored Core VI launch log: `/Users/alexmondello/.metalsharp/compatdata/1888160/logs/launch-1781767066.log`
  - No M12 failure lines found in the scan.
  - Only notable diagnostic: `mscompatdb:error: couldn't find KeServiceDescriptorTable`.

## Verdict

C10 is complete for the ownership milestone. Elden Ring and Armored Core VI both launched and produced user-confirmed visual output with clean bounded-run metrics. Subnautica 2 is intentionally deferred and does not block this C-roadmap completion because this milestone was about ownership/seams, not optimization or the separate Subnautica investigation.
