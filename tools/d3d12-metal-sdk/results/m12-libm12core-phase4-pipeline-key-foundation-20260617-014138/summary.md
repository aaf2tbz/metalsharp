# libm12core Phase 4 pipeline key foundation

## Implemented

- Added `M12CorePipelineCacheKeyInput`, `M12CorePipelineCacheKey`, and `m12core_make_pipeline_cache_key(...)` to native `libm12core`.
- Added `WMTM12CoreMakePipelineCacheKey(...)` PE thunk and unixcall `140`.
- D3D12 render/compute pipeline cache lookup now asks `libm12core` to finalize the device-scoped cache key when enabled.
- Preserved PE fallback for disabled/unavailable `libm12core`.
- Added comments documenting this as a Phase 4 foundation seam only: descriptor normalization and Metal pipeline object ownership remain on the old path.

## Validation

- `tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight`
- Direct x86_64 pipeline-key probe:
  - `direct-pipeline-key-probe.txt`
  - observed distinct keys for distinct device IDs.
- AC6 60s bounded run with m12core enabled and strict hashes:
  - `tools/d3d12-metal-sdk/results/bounded-launches/armored-core-vi-20260617-014138/summary.md`
  - drawn/present: `20/20`
  - failures: `0`
  - new shader artifacts: `0`
- Self-review: `self-review.md`

## Runtime hashes

- `d3d12.dll`: `7f165e91a77e5c84c26cbce62b71b9b804e90fee18430be168a67c7efded083b`
- `dxgi.dll`: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- `dxgi_dxmt.dll`: `4e53b2908a4e769edabd1725112faabe73bc16a945adad48e6420d2d5e108a94`
- `winemetal.dll`: `387b42ea180cd2aafae893609ae16c707e8bf870df18fa391f22dd0bbb97d0e0`
- `winemetal.so`: `a63c84f64b5aebb0029ee27b947c8222ab9fd5d52f6ef89f954fe1e5ecf6b532`
- `libm12core.dylib`: staged by `stage-runtime-metalsharp.json`

## Next safe slice

Move normalized render/compute PSO key field accumulation into `libm12core` one field group at a time, or add native PSO key diagnostics before moving Metal pipeline object caches.
