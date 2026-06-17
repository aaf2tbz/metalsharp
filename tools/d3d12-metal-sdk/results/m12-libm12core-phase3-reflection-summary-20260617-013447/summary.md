# libm12core Phase 3 reflection summary bridge

## Implemented

- Added `M12CoreShaderReflectionSummary` and `m12core_parse_shader_reflection(...)` to native `libm12core`.
- Added `WMTM12CoreParseShaderReflection(...)` PE thunk and unixcall `139`.
- Cached metallib load path now asks `libm12core` to parse compact reflection JSON for:
  - actual Metal entry point name
  - compute threadgroup size
- Preserved the old PE string parser as fallback.
- Added comments marking this as cache-side reflection-summary migration only; full SM50/airconv reflection ownership remains a later slice.

## Validation

- `tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight`
- Direct x86_64 reflection probe:
  - `direct-reflection-probe.txt`
  - observed entry point and threadgroup size parsed by `libm12core`.
- AC6 60s bounded run with m12core enabled and strict hashes:
  - `tools/d3d12-metal-sdk/results/bounded-launches/armored-core-vi-20260617-013447/summary.md`
  - drawn/present: `20/20`
  - failures: `0`
  - new shader artifacts: `0`
- Self-review: `self-review.md`

## Runtime hashes

- `d3d12.dll`: `0b92223c9b10057661aaa84eacc56c6f7e0eaf43d8db32bbc974f74aa6764ef1`
- `dxgi.dll`: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- `dxgi_dxmt.dll`: `6882be0426a543a6e6dc5062c99602b281303ce47ba062bcd2f21b610cb23ded`
- `winemetal.dll`: `e0bc6ec44342a26909a924c3e3dc15eebbb46f654387c23fe40b3b3db3095285`
- `winemetal.so`: `7370db024749dd70170a1a2e7e95b20ace4de64b4289d128034cafa801541649`
- `libm12core.dylib`: staged by `stage-runtime-metalsharp.json`

## Next safe slice

At this point Phase 3 has native ownership of shader keying, DXIL detection, cache path policy, cache lookup decisions, and cached reflection summary parsing. The next large step is actual DXIL->MSL compile and Metal function ownership; that should be treated as a separate higher-risk phase with narrower probes before AC6 runtime validation.
