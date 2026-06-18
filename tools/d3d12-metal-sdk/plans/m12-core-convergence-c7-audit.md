# M12 Core Convergence C7 Completion Audit

Date: 2026-06-18
Branch: `fix/m12-shader-probe-lab`

## Objective restatement

Complete **C7 — Cache-first warm start for shaders/PSOs**:

- Native core decides when compatible cache hits can skip shader/PSO/prewarm work before queuing translation or creation.
- Reuse/skip requires compatibility key and invalidation proof.
- Force-source/cache-disabled/missing-proof paths fall back without skipped work.
- PE diagnostics record skipped-work counters.

## Checklist

| Requirement | Evidence | Status |
|---|---|---|
| Plan before implementation | `m12-core-convergence-c7-plan.md` | PASS |
| ABI remains C/POD/scalar | `M12CoreCacheWarmStartDesc/Summary` use only scalar integers; no payloads or handles cross `libm12core` ABI. | PASS |
| Build/feature advanced | `M12CORE_BUILD_ID_HIGH=0x00000019`; `M12CORE_FEATURE_CACHE_FIRST_WARM_START=1u<<29`; layer feature `1ull<<20`. | PASS |
| Native planner implemented | `m12core_plan_cache_warm_start` in `m12core.cpp`. | PASS |
| Transport bridge append-only | unixcall `168 WMTM12CorePlanCacheWarmStart` through PE thunk and native unix dlsym/dispatch tables. | PASS |
| Compatible hit skips work | Native probe `cache_warm_start_skips_compatible_work=true`: shader skipped 3, PSO skipped 2, prewarm skipped 2, fallback 0. | PASS |
| Missing invalidation proof falls back | Native probe `cache_warm_start_invalidation_fallback=true`: skipped 0, fallback shader 3, fallback PSO 2. | PASS |
| Force-source falls back | Native probe `cache_warm_start_force_source_fallback=true`: skipped 0, fallback shader 3, fallback PSO 2. | PASS |
| Routine cache miss falls back | Native probe `cache_warm_start_cache_miss_fallback=true`: no hit flags, skipped 0, fallback shader 3, fallback PSO 2. | PASS |
| PE skipped-work counters emitted | `/tmp/m12-c7-prewarm-detection.stderr` contains `M12_CACHE_WARM_START cache_first=1 shader_skipped=14 pso_skipped=8 prewarm_skipped=8 fallback_shader=0 fallback_pso=0 invalidated=0`. | PASS |
| Detection updated | `probe_m12_detection.exe` from `out/bin` passed with build high `0x00000019`, feature flags `0x3fffffff`, layer flags `0x00000000001fffff`. | PASS |
| Replay fallback unchanged | Gate-off/gate-on command replay logs remain expected fallback behavior. | PASS |
| No generated results committed | Evidence remains under `results/` or `/tmp`; source/docs/scripts only should be staged. | PASS |

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
# probe_m12_detection.exe from tools/d3d12-metal-sdk/out/bin
# prewarm-profile probe_m12_detection.exe with SteamAppId=1888160 and METALSHARP_M12_PREWARM_PROFILE=armored-core-vi-phase6-canary
# command replay gate-off and gate-on smoke
```

## Evidence artifacts

- Native convergence probe: `tools/d3d12-metal-sdk/results/m12-convergence-c1-probe-20260618-002725.json`
  - `ok=true`
  - `build_id_high=0x00000019`
  - `feature_flags=0x3fffffff`
  - `cache_warm_start_skips_compatible_work=true`
  - `cache_warm_start_invalidation_fallback=true`
  - `cache_warm_start_force_source_fallback=true`
  - `cache_warm_start_cache_miss_fallback=true`
- Detection probe: `tools/d3d12-metal-sdk/results/probe-m12-detection-metalsharp.json`
  - `pass=true`
  - `m12core_build_id_high=0x00000019`
  - `m12core_feature_flags=0x3fffffff`
  - `feature_flags=0x00000000001fffff`
  - `build_string=MetalSharp DXMT M12 convergence-c7 cache abi=1`
- PE warm-start log: `/tmp/m12-c7-prewarm-detection.stderr`
- Command replay logs:
  - `/tmp/m12-c7-command-replay-gateoff.out`
  - `/tmp/m12-c7-command-replay-gateon.out`

## Verdict

C7 is complete as a cache-first warm-start planning slice. The native core now owns scalar skip/fallback decisions with compatibility and invalidation proof, PE diagnostics expose skipped-work counters, and all cache payload reuse remains guarded by fallback/invalidation rules.
