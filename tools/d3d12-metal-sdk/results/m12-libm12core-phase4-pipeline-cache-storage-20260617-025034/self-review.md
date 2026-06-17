# Self-review: Phase 4 native pipeline cache storage

- Scope check: only retained in-process Metal pipeline cache storage moved into `libm12core`; D3D12 still owns PSO key field accumulation and Metal pipeline creation.
- Lifetime check: native Objective-C cache retains stored pipeline objects and returns retained handles to existing `WMT::Reference` wrappers.
- Fallback check: if unixcalls `144`/`145` or `libm12core` are unavailable, D3D12 uses the previous local `g_*_pipeline_cache` maps.
- Boundary check: cache API uses POD query/result structs and opaque object handles only.
- Validation check: build/stage/preflight passed, direct retained-handle cache probe passed, AC6 120s strict-hash run rendered with no failure buckets and native cache hit lines.
