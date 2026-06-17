# M12 libm12core Phase 7/8 Completion Audit

Date: 2026-06-17
Branch: `fix/m12-shader-probe-lab`

## Scope audited

Active goal: complete Phase 7 and Phase 8 with careful slices, clean commits,
auto-review, dry-run staging, and one bounded 150s AC6 launch at the end.

- Phase 7: command replay planning only. Move draw planning/classification/keying
  into `libm12core` and bridge it as diagnostics from PE replay; do not migrate
  command execution.
- Phase 8: expose a public translation-layer detection interface via a stable
  custom GUID queried through `ID3D12Device::QueryInterface`.

## Commits

- `db7aab7 feat: summarize m12 draw plans`
  - Added scalar native `M12CoreDrawPlanDesc` / `M12CoreDrawPlanSummary` ABI.
  - Added `M12CORE_FEATURE_DRAW_PLANNING`.
  - Added `m12core_build_draw_plan`.
- `15382e2 feat: bridge m12 draw plan diagnostics`
  - Added PE/unix bridge `WMTM12CoreBuildDrawPlan` on unixcall `152`.
  - Stored stable render/compute PSO cache keys for replay diagnostics.
  - Logged bounded `M12_DRAW_PLAN` diagnostics from draw replay paths only.
- `ef7970c feat: expose m12 translation layer info`
  - Added `IID_IMetalSharpM12TranslationLayerInfo`.
  - Added versioned POD `MetalSharpM12TranslationLayerInfo`.
  - Exposed it through `MTLD3D12Device::QueryInterface`.
  - Centralized `M12CORE_BUILD_ID_LOW/HIGH` and `M12CORE_FEATURE_ALL` in `m12core.h`.
- `4b96f0a test: add m12 translation layer probe`
  - Added `probe_m12_detection.exe`.
  - Wired the probe into `build-probes.sh`.

## Requirement matrix

| Requirement | Evidence | Result |
|---|---|---|
| Phase 7 stays planning-only, no command execution migration | Draw execution branches remain unchanged; `LogM12CoreDrawPlan()` only builds scalar descriptors, calls `WMTM12CoreBuildDrawPlan`, and logs summaries. Auto-review after slice 2 found no command execution movement. | Pass |
| C/POD-only PE/native ABI | `M12CoreDrawPlanDesc` and `M12CoreDrawPlanSummary` contain only `uint32_t`/`uint64_t`; no command-list objects, descriptors, Metal objects, resource handles, STL, exceptions, or ownership cross the unixcall. | Pass |
| Fallback-safe when old/missing libm12core/unixcall is unavailable | `WMTM12CoreBuildDrawPlan` returns false on unixcall/native-core failure; caller silently returns without affecting draws. Auto-review verified old-core fallback safety. | Pass |
| Draw-plan diagnostics are bounded | Launch log contained exactly `192` `M12_DRAW_PLAN` lines, matching `TakeLogBudget(&g_draw_plan_logs, 192)`. | Pass |
| Phase 8 uses stable custom GUID, not heuristics | Runtime exposes `IID_IMetalSharpM12TranslationLayerInfo = 4d315232-4d53-4458-8c3a-316fc7427a11`; probe queries this IID through `ID3D12Device::QueryInterface`. | Pass |
| Phase 8 returns versioned POD info | `MetalSharpM12TranslationLayerInfo` has `abi_version`, `struct_size`, vendor/layer IDs, feature flags, m12core ABI/build/features, reserved fields, and fixed strings. | Pass |
| Detection probe validates interface | `tools/d3d12-metal-sdk/results/m12-phase8-detection-probe-20260617-145445/probe-m12-detection-metalsharp.json`: `pass=true`, `query_interface.hr=0x00000000`, `info_call.hr=0x00000000`, `layer_name=MetalSharp DXMT M12`, `m12core_build_id_low=0x4d313243`, `m12core_feature_flags=0x00001fff`. | Pass |
| Build after each source slice | `./tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime` passed for Phase 7 Slice 2 and Phase 8 Slice 1; `./tools/d3d12-metal-sdk/scripts/build-probes.sh` passed for Phase 8 Slice 2. | Pass |
| Auto-review after each slice | Phase 7 Slice 2, Phase 8 Slice 1, and Phase 8 Slice 2 all had final auto-review with no required findings. | Pass |
| Stage runtime + backend dry-run before validation | Final dry-run: `tools/d3d12-metal-sdk/results/m12-phase7-8-final-dryrun-20260617-145717/dry-run-9277.json`; `ok=true`, `pipeline=m12`, `missing=[]`. | Pass |
| Exactly one bounded AC6 launch at the end | Single final launch artifact: `tools/d3d12-metal-sdk/results/m12-phase7-8-final-validation-20260617-145725/bounded-launches/armored-core-vi-20260617-145725/summary.md`; seconds=`150`. | Pass |
| Visual/runtime validation after changes | Final launch summary: `launch_ok=True`, `present_count=23`, `drawn_present_count=23`, `render_pso_failed=0`, `compute_pso_failed=0`, `sm50_compile_failed=0`, `dxil_msl_compile_failed=0`, `unix_call_failed=0`, `unsafe_draw_skips=0`. | Pass |
| Preserve live shader caches / no new raw cache payloads committed | Final launch: `new_dxbc=0`, `new_msl=0`, `new_metallib=0`, `new_pso_render=0`, `new_pso_compute=0`; no result payloads staged. | Pass |

## Final validation artifacts

- Final dry-run:
  - `tools/d3d12-metal-sdk/results/m12-phase7-8-final-dryrun-20260617-145717/dry-run-9277.json`
- Phase 8 detection probe:
  - `tools/d3d12-metal-sdk/results/m12-phase8-detection-probe-20260617-145445/probe-m12-detection-metalsharp.json`
- Final bounded launch:
  - `tools/d3d12-metal-sdk/results/m12-phase7-8-final-validation-20260617-145725/bounded-launches/armored-core-vi-20260617-145725/summary.md`
  - `tools/d3d12-metal-sdk/results/m12-phase7-8-final-validation-20260617-145725/bounded-launches/armored-core-vi-20260617-145725/summary.json`

## Residual risks

- AC6 performance remains poor because many unique graphics PSOs are still
  created at runtime (`graphics_pso_compiled=1430` in the final launch). This is
  unchanged by Phase 7/8 and remains future optimization work.
- Full command replay/presenter migration remains intentionally deferred to the
  optional/high-risk later phase.
- The detection interface currently reports the implemented Phase 7/8 feature
  surface; future phases should append flags rather than repurpose existing bits.

## Audit conclusion

Phases 7 and 8 meet their stated requirements. The final dry-run was green, the
custom detection probe passed, and the one allowed 150-second AC6 launch after
all source changes completed with 23/23 drawn presents and zero runtime failures.
