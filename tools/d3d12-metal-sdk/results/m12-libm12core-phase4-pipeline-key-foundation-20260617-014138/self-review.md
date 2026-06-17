# Self-review: Phase 4 pipeline key foundation

- Scope check: only final device-scoped pipeline cache key finalization moved into `libm12core`; descriptor normalization and Metal PSO object ownership remain in D3D12.
- Fallback check: if unixcall `140` or `libm12core` is unavailable, D3D12 falls back to the previous PE-side device-handle hash combine.
- ABI check: fixed-size C input/output structs only; no Metal/PE C++ objects cross the boundary.
- Cache behavior check: this affects only in-process pipeline cache keys, not disk shader/metallib cache filenames.
- Hot-path check: the bridge runs at pipeline compile/cache lookup time, not per draw.
- Validation check: build/stage/preflight passed, direct key probe passed, AC6 60s strict-hash run rendered with no failure buckets.
