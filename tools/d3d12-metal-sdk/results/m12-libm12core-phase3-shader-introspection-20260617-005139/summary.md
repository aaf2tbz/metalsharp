# libm12core Phase 3 shader introspection foundation

## Implemented

- Added stable shader stage ABI values to `m12core.h`.
- Added `M12CoreShaderBytecodeInfo` for native-core shader key/introspection results.
- Added platform-neutral shader helpers:
  - `m12core_hash_shader_bytecode(...)`
  - `m12core_shader_contains_dxil(...)`
- Added Phase-3 comments that scope this as cheap shader-key/introspection groundwork only. Metal function ownership, reflection ownership, and compile ownership remain in the existing D3D12/airconv path until later slices.

## Validation

- `tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight`
- Direct x86_64 `libm12core.dylib` shader probe:
  - `direct-shader-probe.txt`
  - observed: `abi=1 features=0x7 build=libm12core phase3 shader-introspection abi=1 ... contains_dxil=1 helper=1`
- AC6 60s bounded run with m12core enabled and strict hashes:
  - `tools/d3d12-metal-sdk/results/bounded-launches/armored-core-vi-20260617-005139/summary.md`
  - drawn/present: `20/20`
  - failures: `0`
  - m12core log confirms `features=0x7` and bridge first batch.

## Runtime hashes

- `d3d12.dll`: `3aae8ed1758b191a29ae52a630aeee67a30ac0b1f7b335751498563508d23c53`
- `dxgi.dll`: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- `dxgi_dxmt.dll`: `9b3a6dc46b5c7cc74abc354ddc7e7e01c487ff71ad19a1c7ec31666639514146`
- `winemetal.dll`: `b2b463044f67dd658b748a216cef70fb60dc2b82b5fdcc536eb7162e3829f505`
- `winemetal.so`: `9926f3d38c853bfaaab9c9d38ee5b081186c1f21a6d4e7ce91c15dae6411e44a`
- `libm12core.dylib`: `44833d1bb96eee5adbabdd0ff37de25c9e646907f385e6ae183b0cf98323f4c6`

## Remaining Phase 3 work

The native core can now own deterministic bytecode keying and DXIL detection, but the PE side still performs the actual shader cache lookup, DXIL->MSL compilation, Metal function creation, and reflection compatibility handling. Move those ownership domains in separate validated slices.
