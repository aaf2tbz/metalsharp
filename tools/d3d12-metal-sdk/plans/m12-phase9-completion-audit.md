# M12/libm12core Phase 9 Completion Audit

## Final status

Phase 9 is complete and kept.

User-approved Slice 7 visual confirmation was completed on 2026-06-17 for the two required already-running runtime regression targets:

- Elden Ring (`1245620`): visual output confirmed by user after a 150 second bounded M12 launch.
- Armored Core VI (`1888160`): visual output confirmed by user after a 150 second bounded M12 launch.

No regression was found on the already-running runtime. Subnautica 2 remains a separate follow-up investigation and is not a Phase 9 blocker; the current priority is hardening the M12 pipeline so Subnautica 2 can be debugged more easily later. PEAK and Schedule I were not required for Slice 7 because they currently prefer a DX11 runtime path.

## Scope completed

Phase 9 moved more M12 execution planning and eligibility ownership into `libm12core` while preserving safe PE/DXMT fallback at every migration seam:

1. Native present planning/classification.
2. Native command-replay planning/classification.
3. Native command-stream descriptor validation, shadow-only.
4. Native render-pass/hazard planning, shadow-only.
5. Native present execute planning plus default-off gated raw blit/present execution seam.
6. Native replay execute eligibility/support classification plus default-off future execution gate.
7. User-approved bounded visual rollout for Elden Ring and AC6.

Actual arbitrary D3D12 command replay, Metal encoder lifetime, drawable acquisition, command-buffer commits, synchronization, and resource hazard execution remain on the existing PE/DXMT path unless a narrow opt-in gate explicitly proves support and falls back safely.

## Ground rules verified

| Requirement | Evidence |
|---|---|
| `libm12core` ABI remains C/POD-only | New `M12Core*Desc` and `M12Core*Summary` structs are scalar/POD only; Metal/object handles remain in winemetal-native unixcall seams, not `libm12core`. |
| PE fallback remains available | `DXMT_M12CORE_PRESENT_EXECUTE` and `DXMT_M12CORE_REPLAY_EXECUTE` are default-off; unavailable/older/invalid native core paths fall back to PE/DXMT. |
| Append-only unixcall growth | Phase 9 added unixcalls `153` through `159` without reordering earlier calls. |
| No raw cache/corpus payloads committed | No raw D3DMetal cache payloads, extracted metallibs, DXBC blobs, or large corpora are committed. Per-run result payloads remain local evidence; small tracked manifests may still reference local evidence paths. |
| Runtime staged explicitly | Staged runtime target: `~/.metalsharp/runtime/wine/lib/dxmt_m12`; evidence: `tools/d3d12-metal-sdk/results/stage-runtime-metalsharp.json`. |
| Dry-run before launch | Slice 7 dry-runs were run before each bounded launch. |
| Game launches required approval | Slice 7 launches were performed only after explicit user approval. |

## Feature and ABI evidence

Final Phase 9 feature/build surface:

- `M12CORE_BUILD_ID_HIGH = 0x00000013`.
- `M12CORE_FEATURE_PRESENT_PLANNING`.
- `M12CORE_FEATURE_REPLAY_PLANNING`.
- `M12CORE_FEATURE_COMMAND_STREAM_DESCRIPTORS`.
- `M12CORE_FEATURE_RENDER_PASS_HAZARD_PLANNING`.
- `M12CORE_FEATURE_PRESENT_EXECUTE_PLANNING`.
- `M12CORE_FEATURE_REPLAY_EXECUTE_PLANNING`.

Final Phase 9 unixcall allocation:

- `153`: `WMTM12CoreBuildPresentPlan`.
- `154`: `WMTM12CoreBuildReplayPlan`.
- `155`: `WMTM12CoreValidateCommandStream`.
- `156`: `WMTM12CorePlanRenderPass`.
- `157`: `WMTM12CorePlanPresentExecute`.
- `158`: `WMTM12CoreExecutePresentBlit`.
- `159`: `WMTM12CorePlanReplayExecute`.

