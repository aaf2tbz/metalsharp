# M12 binary archive Phase 2 offline proof

Result: PASS

## Scope

- Offline only: no Steam, Wine, wineserver, AC6 launch, or runtime staging.
- Native Metal probe with hard process-group timeout.
- Production source-shape checks for PE-side manager invariants.

## Acceptance

- env_parsing_enables_and_disables: `True`
- bypass_disables_lookup_without_disabling_population: `True`
- archive_path_formatted_once_process_lifetime: `True`
- pipeline_cache_path_preferred: `True`
- shader_cache_path_fallback: `True`
- existing_archive_load_succeeds: `True`
- missing_archive_falls_back_to_empty_memory_archive: `True`
- corrupt_archive_falls_back_to_empty_memory_archive: `True`
- source_invariants_pass: `True`
- hard_timeout_process_group_kill_active: `True`
- commands_passed: `True`
- no_wine_steam_ac6_runtime_staging_logging_or_tracing: `True`
- dxmt_m12_runtime_snapshot_unchanged: `True`

## Probe cases

- disabled: enabled=`False` allow_lookup=`False` path_load=`False` memory_fallback=`False` archive=`False`
- enabled: enabled=`True` allow_lookup=`True` path_load=`False` memory_fallback=`True` archive=`True`
- bypass: enabled=`True` allow_lookup=`False` path_load=`False` memory_fallback=`True` archive=`True`
- shader-cache-fallback: enabled=`True` allow_lookup=`True` path_load=`False` memory_fallback=`True` archive=`True`
- missing: enabled=`True` allow_lookup=`True` path_load=`False` memory_fallback=`True` archive=`True`
- corrupt: enabled=`True` allow_lookup=`True` path_load=`False` memory_fallback=`True` archive=`True`
- existing: enabled=`True` allow_lookup=`True` path_load=`True` memory_fallback=`False` archive=`True`

## Commands

- rc=0 timeout=False cmd=`clang++ -std=c++17 -O2 /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/probes/probe_m12_binary_archive_manager_phase2/probe_m12_binary_archive_manager_phase2.mm -framework Foundation -framework Metal -o /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_manager_phase2`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/compile.stdout.txt`
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/compile.stderr.txt`
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_manager_phase2 --case disabled --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/disabled/result.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/disabled/stdout.txt`
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/disabled/stderr.txt`
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_manager_phase2 --case enabled --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/enabled/result.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/enabled/stdout.txt`
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/enabled/stderr.txt`
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_manager_phase2 --case bypass --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/bypass/result.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/bypass/stdout.txt`
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/bypass/stderr.txt`
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_manager_phase2 --case shader-cache-fallback --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/shader-cache-fallback/result.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/shader-cache-fallback/stdout.txt`
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/shader-cache-fallback/stderr.txt`
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_manager_phase2 --case missing --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/missing/result.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/missing/stdout.txt`
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/missing/stderr.txt`
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_manager_phase2 --case corrupt --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/corrupt/result.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/corrupt/stdout.txt`
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/corrupt/stderr.txt`
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_manager_phase2 --case existing --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/existing/result.json --existing-archive /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-offline-corpus-20260620-131215/m12_binary_archive_corpus.binarchive`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/existing/stdout.txt`
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase2-proof-20260620-134726/existing/stderr.txt`
