# libm12core Phase 3 shader cache lookup bridge

## Implemented

- Added `M12CoreShaderCacheLookup` and `m12core_probe_shader_cache(...)` to native `libm12core`.
- Added `WMTM12CoreProbeShaderCache(...)` PE thunk and unixcall `138`.
- Moved the metallib cache usability decision into `libm12core`:
  - path formatting
  - non-empty metallib existence check
  - force-source-compile override handling
- Preserved PE-local fallback for disabled/unavailable `libm12core`.
- Kept actual file IO, DXIL->MSL compilation, Metal library/function creation, and reflection compatibility on the existing path.
- Added comments marking this as a cache lookup ownership seam rather than full compiler migration.

## Validation

- `tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight`
- Direct x86_64 lookup probe:
  - `direct-lookup-probe.txt`
  - observed cached metallib exists/available, and force-source disables availability.
- AC6 60s bounded run with m12core enabled and strict hashes:
  - `tools/d3d12-metal-sdk/results/bounded-launches/armored-core-vi-20260617-012804/summary.md`
  - drawn/present: `20/20`
  - failures: `0`
  - new shader artifacts: `0`
- Self-review: `self-review.md`

## Runtime hashes

- `d3d12.dll`: `9c4ceca0e764cd7890dbe6c1098a0b2d862d2eba61d5f045d6345f7118e37335`
- `dxgi.dll`: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- `dxgi_dxmt.dll`: `e458eae71f4ef2bc5e990ce93e084d3c510ae18a7f5d71719cfcdfb266855adf`
- `winemetal.dll`: `c71f68c14b6b5a6d6922a6ad99a5afb4883631d3ac900f59f4cf714631e27428`
- `winemetal.so`: `383fc0a290ab84ee45cb34d923b23a4a3de003f7e4d19672cfe75e2fb7c049d4`
- `libm12core.dylib`: staged by `stage-runtime-metalsharp.json`

## Next safe slice

Move reflection-summary/path metadata into native result structs or introduce an opaque shader-cache record handle. Full DXIL->MSL compiler ownership should remain a later slice because it touches airconv and Metal library lifetimes.
