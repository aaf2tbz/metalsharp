# Phase 17 full SM6-sensitive corpus accounting

- Raw SM6-sensitive inventory records: `497`
- `requires_unreal_generated_environment`: `189`
- `dependency_include_only`: `180`
- `dependency_no_entrypoint`: `121`
- `standalone_valid_dxil_msc_control_not_unreal_native_acceptance`: `4`
- `entrypoint_or_profile_guess_mismatch`: `3`

## Interpretation

- Raw standalone DXC failures remain classified as Unreal generated-environment/permutation-input issues, not M12 replay failures.
- Include-only and no-entrypoint units are dependencies; their authoritative lane is an owning Unreal shader job or generated/cache artifact, not standalone compilation.
- The source-backed generated artifact lane is the Unreal-native `METAL_SM6` material debug dump: 51 DXIL artifacts, MSC4 51/51, native Metal PSO 51/51, M12 compute 4/4, and after the M12 fix M12 graphics 35/35.
- Mac-host `PCD3D_SM6` commandlets remain blocked by Phase 14 module/SDK absence; this accounting is therefore fallback/offline evidence, not universal PCD3D_SM6 completion.

## Evidence
- raw_standalone_aggregate: `06-results/completed/20260625-phase4-sm6-dxc-msc-aggregate/sm6-dxc-msc-aggregate.json`
- fallback_generated_artifact_lane: `06-results/in-progress/phase15-16-unreal-generated-dxil-msc-m12-replay-20260625-143503/phase15-16-summary.json`
- m12_graphics_fix_fast_exit_summary: `06-results/in-progress/phase16-m12-unreal-generated-graphics-fix-fast-exit-20260625-151216/graphics-fast-exit-summary.json`
- full_contract_after_fix: `06-results/in-progress/phase18-m12-runtime-selection-fixed-full-contract-rerun-20260625-153326/contract-summary-metalsharp.json`

## Reason counts
- `no standalone entrypoint; compile through owning top-level Unreal shader job`: `180`
- `standalone_lwc_doublefloat_platform_context_mismatch`: `180`
- `shader source unit has no reliable standalone entrypoint candidate`: `121`
- `standalone_missing_unreal_permutation_define_or_uniform`: `5`
- `raw standalone DXC produced DXIL and MSC converted; kept as control evidence only`: `4`
- `standalone_compile_failed_without_unreal_shader_environment`: `3`
- `standalone_entrypoint_guess_not_authoritative`: `3`
- `standalone_missing_unreal_virtual_generated_includes`: `1`
