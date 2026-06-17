# libm12core Phase 1 loader

## Implemented

- Added native x86_64 macOS `libm12core.dylib` build target under `vendor/dxmt/src/m12core/`.
- Added documented C ABI header `m12core.h` with phase-1 version/build exports only.
- Added inert `m12core.cpp` implementation with explicit comments describing the no-rendering-ownership Phase 1 boundary.
- Added env-gated loader in `winemetal_unix.c`:
  - `DXMT_M12CORE_ENABLE=1` enables probing/loading.
  - `DXMT_M12CORE_PATH=/path/to/libm12core.dylib` overrides the default `@loader_path/libm12core.dylib`.
  - `DXMT_M12CORE_REQUIRED=1` aborts on loader/version failure for strict tests.
  - default behavior is disabled/fallback, preserving existing M12 runtime behavior.
- Staging now copies `libm12core.dylib` beside `winemetal.so` in both the isolated `dxmt_m12` lane and shared Wine unix sidecar lane.
- Rust/backend launch override allowlist now maps:
  - `METALSHARP_M12CORE_ENABLE` -> `DXMT_M12CORE_ENABLE`
  - `METALSHARP_M12CORE_REQUIRED` -> `DXMT_M12CORE_REQUIRED`
  - `METALSHARP_M12CORE_PATH` -> `DXMT_M12CORE_PATH`
- Bounded launch script can forward the same `METALSHARP_M12CORE_*` envs.

## Validation

- `tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight`
- `python3 -m py_compile tools/d3d12-metal-sdk/scripts/analyze-m12-perf-run.py`
- `cd app/src-rust && cargo fmt --all --check`
- `cd app/src-rust && cargo test tests::m12core_launch_overrides_map_to_dxmt_loader_env`
- `cd app/src-rust && cargo test mtsp::launcher::tests:: -- --nocapture`
- `cd app/src-rust && cargo clippy --all-targets -- -D warnings`
- direct x86_64 dlopen probe for staged `winemetal.so` with `DXMT_M12CORE_ENABLE=1`:
  - log: `tools/d3d12-metal-sdk/results/m12-libm12core-phase1-loader-20260617-000501/direct-loader-probe.txt`
  - observed: `[m12core] loaded path=@loader_path/libm12core.dylib abi=1 features=0x1 build_id=00000001:4d313243 build=libm12core phase1 inert-loader abi=1`
- AC6 bounded 60s smoke with strict staged hashes:
  - run: `tools/d3d12-metal-sdk/results/perf-runs/armored-core-vi-smoke-20260617-000501/`
  - drawn/present: `20/20`
  - render/compute/DXIL/unsafe failures: `0`

## Staged runtime hashes

- stage manifest: `tools/d3d12-metal-sdk/results/stage-runtime-metalsharp.json`
- `d3d12.dll`: `f0f971a3cb2dc76803c3e9d537929bf99aa62cf97c32ce4b7ff3bcc03463665d`
- `dxgi.dll`: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- `dxgi_dxmt.dll`: `e92a9c02e50224cc36eef763cf5cf99220022bad289511aff5e5e1cacd2e5d67`
- `winemetal.dll`: `7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85`
- `winemetal.so`: `5252e100552851437deb4fbe34085d72b63b0b2080dacc1824288f7ba6c24a44`
- `libm12core.dylib`: `98a776f4ec5956b1193157c9b55dd8648d60b93c87ba4ef5469dc6985efc1e5a`

## Scope note

This phase intentionally does not move shader, PSO, binding, command replay, or presenter ownership into `libm12core`. It only proves the loader, ABI/version check, staging, fallback shape, and future override plumbing.
