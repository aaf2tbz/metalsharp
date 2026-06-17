# libm12core Phase 3 PE shader key bridge

## Implemented

- Added `WMTM12CoreHashShaderBytecode()` to the winemetal PE thunk surface.
- Added unixcall `136` to forward shader bytecode/stage metadata into native `libm12core`.
- Updated `d3d12_pipeline_state.cpp` to use the native core for:
  - shader bytecode cache-key hashing
  - DXIL-container detection
- Preserved the existing shader cache filename namespace by mapping stable `M12CORE_SHADER_STAGE_*` ABI values back to the legacy PE `ShaderType` numeric values for the hash seed.
- Kept a PE-local fallback path when `libm12core` is disabled or the native bridge is unavailable.
- Added comments documenting this as key/introspection migration only; Metal function creation, reflection, DXIL->MSL lowering, and cache object ownership remain on the old path for later non-breaking slices.

## Validation

- `tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight`
- Direct x86_64 shader probe:
  - `direct-shader-bridge-probe.txt`
  - observed `features=0x7`, `contains_dxil=1`, helper success.
- AC6 60s bounded run with m12core enabled and strict hashes:
  - `tools/d3d12-metal-sdk/results/bounded-launches/armored-core-vi-20260617-010627/summary.md`
  - drawn/present: `20/20`
  - failures: `0`
  - new shader artifacts: `0`
  - bridge log confirms m12core load and counter bridge.

## Runtime hashes

- `d3d12.dll`: `f14e3f9b0098e6e62919ba1932eb1a7e794e96d298827220c37b942d93f781b5`
- `dxgi.dll`: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- `dxgi_dxmt.dll`: `ab73d7539af1757f3c470e2c3003d72de0efc07c15a29b6ae8a94f22b75ffbd1`
- `winemetal.dll`: `aa359ce0ef227c1994d44dcab4a89552cfc0b8fbad2da9729755f76410c6149f`
- `winemetal.so`: `2522532d548657efadd1caaa3e101acb97e9db298365bddc915f3e4028c1cf16`
- `libm12core.dylib`: `f5223e8fc6dcc641320a598cd420581b23d1cc95341919862829bea6fa88067a`
