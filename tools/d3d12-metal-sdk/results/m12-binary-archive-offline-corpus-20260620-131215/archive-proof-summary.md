# M12 binary archive offline corpus proof

Result: PASS

## Scope

- Offline only: no Steam, Wine, wineserver, or AC6 launch.
- Native Metal probe with hard process timeout.
- Existing AC6 shader-cache/PSO manifests only.

## Corpus counts

- complete compute PSO descriptor: 24
- complete render PSO descriptor: 2217
- incomplete descriptor metadata: 5
- missing shader artifact: 31

## Probe counts

- archive_add_ok: 24
- baseline_create_ok: 24
- initial_archive_add_and_baseline_create_ok: 24
- selected_total: 24
- strict_lookup_create_ok: 24

## Archive

- path: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-offline-corpus-20260620-131215/m12_binary_archive_corpus.binarchive`
- bytes: `1997408`
- sha256: `76f6610524ed0af2128aa07494658957dae9a2a99919e629c5cda9ceb163f903`
- serialize_ok: `True`
- reload_ok: `True`

## Commands

- rc=0 timeout=False cmd=`clang++ -std=c++17 -fobjc-arc -O2 /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/probes/probe_m12_binary_archive_corpus/probe_m12_binary_archive_corpus.mm -framework Foundation -framework Metal -o /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_corpus`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-offline-corpus-20260620-131215/compile.stdout.txt`
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-offline-corpus-20260620-131215/compile.stderr.txt`
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/out/bin/probe_m12_binary_archive_corpus --input /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-offline-corpus-20260620-131215/probe-input.json --archive /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-offline-corpus-20260620-131215/m12_binary_archive_corpus.binarchive --output /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-offline-corpus-20260620-131215/archive-proof-summary.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-offline-corpus-20260620-131215/probe.stdout.txt`
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-binary-archive-offline-corpus-20260620-131215/probe.stderr.txt`

## Acceptance

- offline_only_no_wine_or_ac6_launch: `True`
- no_live_runtime_staging_occurred: `True`
- compile_passed: `True`
- probe_returncode_zero: `True`
- probe_not_timed_out: `True`
- probe_did_not_survive_sigkill: `True`
- archive_nonzero: `True`
- archive_reload_ok: `True`
- all_selected_render_strict_lookup_passed: `True`
- all_selected_compute_strict_lookup_passed_if_complete_compute_exists: `True`
- failures_classified_not_hidden: `True`
