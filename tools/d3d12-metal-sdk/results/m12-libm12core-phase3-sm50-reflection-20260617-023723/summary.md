# libm12core Phase 3 SM50 reflection compatibility

## Implemented

- Added `M12CORE_FEATURE_SM50_REFLECTION`.
- Added C ABI:
  - `M12CoreSM50ReflectionStatus`
  - `M12CoreSM50ShaderReflection`
  - `M12CoreSM50ShaderArgument`
  - `M12CoreSM50ReflectionResult`
  - `m12core_reflect_sm50_shader(...)`
- Added PE/native bridge:
  - `WMTM12CoreReflectSM50Shader(...)`
  - unixcall `143`
- `libm12core` now owns SM50 reflection and argument extraction for non-DXIL bytecode.
- D3D12 maps the POD result back into existing `MTL_SHADER_REFLECTION` and `MTL_SM50_SHADER_ARGUMENT` vectors.
- DXIL bytecode is guarded out of the SM50 reflection ABI to avoid expected init failures.
- Legacy PE-side `SM50GetArgumentsInfo` remains fallback.

## Validation

- `tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight`
- Direct x86_64 SM50 reflection probe on legacy DXBC:
  - `direct-sm50-reflection-probe.txt`
  - observed `status=0`, two reflected arguments.
- AC6 120s bounded run with m12core required and strict hashes:
  - `tools/d3d12-metal-sdk/results/bounded-launches/armored-core-vi-20260617-023723/summary.md`
  - drawn/present: `21/21`
  - failures: `0`
  - new shader artifacts: `0`
  - runtime m12core evidence: `m12core-log-lines.txt`
- Self-review: `self-review.md`

## Runtime hashes

- `d3d12.dll`: `f0788fdc4f340843939337465f321b3515b5ca441902eae2eef4db16db0a3b8f`
- `dxgi.dll`: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- `dxgi_dxmt.dll`: `ac7c21bcf3b985a33fe0b517ccb543270b2a808c9a2c5db317d758b1de368cbd`
- `winemetal.dll`: `3061f6375e272f5d1e17d25537c5f5194f74d2a8be5443c8e244932afc923b3a`
- `winemetal.so`: `67b6beca3ed64c62ebe57323aac06f0d3927937af4da8bb001da341086e33dd1`
- `libm12core.dylib`: `72fccfe55db61b26df2123285fd0bf0af696df84343b05ffc30f969d1ec51e8f`

## Phase 3 state

Runtime shader ownership for Phase 3 is now covered by `libm12core`: keying, cache policy, cache lookup, DXIL->MSL lowering, Metal function creation/cache, cached reflection summaries, and SM50 reflection compatibility. Diagnostic/report file ownership can move later as an observability cleanup, but it is no longer blocking Phase 3 runtime ownership.
