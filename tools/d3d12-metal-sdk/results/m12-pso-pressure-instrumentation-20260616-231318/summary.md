# M12 PSO pressure instrumentation

Implemented Phase B counters/log parsing for M12 PSO churn diagnosis.

## Runtime instrumentation

- D3D12 graphics PSO create request totals.
- D3D12 compute PSO create request totals.
- Unique/repeated descriptor pressure hashes for graphics and compute PSOs.
- Metal render pipeline creation call count.
- Metal compute pipeline creation call count.
- Shader in-memory function cache hit/miss counters.
- DXIL metallib cache hit/miss counters.
- Compile wait/stall counts and total wait nanoseconds for threads waiting on an in-progress PSO compile.

Runtime emits compact `PSO_PRESSURE ...` lines into existing M12 D3D12 diagnostic logs.

## Perf summary integration

`tools/d3d12-metal-sdk/scripts/analyze-m12-perf-run.py` now scans run logs and includes a `pso_pressure` block plus pressure markdown lines for:

- graphics unique/repeated/total requests
- compute unique/repeated/total requests
- Metal render/compute pipeline creates
- shader cache hit/miss counters
- compile waits and total wait milliseconds

## Validation

- `tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight`
- `python3 -m py_compile tools/d3d12-metal-sdk/scripts/analyze-m12-perf-run.py`
- `python3 tools/d3d12-metal-sdk/scripts/analyze-m12-perf-run.py tools/d3d12-metal-sdk/results/perf-runs/ac6-clipw-fix-clean-cache-20260616-222007/armored-core-vi-smoke-20260616-222007`

## AC6 bounded validation

- run root: `tools/d3d12-metal-sdk/results/perf-runs/armored-core-vi-smoke-20260616-232156/`
- strict runtime hash gates passed.
- drawn/present: `20/20`
- failures: render/compute/compile/unsafe draw all `0`
- graphics PSO requests unique/repeated/total: `2270/739/3009`
- compute PSO requests unique/repeated/total: `24/0/24`
- Metal pipeline creates render/compute: `263/0`
- shader metallib hits/misses: `0/495`
- compile waits: `0`

This confirms the new counters are visible in launch logs and folded into perf analysis while AC6 remains rendered/no-failure under the bounded 60-second smoke.

## Staged runtime hashes

- stage manifest: `tools/d3d12-metal-sdk/results/stage-runtime-metalsharp.json`
- `d3d12.dll`: `d155d62c7777de0763ab31cc50a46ad90372fa8dc7da66e1c07881962b7096f1`
- `dxgi.dll`: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- `dxgi_dxmt.dll`: `fb1a3ff802c79ad6821a4cda6c62f6c9121299820c2a795a56f7c9e2531a713d`
- `winemetal.dll`: `7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85`
- `winemetal.so`: `cad141357fecf172c9c3966daeb8c5d0972d4e06d383e30cf495593dbc9dab7d`
