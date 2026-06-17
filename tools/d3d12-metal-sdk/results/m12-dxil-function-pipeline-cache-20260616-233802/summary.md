# M12 DXIL function and Metal pipeline cache pass

## Change

- Allows DXIL-backed shaders to reuse the in-process `s_shader_cache` even when the D3D12 PSO path asks for SM50 reflection outputs. DXIL lowering does not provide SM50 reflection handles, so the previous reflection-gated cache lookup forced repeated DXIL-to-MSL/source library work for repeated shaders in the same process.
- Keeps vertex input layout analysis before the cache lookup so vertex-pulling metadata remains available for the current PSO.
- Adds conservative in-process Metal render/compute pipeline object caches keyed by shader hashes plus render pipeline state fields.
- Extends perf analysis parsing for Metal pipeline cache hit/miss counters.

## Validation

- `python3 -m py_compile tools/d3d12-metal-sdk/scripts/analyze-m12-perf-run.py`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight`
- bounded AC6 smoke with strict staged hashes: `tools/d3d12-metal-sdk/results/perf-runs/armored-core-vi-smoke-20260616-233802/`
- extended 150-second AC6 smoke with strict staged hashes: `tools/d3d12-metal-sdk/results/perf-runs/armored-core-vi-smoke-20260616-234123/`

## AC6 bounded result

- 60s drawn/present: `20/20`; failures: render/compute/DXIL/unsafe draw all `0`
- 60s graphics PSO requests unique/repeated/total: `2271/739/3010`
- 60s Metal pipeline creates render/compute: `450/0`
- 60s Metal pipeline cache hits/misses render: `6/450`
- 60s shader cache hits/misses memory: `427/450`
- 60s shader metallib hits/misses: `0/435`
- 150s drawn/present: `22/22`; failures: render/compute/DXIL/unsafe draw all `0`
- 150s graphics PSO requests unique/repeated/total: `2282/745/3027`
- 150s Metal pipeline creates render/compute: `1519/0`
- 150s Metal pipeline cache hits/misses render: `30/1519`

## Staged runtime hashes

- stage manifest: `tools/d3d12-metal-sdk/results/stage-runtime-metalsharp.json`
- `d3d12.dll`: `0714fb0b3ab40dcc64a5270a451b729b1f80cd39793dece06f196b786c4798ec`
- `dxgi.dll`: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- `dxgi_dxmt.dll`: `869b8fe1a7860c3c615735f36d48b7a6591df030042b0824b09136bd41cbc537`
- `winemetal.dll`: `7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85`
- `winemetal.so`: `cad141357fecf172c9c3966daeb8c5d0972d4e06d383e30cf495593dbc9dab7d`

## Takeaway

The hot path now proves repeated DXIL shader work can be served from the in-process function cache, but AC6 remains dominated by many unique PSO descriptors rather than identical Metal pipeline reuse. Next step remains offline/prewarm/oracle-guided PSO preparation rather than increasing worker fanout.
