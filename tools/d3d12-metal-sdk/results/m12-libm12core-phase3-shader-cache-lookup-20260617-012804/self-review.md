# Self-review: Phase 3 shader cache lookup result ownership

- Scope check: `libm12core` now determines shader cache paths plus whether a metallib is usable; PE-side code still performs file reads, DXIL->MSL compilation, Metal library creation, and reflection handling.
- Fallback check: if unixcall `138` or `libm12core` is unavailable, the legacy PE `fopen` + force-source logic remains active.
- Compatibility check: cache paths preserve existing `<root>/<016x>.metallib` and related filenames; no cache invalidation is intended.
- ABI check: only fixed-size C structs and primitive fields cross PE/native; no C++/STL objects cross.
- Hot-path check: lookup bridge is used at shader cache lookup/compile time, not per draw.
- Validation check: build/stage/preflight passed, direct lookup probe passed, AC6 60s strict-hash run rendered with no failure buckets.
