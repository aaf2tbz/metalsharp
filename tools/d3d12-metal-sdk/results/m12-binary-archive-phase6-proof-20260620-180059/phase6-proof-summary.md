# M12 binary archive Phase 6 offline proof

Result: PASS

## Scope

- Offline only: no Steam, Wine, wineserver, AC6 launch, or runtime staging.
- Native Metal fixture matrix for archive validation/circuit-breaker behavior.
- Production source-shape checks for runtime lookup gating.

## Acceptance

- good_archive_validation_enables_lookup: `True`
- bypass_env_keeps_lookup_disabled: `True`
- missing_corrupt_empty_disable_lookup: `True`
- serialization_failure_disables_lookup: `True`
- archive_population_disabled_by_default_even_when_lookup_disabled: `True`
- archive_population_requires_explicit_env: `True`
- validation_failure_paths_emit_no_probe_logs: `True`
- validation_temp_artifacts_are_cleaned: `True`
- source_invariants_pass: `True`
- hard_timeout_process_group_kill_active: `True`
- commands_passed: `True`
- no_wine_steam_ac6_runtime_staging_logging_or_tracing: `True`
- dxmt_m12_runtime_snapshot_unchanged: `True`

## Cases

- good: allow_lookup=`True` validation=`True` loaded_existing=`True` memory_fallback=`False` population=`False`
- good-populate: allow_lookup=`True` validation=`True` loaded_existing=`True` memory_fallback=`False` population=`True`
- bypass: allow_lookup=`False` validation=`True` loaded_existing=`True` memory_fallback=`False` population=`False`
- missing: allow_lookup=`False` validation=`True` loaded_existing=`False` memory_fallback=`True` population=`False`
- corrupt: allow_lookup=`False` validation=`True` loaded_existing=`False` memory_fallback=`True` population=`False`
- empty: allow_lookup=`False` validation=`True` loaded_existing=`False` memory_fallback=`True` population=`False`
- validation-failure: allow_lookup=`False` validation=`False` loaded_existing=`True` memory_fallback=`False` population=`False`

## Commands

- rc=0 timeout=False cmd=`clang++ -std=c++17 -ObjC++ -O2 /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/probes/probe_m12_binary_archive_validation_phase6/probe_m12_binary_archive_validation_phase6.mm -framework Foundation -framework Metal -o /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_validation_phase6`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/compile.stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/compile.stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_validation_phase6 --case good --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/good/result.json --validation-path /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/good/m12-phase6-validation-test.bin`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/good/probe.stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/good/probe.stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_validation_phase6 --case good-populate --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/good-populate/result.json --validation-path /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/good-populate/m12-phase6-validation-test.bin`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/good-populate/probe.stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/good-populate/probe.stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_validation_phase6 --case bypass --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/bypass/result.json --validation-path /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/bypass/m12-phase6-validation-test.bin`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/bypass/probe.stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/bypass/probe.stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_validation_phase6 --case missing --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/missing/result.json --validation-path /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/missing/m12-phase6-validation-test.bin`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/missing/probe.stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/missing/probe.stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_validation_phase6 --case corrupt --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/corrupt/result.json --validation-path /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/corrupt/m12-phase6-validation-test.bin`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/corrupt/probe.stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/corrupt/probe.stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_validation_phase6 --case empty --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/empty/result.json --validation-path /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/empty/m12-phase6-validation-test.bin`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/empty/probe.stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/empty/probe.stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_validation_phase6 --case validation-failure --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/validation-failure/result.json --validation-path /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/validation-failure/m12-phase6-validation-test.bin`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/validation-failure/probe.stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase6-proof-20260620-180059/validation-failure/probe.stderr.txt` size=0
