# M12 Core Convergence C6 Completion Audit

Date: 2026-06-17
Branch: `fix/m12-shader-probe-lab`

## Objective restatement

Complete **C6 — Native presenter ownership + thin transport boundary** while preserving default-off fallback behavior:

- Expand native present ownership beyond the existing raw present-blit primitive with native scalar sequencing/transport planning.
- Keep `winemetal.dll` as a transport-oriented thunk layer.
- Let `winemetal.so`/`libm12core` own native present decision records where safe.
- Prevent double-present and double-blit when native execution has already scheduled present work.

## Prompt-to-artifact checklist

| Requirement | Evidence | Status |
|---|---|---|
| C6 planned before implementation | `m12-core-convergence-c6-plan.md` | PASS |
| ABI remains C/POD/scalar | `M12CoreNativePresentOwnershipDesc/Summary` contain only integers; no COM/C++/ObjC/Metal ownership crosses `libm12core` ABI. | PASS |
| Build/feature advanced | `M12CORE_BUILD_ID_HIGH = 0x00000018`; feature `M12CORE_FEATURE_NATIVE_PRESENT_OWNERSHIP = 1u << 28`; layer feature `1ull << 19`. | PASS |
| Native C6 planner exists | `m12core_plan_native_present_ownership` in `m12core.cpp`. | PASS |
| winemetal transport is append-only | unixcall `167 WMTM12CorePlanNativePresentOwnership`; PE thunk copies scalar desc only; native unix side dlsym/calls `m12core_plan_native_present_ownership`. | PASS |
| Default-off fallback preserved | Native probe `native_present_gate_off_fallback=true`; command replay gate-off still falls back (`M12_PROBE_REPLAY_EXECUTE gate=0 ... fallback=1`). | PASS |
| Safe raw present ownership planned | Native probe `native_present_raw_owned=true`; planned one blit and one present, `transport_thin=1`, `pe_present_required=0`. | PASS |
| Presenter/non-raw path falls back | Native probe `native_present_presenter_fallback=true`, fallback `NON_RAW_PATH`, PE present required. | PASS |
| Double-present/double-blit prevention proven | Native probe `native_present_double_prevented=true`; summary planned zero extra blits/presents after native-present-executed state and records both prevention flags. | PASS |
| PE runtime diagnostics added | `d3d12_swapchain.cpp` emits `M12_NATIVE_PRESENT_OWNERSHIP` for `presenter-fallback`, `raw-pre-execute`, and `raw-post-native` phases. | PASS |
| Detection probe updated | `probe_m12_detection.cpp` requires build high `0x00000018`, native core feature `0x1fffffff`, and layer feature flags `0x00000000000fffff`. | PASS |
| No visual/game launch run | Only build, probes, detection, and command replay smoke were run. | PASS |

## Validation commands run

```bash
clang-format --dry-run --Werror vendor/dxmt/src/m12core/m12core.cpp vendor/dxmt/src/m12core/m12core.h vendor/dxmt/src/winemetal/winemetal.h vendor/dxmt/src/winemetal/winemetal_thunks.c vendor/dxmt/src/winemetal/winemetal_thunks.h vendor/dxmt/src/winemetal/unix/winemetal_unix.c vendor/dxmt/src/d3d12/d3d12_device.cpp vendor/dxmt/src/d3d12/d3d12_device.hpp vendor/dxmt/src/d3d12/d3d12_swapchain.cpp tools/d3d12-metal-sdk/probes/probe_m12_detection/probe_m12_detection.cpp
python3 -m py_compile tools/d3d12-metal-sdk/scripts/probe-m12-convergence-c1.py
bash -n tools/d3d12-metal-sdk/scripts/m12-dev.sh
git diff --check
./tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime
./tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime
./tools/d3d12-metal-sdk/scripts/m12-dev.sh convergence-probe
./tools/d3d12-metal-sdk/scripts/build-probes.sh
DXMT_RUNTIME="$HOME/.metalsharp/runtime/wine/lib/dxmt_m12" DXMT_M12CORE_ENABLE=1 DXMT_M12CORE_PATH="$HOME/.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-unix/libm12core.dylib" ./tools/d3d12-metal-sdk/scripts/m12-dev.sh probes -- --command-replay-only
DXMT_RUNTIME="$HOME/.metalsharp/runtime/wine/lib/dxmt_m12" DXMT_M12CORE_ENABLE=1 DXMT_M12CORE_REPLAY_EXECUTE=1 DXMT_M12CORE_PATH="$HOME/.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-unix/libm12core.dylib" ./tools/d3d12-metal-sdk/scripts/m12-dev.sh probes -- --command-replay-only
# Direct probe_m12_detection.exe under staged dxmt_m12 runtime.
```

## Evidence artifacts

- Native convergence probe: `tools/d3d12-metal-sdk/results/m12-convergence-c1-probe-20260617-235532.json`
  - `ok=true`
  - `build_id_high=0x00000018`
  - `feature_flags=0x1fffffff`
  - `native_present_gate_off_fallback=true`
  - `native_present_raw_owned=true`
  - `native_present_presenter_fallback=true`
  - `native_present_double_prevented=true`
- Detection probe: `tools/d3d12-metal-sdk/results/probe-m12-detection-metalsharp.json`
  - `pass=true`
  - `m12core_build_id_high=0x00000018`
  - `m12core_feature_flags=0x1fffffff`
  - `feature_flags=0x00000000000fffff`
  - `build_string=MetalSharp DXMT M12 convergence-c6 present abi=1`
- Command replay smoke logs:
  - `/tmp/m12-c6-command-replay-gateoff.out`
  - `/tmp/m12-c6-command-replay-gateon.out`

## Residual work for later slices

- C7 enables cache-first warm-start metadata/skip decisions.
- C8 expands native replay coverage and thins PE renderer policy further.
- C9 records final thin PE checkpoint.
- C10 remains the visual confirmation stage and was not run in C6.

## Verdict

C6 is complete as a fallback-safe native present ownership planning slice. Native-side C/POD planning now records raw-present ownership, presenter fallback, thin transport status, and double-present/double-blit prevention while PE fallback remains authoritative unless gated native execution is safe.
