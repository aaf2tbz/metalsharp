# M12 Core Convergence C9 Completion Audit

Date: 2026-06-18
Branch: `fix/m12-shader-probe-lab`

## Objective restatement

Complete **C9 — Thin PE DLL checkpoint** by making the final PE/native responsibility split explicit, scalar-planned, probeable, and ready for C10 visual confirmation.

## Final module roles

| Module | C9 role | Status |
|---|---|---|
| `d3d12.dll` | COM wrappers + command serialization | Confirmed |
| `dxgi.dll` | bootstrap/wrapper | Confirmed |
| `dxgi_dxmt.dll` | DXGI wrappers + bridge | Confirmed |
| `winemetal.dll` | thunk transport | Confirmed |
| `winemetal.so` | native loader/native object bridge | Confirmed |
| `libm12core` | runtime/renderer/cache policy owner | Confirmed |
| PE fallback | mandatory fallback emitter | Confirmed |

## Checklist

| Requirement | Evidence | Status |
|---|---|---|
| Plan before implementation | `m12-core-convergence-c9-plan.md` | PASS |
| ABI C/POD/scalar-only | `M12CoreThinPECheckpointDesc/Summary` use scalar integer fields only. | PASS |
| Build/feature advanced | `M12CORE_BUILD_ID_HIGH=0x0000001b`; `M12CORE_FEATURE_THIN_PE_CHECKPOINT=1u<<31`; layer feature `1ull<<22`. | PASS |
| Native planner implemented | `m12core_plan_thin_pe_checkpoint` in `m12core.cpp`. | PASS |
| Transport bridge append-only | unixcall `170 WMTM12CorePlanThinPECheckpoint`. | PASS |
| PE diagnostic emitted | Command replay smoke includes `M12_THIN_PE_CHECKPOINT ready=1 c10_ready=1 fallback_safe=1 transport_thin=1 obsolete_remaining=0 missing=0x0`. | PASS |
| Probe coverage | Native convergence probe has `thin_pe_checkpoint_feature=true` and `thin_pe_checkpoint_c10_ready=true`. | PASS |
| Detection updated | Detection passes with feature flags `0x00000000007fffff` and native flags `0xffffffff`. | PASS |
| Runtime staged | `m12-dev.sh stage-runtime` passed and C9 DLLs/sidecars deployed under `~/.metalsharp/runtime/wine/lib/dxmt_m12`. | PASS |
| Preflight gate | `m12-dev.sh preflight` passed. | PASS |
| No visual/game launch | Only build, stage, probes, detection, command replay smoke, preflight. | PASS |

## Validation commands run

```bash
clang-format --dry-run --Werror ...
python3 -m py_compile tools/d3d12-metal-sdk/scripts/probe-m12-convergence-c1.py
git diff --check
./tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime
./tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime
./tools/d3d12-metal-sdk/scripts/m12-dev.sh convergence-probe
./tools/d3d12-metal-sdk/scripts/build-probes.sh
# probe_m12_detection.exe from refreshed tools/d3d12-metal-sdk/out/bin
./tools/d3d12-metal-sdk/scripts/m12-dev.sh probes -- --command-replay-only
./tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight
```

## Evidence artifacts

- Native convergence probe: `/tmp/m12-c9-probe.json`
  - `ok=true`
  - `build_id_high=0x0000001b`
  - `feature_flags=0xffffffff`
  - `thin_pe_checkpoint_feature=true`
  - `thin_pe_checkpoint_c10_ready=true`
- Detection probe: `tools/d3d12-metal-sdk/results/probe-m12-detection-metalsharp.json`
  - `pass=true`
  - `m12core_build_id_high=0x0000001b`
  - `m12core_feature_flags=0xffffffff`
  - `feature_flags=0x00000000007fffff`
  - `build_string=MetalSharp DXMT M12 convergence-c9 thin-pe abi=1`
- PE command replay checkpoint log: `/tmp/m12-c9-command-replay-gateon.out`
- Preflight log: `/tmp/m12-c9-c10-preflight.out`

## Verdict

C9 is complete. The final PE/native split is now represented by an append-only scalar checkpoint ABI, bridged through unixcall 170, emitted by PE diagnostics, and validated by native probe/detection/preflight. C10 can proceed as visual confirmation only, with no additional source implementation required before the bounded 2.5-minute checks.
