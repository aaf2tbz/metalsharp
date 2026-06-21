# M12 binary archive Phase 3 offline proof

Result: PASS

## Scope

- Offline only: no Steam, Wine, wineserver, AC6 launch, or runtime staging.
- Descriptor attachment proof only; Objective-C archive-add safety remains Phase 4.
- Native C++ probe with hard process-group timeout.
- Production source-shape checks for heap-owned lookup payload and descriptor wiring.

## Acceptance

- compute_and_render_serialization_only_when_population_allowed: `True`
- lookup_attached_only_when_allowed: `True`
- lookup_pointer_uses_heap_payload_storage: `True`
- async_worker_shaped_heap_payload_lifetime: `True`
- runtime_fail_on_binary_archive_miss_false: `True`
- source_invariants_pass: `True`
- hard_timeout_process_group_kill_active: `True`
- commands_passed: `True`
- no_wine_steam_ac6_runtime_staging_logging_or_tracing: `True`
- dxmt_m12_runtime_snapshot_unchanged: `True`

## Probe cases

- disabled: enabled=`False` allow_lookup=`False` compute_serialization=`False` render_serialization=`False` compute_lookup_count=`0` render_lookup_count=`0` passed=`True`
- lookup-bypassed: enabled=`True` allow_lookup=`False` compute_serialization=`False` render_serialization=`False` compute_lookup_count=`0` render_lookup_count=`0` passed=`True`
- lookup-allowed: enabled=`True` allow_lookup=`True` compute_serialization=`False` render_serialization=`False` compute_lookup_count=`1` render_lookup_count=`1` passed=`True`
- population-enabled: enabled=`True` allow_lookup=`False` compute_serialization=`True` render_serialization=`True` compute_lookup_count=`0` render_lookup_count=`0` passed=`True`
- circuit-breaker: enabled=`True` allow_lookup=`False` compute_serialization=`False` render_serialization=`False` compute_lookup_count=`0` render_lookup_count=`0` passed=`True`

## Commands

- rc=0 timeout=False cmd=`clang++ -std=c++17 -O2 /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/probes/probe_m12_binary_archive_descriptor_phase3/probe_m12_binary_archive_descriptor_phase3.cpp -o /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_descriptor_phase3`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-180053/compile.stdout.txt`
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-180053/compile.stderr.txt`
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_descriptor_phase3 --case disabled --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-180053/disabled/result.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-180053/disabled/stdout.txt`
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-180053/disabled/stderr.txt`
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_descriptor_phase3 --case lookup-bypassed --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-180053/lookup-bypassed/result.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-180053/lookup-bypassed/stdout.txt`
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-180053/lookup-bypassed/stderr.txt`
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_descriptor_phase3 --case lookup-allowed --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-180053/lookup-allowed/result.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-180053/lookup-allowed/stdout.txt`
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-180053/lookup-allowed/stderr.txt`
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_descriptor_phase3 --case population-enabled --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-180053/population-enabled/result.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-180053/population-enabled/stdout.txt`
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-180053/population-enabled/stderr.txt`
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_descriptor_phase3 --case circuit-breaker --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-180053/circuit-breaker/result.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-180053/circuit-breaker/stdout.txt`
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase3-proof-20260620-180053/circuit-breaker/stderr.txt`
