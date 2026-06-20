# M12 metallib Phase 6B offline proof

Result: PASS

## Scope

- Offline only: no Steam, Wine, AC6 launch, runtime staging, logging, or tracing.
- Proof-local shader cache only.
- Valid metallib load/use plus invalid/stale/error/force-source lookup rejection.

## Acceptance

- valid_fresh_metallib_available: `True`
- missing_zero_bad_header_stale_active_error_and_force_source_unavailable: `True`
- zero_and_bad_header_still_report_existing_only_when_nonzero_regular: `True`
- valid_metallib_loads_with_metal_newLibraryWithData: `True`
- valid_metallib_loads_with_m12core_and_second_call_cache_hit: `True`
- source_invariants_pass: `True`
- runtime_writeback_materialization_absent: `True`
- hard_timeout_process_group_kill_active: `True`
- commands_passed: `True`
- dxmt_m12_runtime_snapshot_unchanged: `True`
- no_wine_steam_ac6_runtime_staging_logging_or_tracing: `True`
- live_shader_cache_unchanged_by_policy: `not touched; proof cache is results-local`

## Lookup cases

- valid: exists=`True` available=`True` force=`False`
- missing: exists=`False` available=`False` force=`False`
- zero: exists=`False` available=`False` force=`False`
- bad_header: exists=`True` available=`False` force=`False`
- stale: exists=`True` available=`False` force=`False`
- active_error: exists=`True` available=`False` force=`False`
- force_source: exists=`True` available=`False` force=`True`

## Commands

- rc=0 timeout=False cmd=`xcrun -sdk macosx metal -x metal -c /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/proof-cache/1111111111111111.msl -o /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/proof-cache/1111111111111111.air`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/metal.stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/metal.stderr.txt` size=0
- rc=0 timeout=False cmd=`xcrun -sdk macosx metallib /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/proof-cache/1111111111111111.air -o /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/proof-cache/1111111111111111.metallib`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/metallib.stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/metallib.stderr.txt` size=0
- rc=0 timeout=False cmd=`clang -arch x86_64 -mmacosx-version-min=12.0 -I /Users/alexmondello/metalsharp-m12-lab/vendor/dxmt/src/m12core /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/probes/probe_m12_metallib_lookup_phase6b/probe_m12_metallib_lookup_phase6b.c /Users/alexmondello/metalsharp-m12-lab/vendor/dxmt/build-metalsharp-x64/src/m12core/libm12core.dylib -Wl,-rpath,/Users/alexmondello/metalsharp-m12-lab/vendor/dxmt/build-metalsharp-x64/src/m12core -Wl,-rpath,/Users/alexmondello/.metalsharp/toolchains/clang+llvm-15.0.7-x86_64-apple-darwin21.0/lib -o /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_metallib_lookup_phase6b`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-compile.stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-compile.stderr.txt` size=126
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_metallib_lookup_phase6b --cache-dir /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/proof-cache --hash 1111111111111111 --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-valid/result.json --force-source 0`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-valid/stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-valid/stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_metallib_lookup_phase6b --cache-dir /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/proof-cache --hash 2222222222222222 --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-missing/result.json --force-source 0`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-missing/stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-missing/stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_metallib_lookup_phase6b --cache-dir /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/proof-cache --hash 3333333333333333 --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-zero/result.json --force-source 0`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-zero/stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-zero/stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_metallib_lookup_phase6b --cache-dir /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/proof-cache --hash 4444444444444444 --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-bad_header/result.json --force-source 0`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-bad_header/stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-bad_header/stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_metallib_lookup_phase6b --cache-dir /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/proof-cache --hash 5555555555555555 --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-stale/result.json --force-source 0`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-stale/stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-stale/stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_metallib_lookup_phase6b --cache-dir /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/proof-cache --hash 6666666666666666 --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-active_error/result.json --force-source 0`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-active_error/stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-active_error/stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_metallib_lookup_phase6b --cache-dir /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/proof-cache --hash 7777777777777777 --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-force_source/result.json --force-source 1`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-force_source/stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/lookup-force_source/stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/scripts/probe-metal-metallib-load.sh --cache-dir /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/proof-cache --hash 1111111111111111`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/metal-probe.stdout.txt` size=80
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/metal-probe.stderr.txt` size=0
- rc=0 timeout=False cmd=`clang -arch x86_64 -mmacosx-version-min=12.0 -I /Users/alexmondello/metalsharp-m12-lab/vendor/dxmt/src/m12core /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/probes/probe_m12_metallib_load/probe_m12_metallib_load.m /Users/alexmondello/metalsharp-m12-lab/vendor/dxmt/build-metalsharp-x64/src/m12core/libm12core.dylib -framework Foundation -framework Metal -Wl,-rpath,/Users/alexmondello/metalsharp-m12-lab/vendor/dxmt/build-metalsharp-x64/src/m12core -Wl,-rpath,/Users/alexmondello/.metalsharp/toolchains/clang+llvm-15.0.7-x86_64-apple-darwin21.0/lib -o /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_metallib_load_phase6b`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/m12core-compile.stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/m12core-compile.stderr.txt` size=126
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_metallib_load_phase6b --cache-dir /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/proof-cache --hash 1111111111111111`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/m12core-probe.stdout.txt` size=111
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/m12core-probe.stderr.txt` size=0
- rc=-15 timeout=True cmd=`python3 -c import time; time.sleep(30)`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/timeout-selftest.stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase6b-proof-20260620-170729/timeout-selftest.stderr.txt` size=0