## Validation evidence

### Build/probe/dry-run evidence

- Runtime build passed during slices 2-5: `./tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`.
- Probe build passed during slices 2-5: `./tools/d3d12-metal-sdk/scripts/build-probes.sh`.
- Formatting passed for touched C/C++/Obj-C files during each implementation slice.
- Autoreview passed after each substantive source slice; Slice 4 hot-path native-present log spam finding was fixed and re-reviewed.
- Detection evidence at final build high: `tools/d3d12-metal-sdk/results/probe-m12-detection-slice5-metalsharp.log`.
- All-five dry-run evidence through Slice 5: `tools/d3d12-metal-sdk/results/m12-phase9-slice5-dryrun-20260617-170304/summary.json`.

### Slice 7 dry-run and bounded launch evidence

| Game | AppID | Dry-run evidence | Bounded launch evidence | Result |
|---|---:|---|---|---|
| Elden Ring | 1245620 | `tools/d3d12-metal-sdk/results/m12-phase9-slice7-visual-elden-ring-20260617-174531/dry-run-elden-ring-1245620.json` | `tools/d3d12-metal-sdk/results/m12-phase9-slice7-visual-elden-ring-20260617-174531/bounded-launches/elden-ring-20260617-174536/summary.md` | PASS: user confirmed visual output; 22/22 drawn presents; zero PSO/compile/unixcall failures; zero unsafe draw skips. |
| Armored Core VI | 1888160 | `tools/d3d12-metal-sdk/results/m12-phase9-slice7-visual-ac6-20260617-174835/dry-run-armored-core-vi-1888160.json` | `tools/d3d12-metal-sdk/results/m12-phase9-slice7-visual-ac6-20260617-174835/bounded-launches/armored-core-vi-20260617-174847/summary.md` | PASS: user confirmed visual output; 22/22 drawn presents; zero PSO/compile/unixcall failures; zero unsafe draw skips. |
| Subnautica 2 | 1962700 | `tools/d3d12-metal-sdk/results/m12-phase9-slice7-subnautica2-20260617-174135/dry-run-subnautica-2-1962700.json` | `tools/d3d12-metal-sdk/results/m12-phase9-slice7-subnautica2-20260617-174135/bounded-launches/subnautica2-20260617-174142/summary.md` | Separate follow-up: launch OK and 14/15 drawn presents, but one Metal/XPC compute PSO failure and two zero-stride unsafe draw skips. Not a Phase 9 blocker. |

Native execution gates were intentionally left off for the game launches. `libm12core` diagnostics were enabled with the staged runtime.

## Slice-by-slice commit evidence

- `0fec647 feat: plan m12 presents in native core`.
- `b4754d4 feat: plan m12 command replays in native core`.
- `507f8d0 docs: plan m12 phase9 execution slices`.
- `0460f1e feat: shadow m12 command stream descriptors`.
- `2bff5c4 feat: shadow m12 render pass hazard plans`.
- `e8bbb2f feat: gate m12 native present blits`.
- `0b62cb2 feat: plan m12 replay execute eligibility`.
- `bf0dfa9 docs: record m12 phase9 slice6 readiness`.

## Residual risks and follow-up

- Subnautica 2 needs separate code investigation for compute PSO/XPC instability and zero-stride unsafe draw skips.
- PEAK and Schedule I M12 validation can be revisited later, but are not required for this Phase 9 decision because they currently prefer DX11.
- D3DMetal direct metallib/cache reuse remains unsafe until ABI/resource-layout compatibility is proven.
- Future native execution migration must continue to promote one seam at a time behind dry-run, probe, bounded-launch, visual, and fallback gates.

## Completion decision

Phase 9 is KEEP.

The phase satisfies its explicit requirements: planning preceded implementation, discrepancies were identified and ruled out slice-by-slice, Slices 1-6 were implemented and validated before Slice 7, Slice 7 launches waited for explicit user approval, visual output was confirmed for Elden Ring and AC6, and the final state retains safe PE fallback with no observed regression on the already-running runtime.
