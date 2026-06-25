# Phase 15-18 Unreal-generated DXIL MSC/M12 replay and M12 graphics fix

Date: 2026-06-25

## Scope

This phase used authoritative generated artifacts where available and did **not** treat raw standalone `.usf` DXC failures as Unreal acceptance. Mac-host `PCD3D_SM6` commandlets remain blocked by Phase 14 (`Win64` SDK / `ShaderFormatD3D` / Windows target platform modules unavailable), so the downstream proof lane is a fallback/offline lane based on Unreal-native generated `METAL_SM6` material debug dump artifacts.

## Results

- Unreal-generated material DXIL corpus: `51` DXIL artifacts from the `METAL_SM6` material debug dump.
- Apple Metal Shader Converter 4 replay: `51/51` converted with `--validateAll`.
- Native Metal offline function/PSO creation: `51/51` functions/pipelines created on Apple M4.
- M12 compute PSO replay: `4/4` Unreal-generated compute DXIL PSOs created.
- Binding/root-signature reconciliation: complete for the generated fallback corpus; Unreal debug reflection and MSC4 reflection both have max CBV slot `b4`/`space0`, no reflected SRV/UAV/samplers, and M12 logs report `root_desc=5`, `lookup_checks=5`, `lookup_mismatches=0`.
- Initial M12 graphics PSO replay: `0/35` due M12 DXIL->MSL graphics lowering failure.
- Fixed M12 lowering: vector pointer reinterpret-casts now classify as vector values/zeroes so generated bool assignments use `any(vector != zero)` instead of assigning `bool4` to `bool`.
- Post-fix M12 graphics PSO replay: `35/35`, process return code `0`, no timeout.
- Harness/runtime-selection fix: `m12-dev.sh probes` now passes the M12 runtime directory; `run-probes.sh --profile metalsharp` defaults to `dxmt_m12`/`dxmt-m12` before older `dxmt`, preventing stale `out/bin` DLL shadowing.
- Post-fix full contract rerun: `19/19` required probes passing, compare-contract `PASS`, issues `0`.

## Key evidence

- Phase 15/16 replay summary: `06-results/in-progress/phase15-16-unreal-generated-dxil-msc-m12-replay-20260625-143503/phase15-16-summary.json`
- Initial graphics failure output: `06-results/in-progress/phase15-16-unreal-generated-dxil-msc-m12-replay-20260625-143503/m12-graphics-pso-summary.json`
- Fixed graphics replay: `06-results/in-progress/phase16-m12-unreal-generated-graphics-fix-fast-exit-20260625-151216/graphics-fast-exit-summary.json`
- Binding/root-signature reconciliation: `06-results/in-progress/phase15-16-unreal-generated-dxil-msc-m12-replay-20260625-143503/pcd3d-sm6-binding-reconciliation.json`
- Full SM6 corpus accounting: `06-results/in-progress/phase17-full-sm6-corpus-accounting-20260625-153942/phase17-full-sm6-corpus-accounting.json`
- Final full contract rerun: `06-results/in-progress/phase18-m12-runtime-selection-fixed-full-contract-rerun-20260625-153326/contract-summary-metalsharp.json`

## Phase 17 accounting summary

Raw SM6-sensitive inventory records: `497`.

- `dependency_include_only`: `180`
- `dependency_no_entrypoint`: `121`
- `requires_unreal_generated_environment`: `189`
- `entrypoint_or_profile_guess_mismatch`: `3`
- `standalone_valid_dxil_msc_control_not_unreal_native_acceptance`: `4` files (`9` successful DXC attempts)

Interpretation: raw standalone failures are accounted as Unreal generated-environment/permutation-input issues or dependency units, not M12 replay failures. Valid generated DXIL artifacts are the M12 replay inputs.

## Contract/cap policy

Final caps remain conservative/honest:

- shader model reported: SM6.5
- WaveOps reported: false
- Int64ShaderOps/atomic64 reported: false
- SM6.6 runtime probes can exist, but public reporting remains denied until policy changes and live/runtime proof justifies it.

## Claim boundary

Proven: source-backed Unreal-generated SM6 material DXIL fallback corpus passes MSC4/native Metal and M12 compute+graphics PSO creation after the fix; M12 required offline contract probes pass again.

Not proven: universal Unreal `PCD3D_SM6` commandlet success, full Unreal SM6 completion, or live Subnautica 2 success.

No live game process was launched.
