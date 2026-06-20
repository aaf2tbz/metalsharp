# M12 binary archive Phase 5 offline proof

Result: PASS

## Scope

- Offline only: no Steam, Wine, wineserver, AC6 launch, or runtime staging.
- Native Metal archive add/serialize/reload/strict-lookup proof.
- Production source-shape checks for batched threshold serialization.

## Acceptance

- relaxed_atomic_counter_triggers_only_at_interval: `True`
- no_per_pso_serialization_below_threshold: `True`
- concurrent_archive_add_and_serialize_ordered_by_mutex: `True`
- output_archive_nonzero_and_reloadable: `True`
- strict_lookup_passes_after_serialization: `True`
- no_global_device_runtime_lock_held_around_command_recording_simulation: `True`
- source_invariants_pass: `True`
- hard_timeout_process_group_kill_active: `True`
- commands_passed: `True`
- no_wine_steam_ac6_runtime_staging_logging_or_tracing: `True`
- dxmt_m12_runtime_snapshot_unchanged: `True`

## Probe

- add_compute_ok: `True`
- add_render_ok: `True`
- baseline_pso_creation_ok: `True`
- serializations_before_threshold: `0`
- before_threshold_size: `0`
- serializations_after_threshold: `1`
- archive_size: `32288`
- strict_lookup_ok: `True`
- max_active_archive_mutators: `1`
- passed: `True`

## Commands

- rc=0 timeout=False cmd=`clang++ -std=c++17 -ObjC++ -O2 /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/probes/probe_m12_binary_archive_serialization_phase5/probe_m12_binary_archive_serialization_phase5.mm -framework Foundation -framework Metal -lpthread -o /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_serialization_phase5`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase5-proof-20260620-153131/compile.stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase5-proof-20260620-153131/compile.stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_serialization_phase5 --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase5-proof-20260620-153131/probe-result.json --archive /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase5-proof-20260620-153131/m12-phase5-proof.binarchive`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase5-proof-20260620-153131/probe.stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-phase5-proof-20260620-153131/probe.stderr.txt` size=0
