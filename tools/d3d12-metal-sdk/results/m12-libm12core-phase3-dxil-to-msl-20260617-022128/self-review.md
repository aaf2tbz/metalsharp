# Self-review: Phase 3 DXIL->MSL ownership

- Scope check: `libm12core` now owns DXIL container parsing, LLVM bitcode parsing, typed `MSLLowering`, fallback `DXILToMSL`, and MSL source production.
- Boundary check: vertex-input lowering metadata crosses the PE/native boundary as POD `M12CoreVertexInputElement` values; no C++ vectors/strings cross.
- Fallback check: if unixcall `142` or `libm12core` is unavailable, D3D12 falls back to the previous PE-side lowering path.
- Diagnostic check: D3D12 still parses DXIL/bitcode for existing module summaries and compile reports; this is intentionally left until diagnostics move behind a native ABI.
- Follow-up check: full SM50/D3D12 reflection compatibility remains the only Phase 3 item not fully owned by `libm12core`.
- Validation check: build/stage/preflight passed, direct native x86_64 DXIL->MSL probe passed on an AC6 DXIL payload, AC6 120s strict-hash run rendered with no failure buckets.
