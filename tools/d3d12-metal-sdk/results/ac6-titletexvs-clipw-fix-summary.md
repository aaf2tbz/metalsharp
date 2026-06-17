# AC6 TitleTexVs clip-W lowering fix summary

## Finding

DXIL disassembly for AC6 `ca33abe9a2d27ce9` / `TitleTexVs` stores `SV_Position.w = 1.0`, but the cached M12 MSL emitted `out.position.w = 0.0`, which can kill rasterization for the pass even when bound textures are valid.

Root cause: DXIL MSL lowering treated any value string containing `.` as unsafe and returned the caller fallback. Float constants such as `1.000000e+00` therefore became fallback `0.0` in `storeOutput`.

## Fix

`vendor/dxmt/src/airconv/dxil/msl_lowering.cpp` now preserves dotted scalar numeric literals (`exprLooksScalarLiteral`) while still rejecting non-literal dotted/swizzle expressions.

A debug-only `airconv --emit-msl --msl-vertex-input=...` path was added so runtime-style vertex-pull lowering can be reproduced offline from captured PSO input layouts.

## Offline validation

- Exact shader/runtime-style validation: `tools/d3d12-metal-sdk/results/ac6-titletexvs-runtime-style-20260616-220943/summary.md`
  - previous cached line: `out.position.w = 0.0;`
  - fixed runtime-style line: `out.position.w = 1.0f;`
  - `airconv_rc=0`, `metal_rc=0`
- Small AC6 vertex regression: `tools/d3d12-metal-sdk/results/ac6-runtime-style-vertex-lowering-regression-20260616-221026/summary.md`
  - 12 unique AC6 vertex shaders
  - all runtime-style lowered and Metal-compiled successfully

## Rebuilt/staged runtime hashes

Staging manifest: `tools/d3d12-metal-sdk/results/stage-runtime-metalsharp.json`

- `d3d12.dll`: `57e1d5e9d239e58e8d438be9ff61532282a84228738a63c1b92fdd397b58f9bf`
- `dxgi_dxmt.dll`: `0a0d553b649beb6e0f9c62fd327257f70757df07f2ae3cf4713882de21e3685e`
- `dxgi.dll`: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- `winemetal.dll`: `7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85`
- `winemetal.so`: `cad141357fecf172c9c3966daeb8c5d0972d4e06d383e30cf495593dbc9dab7d`

Note: `verify-m12-runtime-hashes.py` still contains previous expected hashes and reports expected-mismatch for rebuilt `d3d12.dll`/`dxgi_dxmt.dll`; the stage manifest shows source/destination hash matches for the rebuilt artifacts.
