# M12 Core Convergence PE Responsibility Inventory

## Purpose

This is the C1 inventory for thinning the PE side during the M12 Core Convergence Flow. It classifies current PE/DXMT responsibilities so later slices can migrate renderer/cache policy into `libm12core` without breaking Windows ABI compatibility or fallback behavior.

## Classification legend

| Class | Meaning |
|---|---|
| Windows ABI / COM surface | Must remain PE-visible because games call Windows exports and COM interfaces. |
| Command serialization | Should remain PE-adjacent initially: convert COM method calls into stable POD packets. |
| PE/native bridge transport | Thunk/unixcall transport; should contain minimal policy. |
| Renderer policy to migrate | Should move to `libm12core` or native side once POD packets and handles are available. |
| Cache policy to migrate | Should become cache-first `libm12core` policy with strict compatibility keys. |
| Legacy fallback | Must remain available until native path is default-proven. |

## Current component inventory

### `d3d12.dll`

Representative files:

- `vendor/dxmt/src/d3d12/d3d12_device.cpp`
- `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp`
- `vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp`
- `vendor/dxmt/src/d3d12/d3d12_root_signature.cpp`
- `vendor/dxmt/src/d3d12/d3d12_descriptor_heap.cpp`
- `vendor/dxmt/src/d3d12/d3d12_resource.cpp`
- `vendor/dxmt/src/d3d12/d3d12_swapchain.cpp`

| Responsibility | Class | C1/C2 target |
|---|---|---|
| D3D12 exports and COM object identity | Windows ABI / COM surface | Stay PE-visible. |
| Device creation and feature queries | Windows ABI / COM surface + legacy fallback | Stay PE-visible; native core receives scalar capability/device identity summaries later. |
| Command list method capture | Command serialization | Convert to C1 POD command packets in C2. |
| Command replay / execution decisions | Renderer policy to migrate | Move behind native packet validator/executor in C3-C8. |
| Root signature parsing and binding lookup | Renderer policy to migrate + cache policy to migrate | Already partially summarized in `libm12core`; cache root/binding plans in C5/C7. |
| Shader translation and MSL lowering | Renderer policy to migrate + cache policy to migrate | Already partially moved; C7 makes cache-first lookup authoritative. |
| PSO normalization / creation / caching | Renderer policy to migrate + cache policy to migrate | Already partially moved; C7 proves warm-cache skipped work. |
| Resource state / barrier handling | Renderer policy to migrate | Packetize and migrate with native replay/render-pass ownership. |
| Swapchain/present planning | Renderer policy to migrate | Expand native presenter ownership in C6. |
| Existing PE replay path | Legacy fallback | Keep until C10 final validation proves native path. |

### `dxgi.dll`

Representative files:

- `vendor/dxmt/src/dxgi/dxgi_bootstrap.c`

| Responsibility | Class | C1/C2 target |
|---|---|---|
| DXGI bootstrap loader | Windows ABI / COM surface | Keep thin bootstrap. |
| Loading `dxgi_dxmt.dll` | PE/native bridge transport | Keep until optional future `dxgi_m12.dll` route exists. |

### `dxgi_dxmt.dll`

Representative files:

- `vendor/dxmt/src/dxgi/dxgi_factory.cpp`
- `vendor/dxmt/src/dxgi/dxgi_adapter.cpp`
- `vendor/dxmt/src/dxgi/dxgi.cpp`

| Responsibility | Class | C1/C2 target |
|---|---|---|
| DXGI factory/adapter/output COM wrappers | Windows ABI / COM surface | Stay PE-visible. |
| Swapchain factory policy | Renderer policy to migrate | Migrate supported present/swapchain policy after C6. |
| Adapter/device identity reporting | Cache policy to migrate | Feed P11 compatibility key dimensions. |
| Legacy DXGI behavior | Legacy fallback | Keep available. |

### `winemetal.dll`

Representative files:

- `vendor/dxmt/src/winemetal/winemetal_thunks.c`
- `vendor/dxmt/src/winemetal/winemetal_thunks.h`
- `vendor/dxmt/src/winemetal/winemetal.h`

| Responsibility | Class | C1/C2 target |
|---|---|---|
| PE thunk ABI | PE/native bridge transport | Keep and make more mechanical. |
| Scalar unixcall payload definitions | PE/native bridge transport | Extend append-only as needed. |
| Renderer policy embedded in thunk call selection | Renderer policy to migrate | Push decisions into `libm12core`/native side. |
| Existing fallback thunks | Legacy fallback | Keep available. |

### `winemetal.so`

Representative files:

- `vendor/dxmt/src/winemetal/unix/winemetal_unix.c`
- `vendor/dxmt/src/winemetal/Metal.hpp`

| Responsibility | Class | C1/C2 target |
|---|---|---|
| Wine unixlib/native loader integration | PE/native bridge transport | Keep. |
| `libm12core` loader/fallback | PE/native bridge transport + legacy fallback | Keep robust. |
| Native Metal object ownership | Renderer policy to migrate | Native objects stay native; expose scalar IDs to `libm12core` in C3. |
| Present blit native execution seam | Renderer policy to migrate | Expand in C6 with explicit gates. |
| Error/object lifetime translation | PE/native bridge transport | Keep mechanical; do not expose C++/ObjC across ABI. |

### `libm12core.dylib`

Representative files:

- `vendor/dxmt/src/m12core/m12core.h`
- `vendor/dxmt/src/m12core/m12core.cpp`
- `vendor/dxmt/src/m12core/m12core_metal.c`

| Responsibility | Class | C1/C2 target |
|---|---|---|
| Stable public C/POD ABI | Native core owner | Extend with C1 packet/schema ABI. |
| Shader/PSO/root planning and partial native ownership | Renderer policy to migrate + cache policy to migrate | Continue to centralize. |
| Command packet validation | Native core owner | Add in C1, feed from real PE stream in C2. |
| Cache compatibility keying | Native core owner | Add in C1, use shadow index in C2/C7. |
| Future command replay/presenter executor | Native core owner | C4-C8. |

## C2 migration targets

C2 should not execute natively. It should only record and validate:

1. Command list packet streams from PE command-list/replay code.
2. Per-stream packet validation summary from `m12core_validate_command_packet_stream`.
3. Shadow cache compatibility keys from `m12core_make_cache_compatibility_key` for shaders, PSOs, root/binding plans, and prewarm packs.
4. Bounded diagnostics for packet validity, unsupported packet kinds, and missing cache dimensions.

## Non-goals for C1

- No game launch.
- No native replay execution.
- No PE thinning implementation beyond inventory.
- No cache reuse.
- No raw cache/metallib/DXBC payload commitment.
