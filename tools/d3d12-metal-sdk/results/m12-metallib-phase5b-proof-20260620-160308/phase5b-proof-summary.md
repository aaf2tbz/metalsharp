# M12 generated-MSL metallib Phase 5B offline proof

Result: PASS

## Scope

- Offline only: no Steam, Wine, wineserver, AC6 launch, or runtime staging.
- Uses a proof cache copied from AC6 generated MSL sidecars; live shader cache is not mutated.
- Proves .metallib write/persist plus actual load/use through Metal and M12Core.
- Does not rewrite or perturb the proven DXIL/HLSL -> generated-MSL path.

## Selected hashes

- `035eda86ce184ec9` size=338483 sha256=`920757bc147e9bba669cefa86688d839cad99f3273092fb2616b52cf5e09e33c`
- `28e798d611ca9d9b` size=319667 sha256=`be8627683503dd48785c6f45157a37c0df3f585b7be78dc389d8cbd2c19bbfca`
- `30b7ac004179379b` size=332969 sha256=`d1e38b35a81d404655394d2735e68e04120b0898286a5c103c304ec8887f73d9`
- `358d2013e6f2e72d` size=344017 sha256=`a6b9802e1df8dcc6224772ec3d829bda5186f49105b932e4280d4452ee266334`
- `4b1b88ab893f62d4` size=350899 sha256=`3074b0cba5145649c3af08c465769cbb1e5c094662fe4fabf06388c04c8a3f9f`
- `5e927c8c9dfba1ac` size=326659 sha256=`7e368c1531d16ea2490d7fdd0b690797e4939016eecc082e053f7edc4c2378cf`
- `80a670aae31c7878` size=339253 sha256=`6030ae8c9e8a460facfb0a2ea50b68a7c42946e5300cbe09c1e83cf9c12c6c7d`
- `9b9158ee986ea931` size=320352 sha256=`8c77466c31df391cb158431c647f76069a011d1ae11022e97ff24e55ce008b63`

## Acceptance

- selected_real_ac6_generated_msl_sidecars: `True`
- proof_cache_only_live_shader_cache_not_mutated: `True`
- metallib_materialization_succeeded: `True`
- metallibs_are_fresh_nonzero_with_mtlb_header: `True`
- metallibs_load_with_metal_newLibraryWithData: `True`
- metallibs_load_with_m12core_shader_function_path: `True`
- m12core_second_load_hits_function_cache: `True`
- dxmt_runtime_contract_prefers_metallib_before_msl_dxil_fallback: `True`
- commands_passed: `True`
- hard_timeout_process_group_kill_active: `True`
- no_wine_steam_ac6_runtime_staging_commands_requested: `True`
- dxmt_m12_runtime_snapshot_unchanged: `True`

## Load proof

- Direct Metal probe: `{'output': '/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/metal-probe/probe-output.txt', 'summary_found': True, 'total': 8, 'failures': 0, 'ok_lines': 8, 'second_cache_hits': 0, 'text_tail': 'ok hash=035eda86ce184ec9 bytes=42397 function=ps_main\nok hash=28e798d611ca9d9b bytes=34525 function=ps_main\nok hash=30b7ac004179379b bytes=28397 function=ps_main\nok hash=358d2013e6f2e72d bytes=54077 function=ps_main\nok hash=4b1b88ab893f62d4 bytes=11293 function=cs_main\nok hash=5e927c8c9dfba1ac bytes=53773 function=ps_main\nok hash=80a670aae31c7878 bytes=41309 function=ps_main\nok hash=9b9158ee986ea931 bytes=32877 function=ps_main\nsummary total=8 failures=0\n'}`
- M12Core probe: `{'output': '/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/m12core-probe/probe-output.txt', 'summary_found': True, 'total': 8, 'failures': 0, 'ok_lines': 8, 'second_cache_hits': 8, 'text_tail': 'ok hash=035eda86ce184ec9 bytes=42397 first_cache_hit=0 second_cache_hit=1 entry=main\nok hash=28e798d611ca9d9b bytes=34525 first_cache_hit=0 second_cache_hit=1 entry=main\nok hash=30b7ac004179379b bytes=28397 first_cache_hit=0 second_cache_hit=1 entry=main\nok hash=358d2013e6f2e72d bytes=54077 first_cache_hit=0 second_cache_hit=1 entry=main\nok hash=4b1b88ab893f62d4 bytes=11293 first_cache_hit=0 second_cache_hit=1 entry=main\nok hash=5e927c8c9dfba1ac bytes=53773 first_cache_hit=0 second_cache_hit=1 entry=main\nok hash=80a670aae31c7878 bytes=41309 first_cache_hit=0 second_cache_hit=1 entry=main\nok hash=9b9158ee986ea931 bytes=32877 first_cache_hit=0 second_cache_hit=1 entry=main\nsummary total=8 failures=0\n'}`

## Timeout self-test

- rc=-15 timeout=True still_running_after_sigkill=False
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/timeout-selftest.stdout.txt` size=0
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/timeout-selftest.stderr.txt` size=0

## Commands

- rc=0 timeout=False cmd=`/opt/homebrew/opt/python@3.14/bin/python3.14 /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/scripts/materialize-m12-msl-metallibs.py --cache-dir /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/proof-cache --hash-file /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/selected-hashes.txt --workers 2 --timeout 180 --strict --out /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/materialize.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/materialize.stdout.txt` size=424
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/materialize.stderr.txt` size=0
- rc=0 timeout=False cmd=`/opt/homebrew/opt/python@3.14/bin/python3.14 /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/scripts/verify-m12-metallib-freshness.py --cache-dir /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/proof-cache --hash-file /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/selected-hashes.txt --strict --out /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/freshness.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/freshness.stdout.txt` size=162
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/freshness.stderr.txt` size=0
- rc=0 timeout=False cmd=`/opt/homebrew/opt/python@3.14/bin/python3.14 /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/scripts/verify-m12-metallib-runtime-contract.py --source /Users/alexmondello/metalsharp-m12-lab/vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp --json /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/runtime-contract.json`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/runtime-contract.stdout.txt` size=1121
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/runtime-contract.stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/scripts/probe-metal-metallib-load.sh --cache-dir /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/proof-cache --hash-file /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/selected-hashes.txt`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/metal-probe.stdout.txt` size=459
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/metal-probe.stderr.txt` size=0
- rc=0 timeout=False cmd=`/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/scripts/probe-m12-metallib-load.sh --cache-dir /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/proof-cache --hash-file /Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/selected-hashes.txt`
  - stdout: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/m12core-probe.stdout.txt` size=707
  - stderr: `/Users/alexmondello/metalsharp-m12-lab/tools/d3d12-metal-sdk/results/m12-metallib-phase5b-proof-20260620-160308/m12core-probe.stderr.txt` size=126
