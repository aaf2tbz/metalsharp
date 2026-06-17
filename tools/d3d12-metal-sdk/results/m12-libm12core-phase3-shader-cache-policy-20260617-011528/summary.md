# libm12core Phase 3 shader cache policy bridge

## Implemented

- Added `M12CoreShaderCachePaths` and `m12core_format_shader_cache_paths(...)` to the stable C ABI.
- Added `WMTM12CoreFormatShaderCachePaths(...)` PE thunk and unixcall `137`.
- Updated `d3d12_pipeline_state.cpp` shader compile/cache lookup path to ask `libm12core` for deterministic shader cache paths when enabled.
- Preserved the legacy PE path formatter as fallback when `libm12core` is disabled or unavailable.
- Added code comments at the migration seam documenting that this slice moves path/key policy only; file IO, DXIL->MSL lowering, Metal function ownership, and reflection remain on the old path.

## Validation

- `tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight`
- Direct x86_64 path-format probe:
  - `direct-path-probe.txt`
  - observed expected legacy-compatible paths under `/tmp/m12-cache/000000001234abcd*`.
- AC6 60s bounded run with m12core enabled and strict hashes:
  - `tools/d3d12-metal-sdk/results/bounded-launches/armored-core-vi-20260617-011528/summary.md`
  - drawn/present: `20/20`
  - failures: `0`
  - new shader artifacts: `0`
- Self-review: `self-review.md`

## Runtime hashes

- `d3d12.dll`: `e719d8f0565b31131fecd7a134949e1132e4cfb1427d4efac74842ece0dc98af`
- `dxgi.dll`: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- `dxgi_dxmt.dll`: `3db41d5f0aa83db0258daa663a9f71096231ad55a45e1780b40d38da907a461d`
- `winemetal.dll`: `e9bbe2af43e9ccb7fc46fa8682621c96b0483d2f378c81f6cfff3eee8d4e4e6c`
- `winemetal.so`: `9e6c9db95f219cf101832d59f120cf95d905173a3c02d508ac781e72a7581e45`
- `libm12core.dylib`: `7c9da3a673c94d8602b388ff6c5ff3e29d4d08e764b2f4886ac6b8c48ba88360`

## Next safe slice

Move shader cache lookup decisions/results into `libm12core` while still leaving actual file reads/writes and Metal library/function creation on the PE/winemetal path, or move reflection-summary formatting next if we want an even smaller migration step.
