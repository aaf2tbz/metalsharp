# M12 Core Convergence C8 Completion Audit

Date: 2026-06-18
Branch: `fix/m12-shader-probe-lab`

## Objective restatement

Complete **C8 — Expand native replay coverage while thinning PE** by moving replay coverage policy into `libm12core` scalar planning while preserving PE COM facade and whole-list fallback.

## Checklist

| Requirement | Evidence | Status |
|---|---|---|
| Plan before implementation | `m12-core-convergence-c8-plan.md` | PASS |
| ABI C/POD/scalar-only | `M12CoreReplayCoverageDesc/Summary` use scalar integer fields only. | PASS |
| Build/feature advanced | `M12CORE_BUILD_ID_HIGH=0x0000001a`; `M12CORE_FEATURE_EXPANDED_NATIVE_REPLAY_COVERAGE=1u<<30`; layer feature `1ull<<21`. | PASS |
| Native planner implemented | `m12core_plan_replay_coverage` in `m12core.cpp`. | PASS |
| Transport bridge append-only | unixcall `169 WMTM12CorePlanReplayCoverage` through PE thunk and native unix dlsym/dispatch tables. | PASS |
| Safe stream native coverage planned | Native probe `replay_coverage_safe_native_policy=true`: native covered 4, PE fallback 0, policy native 4, COM facade preserved, transport thin. | PASS |
| Unsupported/copy remains PE fallback | Native probe `replay_coverage_copy_pe_fallback=true`: native covered 0, PE fallback 4, unsupported >=1, COM facade preserved. | PASS |
| PE diagnostics emitted | Command replay gate-on log contains `M12_REPLAY_COVERAGE_THIN_PE ... com_facade_preserved=1 ...`. | PASS |
| Detection updated | Detection passed from refreshed `out/bin` with build high `0x0000001a`, native features `0x7fffffff`, layer flags `0x00000000003fffff`. | PASS |
| No visual/game launch | Only build, probes, detection, command replay smoke. | PASS |

## Validation commands run

```bash
clang-format --dry-run --Werror ...
python3 -m py_compile tools/d3d12-metal-sdk/scripts/probe-m12-convergence-c1.py
bash -n tools/d3d12-metal-sdk/scripts/m12-dev.sh
git diff --check
./tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime
./tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime
./tools/d3d12-metal-sdk/scripts/m12-dev.sh convergence-probe
./tools/d3d12-metal-sdk/scripts/build-probes.sh
# probe_m12_detection.exe from refreshed tools/d3d12-metal-sdk/out/bin
# command replay gate-on smoke
```

## Evidence artifacts

- Native convergence probe: `tools/d3d12-metal-sdk/results/m12-convergence-c1-probe-20260618-003422.json`
  - `ok=true`
  - `build_id_high=0x0000001a`
  - `feature_flags=0x7fffffff`
  - `replay_coverage_safe_native_policy=true`
  - `replay_coverage_copy_pe_fallback=true`
- Detection probe: `tools/d3d12-metal-sdk/results/probe-m12-detection-metalsharp.json`
  - `pass=true`
  - `m12core_build_id_high=0x0000001a`
  - `m12core_feature_flags=0x7fffffff`
  - `feature_flags=0x00000000003fffff`
  - `build_string=MetalSharp DXMT M12 convergence-c8 replay abi=1`
- Command replay log: `/tmp/m12-c8-command-replay-gateon.out`

## Verdict

C8 is complete as a measured native replay coverage / PE-thinning policy slice. Native core now owns scalar coverage policy and PE logs expose native-covered vs fallback work while preserving PE COM facade and fallback execution.
