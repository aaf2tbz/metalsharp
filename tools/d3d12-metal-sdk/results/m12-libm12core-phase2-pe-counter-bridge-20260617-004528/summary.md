# libm12core Phase 2 PE counter bridge

## Implemented

- Added a PE-side batching shim at `vendor/dxmt/src/d3d12/d3d12_m12core_counters.hpp`.
  - Hot-path `PSO_PRESSURE` events update relaxed local atomics.
  - Deltas flush every 64 events through one compact bridge call.
  - Rendering stays best-effort/non-blocking if the optional native core is disabled.
- Added `WMTM12CoreRecordCounters()` to the winemetal PE thunk surface.
- Added unixcall `135` in `winemetal.so` to apply batched deltas into native `libm12core` counters.
- Wired existing D3D12 hot-path pressure points into the bridge:
  - graphics/compute PSO requests and repeated requests
  - shader memory cache hits/misses
  - shader metallib cache hits/misses
  - render/compute Metal pipeline creates
  - render/compute pipeline cache hits/misses
  - async compile wait count/ns
- Added comments at the PE/native ownership seam to make future refactors easier to scope.
- Added `METALSHARP_M12CORE_DUMP_COUNTERS` backend/bounded-launch plumbing for diagnostic snapshots.

## Validation

- Built/staged/preflighted updated runtime:
  - `tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`
  - `tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime`
  - `tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight`
- Rust env override test:
  - `cargo test m12core_launch_overrides_map_to_dxmt_loader_env`
- User-confirmed 150s AC6 visual run with m12core enabled and no perf overrides:
  - `tools/d3d12-metal-sdk/results/bounded-launches/armored-core-vi-20260617-003936/summary.md`
  - drawn/present: `22/22`
  - failures: `0`
- 60s AC6 bridge proof with m12core enabled and strict hashes:
  - `tools/d3d12-metal-sdk/results/bounded-launches/armored-core-vi-20260617-004528/summary.md`
  - drawn/present: `20/20`
  - failures: `0`
  - env override proof: `ac6-60s-env-proof.txt`
  - bridge log proof: `m12core-bridge-log-lines.txt`

## Runtime hashes after bridge

- `d3d12.dll`: `6e617ad6849ab0c5985c4fde7c7ff2be0af0a1926a2a2574956e7ef15426d8b3`
- `dxgi.dll`: `dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24`
- `dxgi_dxmt.dll`: `4a6d1a1e8ab419d13d7cdde84f2a62c6f4e5fd923c8fe5b1820518062090041c`
- `winemetal.dll`: `fd0211f5b137e065f9f6b789bd109a716b90ea6f20456ac93107693a51700d7b`
- `winemetal.so`: `02e7ea2dea8167c6ef80ec0a72d7fe45d58c9ddbc375866a37e5508523ac8800`
- `libm12core.dylib`: `5b0bdc00925c67c8495c768fa46de97820231403a39aa46c714e7c95bf348b09`

## Notes

The bridge moves aggregate diagnostics ownership toward `libm12core` without moving shader/PSO object ownership yet. The existing `PSO_PRESSURE` log lines remain the oracle for perf-analysis while the native counter API matures.
