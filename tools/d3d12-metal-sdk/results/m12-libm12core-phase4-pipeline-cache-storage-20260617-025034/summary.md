# libm12core Phase 4 native pipeline cache storage

## Implemented

- Added `M12CORE_FEATURE_PIPELINE_CACHE`.
- Added C ABI:
  - `M12CorePipelineCacheQuery`
  - `M12CorePipelineCacheResult`
  - `m12core_lookup_pipeline_cache(...)`
  - `m12core_store_pipeline_cache(...)`
- Implemented retained Objective-C pipeline cache storage in `m12core_metal.c`.
- Added PE/native bridge:
  - `WMTM12CoreLookupPipelineCache(...)`
  - `WMTM12CoreStorePipelineCache(...)`
  - unixcalls `144` and `145`
- D3D12 render/compute PSO cache lookup now tries `libm12core` first and falls back to the old local maps when unavailable.
- D3D12 still owns normalized PSO field accumulation and actual Metal pipeline creation; this slice moves cache storage/lifetime only.

## Validation

- `tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight`
- Direct x86_64 retained pipeline-cache probe:
  - `direct-pipeline-cache-probe.txt`
- AC6 120s bounded run with m12core required and strict hashes:
  - `tools/d3d12-metal-sdk/results/bounded-launches/armored-core-vi-20260617-025034/summary.md`
  - drawn/present: `22/22`
  - failures: `0`
  - new shader artifacts: `0`
  - native cache evidence: `native-pipeline-cache-lines.txt`
- Self-review: `self-review.md`

## Runtime hashes

- `d3d12.dll`: `df09fb3bffe7b4ce7500c04f8b6ab7ce92aae76f5f368a449a174ce81b58af1a`
- `dxgi.dll`: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- `dxgi_dxmt.dll`: `aa3286e99f7f81972119f9321493f86ad457476c09443589357c1b9402fa8d77`
- `winemetal.dll`: `b4d126fb035fa0494722a6a0ae8e2c648b941f39510aeb1be8c090aaed17307e`
- `winemetal.so`: `035bc8a51cce42163dbc1a2b9bbb01546f53d6dbc12e285d1992b01cbdc27bf4`
- `libm12core.dylib`: `faa92fb59b6787e41c168e9b586152e63dc6fe0a6076726e1325ad5d0287b07b`

## Next Phase 4 slice

Move normalized render/compute PSO field accumulation into `libm12core`, then move actual Metal render/compute pipeline creation into the native pipeline cache.
