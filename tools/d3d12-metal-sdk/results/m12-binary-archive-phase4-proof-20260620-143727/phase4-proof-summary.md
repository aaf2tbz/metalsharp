# M12 binary archive Phase 4 offline proof

Result: PASS

## Scope

- Offline only: no Steam, Wine, wineserver, AC6 launch, or runtime staging.
- Native ObjC safety probe with hard process-group timeout.
- Production source-shape checks for archive-add mutex/exception guards.

## Acceptance

- render_and_compute_archive_add_isolated_under_mutex: `True`
- normal_add_success_allows_baseline_pso_creation: `True`
- forced_failure_exception_paths_unlock_mutex: `True`
- failure_clears_descriptor_archive_lookup: `True`
- standard_pso_creation_continues_after_archive_add_failure: `True`
- stdout_stderr_empty_for_archive_failure_paths: `True`
- source_invariants_pass: `True`
- hard_timeout_process_group_kill_active: `True`
- commands_passed: `True`
- no_wine_steam_ac6_runtime_staging_logging_or_tracing: `True`
- dxmt_m12_runtime_snapshot_unchanged: `True`

## Probe cases

- compute-success: forced_exception=`False` add_attempted=`True` mutex_unlocked=`True` cleared=`True` continues=`True` passed=`True`
- render-success: forced_exception=`False` add_attempted=`True` mutex_unlocked=`True` cleared=`True` continues=`True` passed=`True`
- compute-exception: forced_exception=`True` add_attempted=`True` mutex_unlocked=`True` cleared=`True` continues=`True` passed=`True`
- render-exception: forced_exception=`True` add_attempted=`True` mutex_unlocked=`True` cleared=`True` continues=`True` passed=`True`

## Commands

- rc=0 timeout=False cmd=`clang -ObjC -O2 /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/probes/probe_m12_binary_archive_objc_phase4/probe_m12_binary_archive_objc_phase4.m -framework Foundation -lpthread -o /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_objc_phase4`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase4-proof-20260620-143727/compile.stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase4-proof-20260620-143727/compile.stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_objc_phase4 --case compute-success --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase4-proof-20260620-143727/compute-success/result.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase4-proof-20260620-143727/compute-success/stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase4-proof-20260620-143727/compute-success/stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_objc_phase4 --case render-success --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase4-proof-20260620-143727/render-success/result.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase4-proof-20260620-143727/render-success/stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase4-proof-20260620-143727/render-success/stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_objc_phase4 --case compute-exception --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase4-proof-20260620-143727/compute-exception/result.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase4-proof-20260620-143727/compute-exception/stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase4-proof-20260620-143727/compute-exception/stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_objc_phase4 --case render-exception --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase4-proof-20260620-143727/render-exception/result.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase4-proof-20260620-143727/render-exception/stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase4-proof-20260620-143727/render-exception/stderr.txt` size=0
