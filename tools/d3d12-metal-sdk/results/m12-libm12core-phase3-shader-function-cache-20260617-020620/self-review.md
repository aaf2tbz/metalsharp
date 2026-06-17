# Self-review: Phase 3 native shader function cache

- Scope check: native `libm12core` now owns Metal library creation, function fallback lookup, and native function caching for cached metallib and generated MSL-source inputs.
- Boundary check: PE/native ABI remains POD-only; PE passes a native MTLDevice handle as an opaque integer and receives opaque retained MTLFunction/NSError handles.
- Fallback check: if unixcall `141` or `libm12core` is unavailable, D3D12 falls back to the previous WMT `newLibrary`/`newLibraryWithSource` + `newFunction` path.
- Remaining Phase 3 check: DXIL->MSL lowering and full SM50/D3D12 reflection compatibility still remain in D3D12/airconv pending later slices.
- Lifetime check: `libm12core` retains cached functions and returns retained handles compatible with existing `WMT::Reference` ownership.
- Validation check: build/stage/preflight passed, direct native x86_64 shader-function probe passed with second-call cache hit, AC6 120s strict-hash run rendered with no failure buckets.
