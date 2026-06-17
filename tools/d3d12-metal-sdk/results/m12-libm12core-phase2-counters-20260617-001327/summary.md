# libm12core Phase 2 counter ABI foundation

## Implemented

- Extended `m12core.h` with append-only stable counter IDs matching the existing `PSO_PRESSURE` vocabulary.
- Added native `M12CoreCounterSnapshot` ABI struct.
- Added native core counter functions:
  - `m12core_record_counter(counter_id, delta)`
  - `m12core_get_counters(out_snapshot)`
  - `m12core_reset_counters()`
- Updated `m12core_build_string()` and version feature flags:
  - `M12CORE_FEATURE_INERT_LOADER`
  - `M12CORE_FEATURE_COUNTERS`
- Updated `winemetal_unix.c` loader to resolve `m12core_record_counter` and record `M12CORE_COUNTER_LOADER_LOAD_SUCCESS` after a successful version check.
- Added code comments documenting this as the Phase 2 diagnostics ownership seam. Rendering, shader, PSO, binding, command replay, and presenter ownership are not moved yet.

## Validation

- `tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight`
- Direct x86_64 `libm12core.dylib` counter ABI probe:
  - `tools/d3d12-metal-sdk/results/m12-libm12core-phase2-counters-20260617-001327/direct-counter-probe.txt`
  - observed: `abi=1 features=0x3 build=libm12core phase2 counters abi=1 counter1=7 counter16=1000 count=17`
- Direct x86_64 `winemetal.so` loader probe with `DXMT_M12CORE_ENABLE=1`:
  - `tools/d3d12-metal-sdk/results/m12-libm12core-phase2-counters-20260617-001327/direct-loader-probe.txt`
  - observed: `[m12core] loaded path=@loader_path/libm12core.dylib abi=1 features=0x3 build_id=00000002:4d313243 build=libm12core phase2 counters abi=1`
- AC6 bounded 60s smoke with strict staged hashes:
  - run: `tools/d3d12-metal-sdk/results/perf-runs/armored-core-vi-smoke-20260617-001327/`
  - drawn/present: `20/20`
  - render/compute/DXIL/unsafe failures: `0`

## Staged runtime hashes

- stage manifest: `tools/d3d12-metal-sdk/results/stage-runtime-metalsharp.json`
- `d3d12.dll`: `f1626f1186d95904ac3c7c6ee71379499fc2342db64afc4b7e4d1ce529fcf3c0`
- `dxgi.dll`: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- `dxgi_dxmt.dll`: `74dffe32c3eb53457b38922cf7c1266df7ed568bbb8e60d4a4b3e5b3955029f9`
- `winemetal.dll`: `7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85`
- `winemetal.so`: `22c2060dec0247846df42773a3aa4c0e9938c27e82fd8bc7e59478dafb0cc616`
- `libm12core.dylib`: `5b0bdc00925c67c8495c768fa46de97820231403a39aa46c714e7c95bf348b09`

## Remaining Phase 2 work

The native core now owns counter storage and the loader records its success counter, but the D3D12 PE hot-path `PSO_PRESSURE` counters still live in `d3d12_device.cpp` and `d3d12_pipeline_state.cpp`. The next step is adding a low-overhead PE-to-Unix counter thunk or batched snapshot bridge before moving those increments into `libm12core`.
