# libm12core Phase 3 DXIL->MSL lowering ownership

## Implemented

- Added `M12CORE_FEATURE_DXIL_TO_MSL`.
- Added C ABI:
  - `M12CoreVertexInputElement`
  - `M12CoreDXILToMSLStatus`
  - `M12CoreDXILToMSLDesc`
  - `M12CoreDXILToMSLResult`
  - `m12core_lower_dxil_to_msl(...)`
- Added PE/native bridge:
  - `WMTM12CoreLowerDXILToMSL(...)`
  - unixcall `142`
- Linked native `libm12core` against native airconv so it owns:
  - DXIL container parsing
  - LLVM bitcode parsing
  - typed `MSLLowering`
  - fallback `DXILToMSL`
  - MSL source output sizing/copying
- D3D12 now calls `libm12core` for DXIL->MSL source generation first, with old PE-side lowering fallback if the core ABI is unavailable.
- Comments mark the remaining diagnostic seam: D3D12 still writes module summaries and compile reports until diagnostics move into the core.

## Validation

- `tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime`
- `tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight`
- Direct x86_64 native DXIL->MSL probe on an AC6 DXIL payload:
  - `direct-dxil-to-msl-probe.txt`
  - observed `status=0`, `entry=vs_main`, `typed=1`.
- AC6 120s bounded run with m12core required and strict hashes:
  - `tools/d3d12-metal-sdk/results/bounded-launches/armored-core-vi-20260617-022128/summary.md`
  - drawn/present: `22/22`
  - failures: `0`
  - new shader artifacts: `0`
  - live native DXIL->MSL evidence: `m12core-log-lines.txt`
- Self-review: `self-review.md`

## Runtime hashes

- `d3d12.dll`: `76cbc1ae10c17429f2f91ad7d9988c59d88e91d7153ae50f4f6eba2fffdd09cc`
- `dxgi.dll`: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- `dxgi_dxmt.dll`: `f764f937730ed5acc722e0897e6f9228666d91331e55b76b01a5bb24fbce19af`
- `winemetal.dll`: `9a322168222799975f9c10e145fc26be698fef5432742123807a2c5788223edc`
- `winemetal.so`: `44d8d9a47716392b7dabdcc2ff67a18875ad96a73ada061b2944baa447fa6c80`
- `libm12core.dylib`: `73b8fc05bf268b53ba4349c50ec852a2fe430ce6c6a47de46ecb7caa9bc29f72`

## Still remaining in Phase 3

- Full SM50/D3D12 reflection compatibility ownership.
- Native-core diagnostics/report ownership for module summaries and compile reports.
