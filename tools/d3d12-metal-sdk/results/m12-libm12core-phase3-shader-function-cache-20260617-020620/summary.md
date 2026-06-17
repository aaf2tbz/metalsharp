# libm12core Phase 3 native shader function cache

## Implemented

- Added native Objective-C Metal implementation file:
  - `vendor/dxmt/src/m12core/m12core_metal.c`
- Added `M12CORE_FEATURE_SHADER_FUNCTIONS`.
- Added C ABI:
  - `M12CoreShaderFunctionInputKind`
  - `M12CoreShaderFunctionStatus`
  - `M12CoreShaderFunctionDesc`
  - `M12CoreShaderFunctionResult`
  - `m12core_create_shader_function(...)`
- Added PE/native bridge:
  - `WMTM12CoreCreateShaderFunction(...)`
  - unixcall `141`
- Moved these responsibilities into `libm12core` for DXIL cached metallibs and generated MSL-source inputs:
  - Metal library creation
  - function fallback lookup
  - native in-process Metal function cache
- Preserved old WMT path as fallback if the new bridge is unavailable.
- Added comments marking this as the shader-function ownership seam; DXIL->MSL lowering and full reflection compatibility remain separate slices.

## Validation

- `tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight`
- Direct x86_64 native Metal shader-function probe:
  - `direct-shader-function-probe.txt`
  - second call returned `cache_hit=1` from `libm12core`.
- AC6 120s bounded run with m12core required and strict hashes:
  - `tools/d3d12-metal-sdk/results/bounded-launches/armored-core-vi-20260617-020620/summary.md`
  - drawn/present: `22/22`
  - failures: `0`
  - new shader artifacts: `0`
  - live native shader-function evidence: `m12core-log-lines.txt`
- Self-review: `self-review.md`

## Runtime hashes

- `d3d12.dll`: `d98ff6317e27254f3d54a7323ecb5e975b9ea0e1bc69c3682b19719912c9edd3`
- `dxgi.dll`: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- `dxgi_dxmt.dll`: `c765bda8bf0a526181343690e35d7657fb1c265ddf2f2a2a80ba30857eb21c8e`
- `winemetal.dll`: `b5bf192c50027aea4d5c02df158f2584db19d198bb42733f59978b269798f83f`
- `winemetal.so`: `973a26e32ce031171bfcb1a48cb595b9cdec20d4205fc6162ddb89254e57eb82`
- `libm12core.dylib`: `9c159d3acf9c14d919a2fff585d9b9dfcce18da245c818a8cf6176c1c64cf05f`

## Still remaining in Phase 3

- Move actual DXIL->MSL lowering ownership into `libm12core`.
- Move/replace full SM50/D3D12 reflection compatibility handling.
