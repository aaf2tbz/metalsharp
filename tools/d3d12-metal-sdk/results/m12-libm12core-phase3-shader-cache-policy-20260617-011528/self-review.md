# Self-review: Phase 3 shader cache policy

- Scope check: only shader cache path formatting/lookup policy moved into `libm12core`; file IO, DXIL->MSL lowering, Metal function creation, and reflection compatibility remain on the existing PE path.
- Fallback check: `FormatM12CoreShaderCachePaths()` returns false when the native bridge is unavailable, and the old `FormatShaderCachePath()` logic remains intact.
- Cache compatibility check: native formatting preserves the existing `<cache-root>/<016x>{.dxbc,.metallib,.json,...}` filenames.
- ABI check: new structs use fixed-size C fields, no STL/C++ objects cross PE/native boundaries.
- Hot-path check: path bridge is used at shader compile/cache lookup points, not per draw.
- Validation check: build/stage/preflight passed, direct path probe passed, AC6 60s strict-hash run rendered with no failure buckets.
