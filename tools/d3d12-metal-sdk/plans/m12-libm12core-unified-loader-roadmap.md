# libm12core unified loader roadmap

## Purpose

Move M12 from a scattered PE-DLL-centered implementation toward a GPTK/D3DMetal-style architecture with thin Windows ABI shims and a unified native/core runtime library, tentatively `libm12core`.

The aim is not to hide complexity in one giant file. The aim is to centralize ownership of the hot runtime systems that currently span many `.cpp`/`.hpp` files:

- Metal device/object ownership
- shader compilation and function caching
- graphics/compute PSO keying and pipeline caching
- root signature and descriptor binding translation
- command replay state
- diagnostics/performance counters
- future D3DMetal oracle/prewarm ingestion

## Current architecture

Current staged runtime shape:

```text
x86_64-windows/d3d12.dll        PE D3D12 implementation
x86_64-windows/dxgi.dll         tiny DXGI bootstrap shim
x86_64-windows/dxgi_dxmt.dll    real DXGI implementation
x86_64-windows/winemetal.dll    PE thunk bridge
x86_64-unix/winemetal.so        Unix/macOS Metal bridge implementation
x86_64-unix/libc++.1.dylib      C++ runtime sidecar
x86_64-unix/libc++abi.1.dylib   ABI sidecar
x86_64-unix/libunwind.1.dylib   unwind sidecar
```

Current high-level flow:

```text
Game
  -> d3d12.dll / dxgi.dll
    -> dxgi_dxmt.dll
      -> winemetal.dll PE thunks
        -> winemetal.so Unix thunks
          -> Metal.framework / shader compiler / native resources
```

Important detail: `dxgi.dll` is only a bootstrap. It dynamically loads `dxgi_dxmt.dll` with `LoadLibraryA("dxgi_dxmt.dll")` and forwards DXGI exports.

## Target architecture

Target staged runtime shape:

```text
x86_64-windows/d3d12.dll        thin-but-compatible D3D12 COM/export shim
x86_64-windows/dxgi.dll         bootstrap shim, still loads dxgi_dxmt.dll or future dxgi_m12.dll
x86_64-windows/dxgi_dxmt.dll    PE DXGI shim, gradually thinned
x86_64-windows/winemetal.dll    PE thunk bridge / native loader ABI
x86_64-unix/winemetal.so        Wine unixlib bridge and loader for native core
x86_64-unix/libm12core.dylib    unified native M12 runtime core
x86_64-unix/libc++.1.dylib      runtime sidecar
x86_64-unix/libc++abi.1.dylib   runtime sidecar
x86_64-unix/libunwind.1.dylib   runtime sidecar
```

Target flow:

```text
Game
  -> thin PE D3D12/DXGI COM objects
    -> stable M12 C ABI through winemetal thunk layer
      -> libm12core.dylib
        -> Metal device/resources/pipelines
        -> DXIL/MSL compiler and shader cache
        -> root/descriptor binding mapper
        -> command replay/presenter
```

## Design principles

1. **Do not cross the PE/native boundary with C++ objects.**
   - Use stable C ABI handles and POD structs.
   - No STL, exceptions, C++ virtuals, `std::string`, or owning C++ objects across the boundary.

2. **Keep Windows ABI shims intact.**
   - Games still need `d3d12.dll`, `dxgi.dll`, COM interfaces, and expected exports.
   - `libm12core` should not pretend to be a Windows DLL.

3. **Move ownership, not just code.**
   - The point is centralizing lifetime/cache/key ownership in one runtime core.
   - Facades that still leave cache state scattered do not solve the real problem.

4. **Migrate in vertical slices.**
   - Start with shader/PSO cache ownership.
   - Move binding mapper next.
   - Move command replay/presenter last.

5. **Keep M12 recoverable.**
   - Preserve current working staged runtime fallback.
   - Add env-gated loader path first.
   - Keep strict hash gates and bounded-run validation.

## Proposed native C ABI

Initial C ABI should be deliberately small:

```c
typedef uint64_t m12_device_handle;
typedef uint64_t m12_shader_handle;
typedef uint64_t m12_render_pso_handle;
typedef uint64_t m12_compute_pso_handle;
typedef uint64_t m12_root_signature_handle;
typedef uint64_t m12_descriptor_heap_handle;
typedef uint64_t m12_command_stream_handle;

typedef struct M12CoreVersion {
  uint32_t abi_version;
  uint32_t feature_flags;
  uint32_t build_id_low;
  uint32_t build_id_high;
} M12CoreVersion;

int m12core_get_version(M12CoreVersion *out);
int m12core_create_device(const M12DeviceDesc *desc, m12_device_handle *out);
void m12core_release_device(m12_device_handle device);

int m12core_create_root_signature(
    m12_device_handle device,
    const M12RootSignatureDesc *desc,
    m12_root_signature_handle *out);

int m12core_compile_shader(
    m12_device_handle device,
    const M12ShaderCompileDesc *desc,
    m12_shader_handle *out,
    M12ShaderReflection *reflection_out);

int m12core_create_render_pso(
    m12_device_handle device,
    const M12RenderPsoDesc *desc,
    m12_render_pso_handle *out);

int m12core_create_compute_pso(
    m12_device_handle device,
    const M12ComputePsoDesc *desc,
    m12_compute_pso_handle *out);

int m12core_get_counters(
    m12_device_handle device,
    M12RuntimeCounters *out);
```

Later C ABI expansion:

```c
int m12core_create_resource(...);
int m12core_create_descriptor_heap(...);
int m12core_update_descriptor(...);
int m12core_submit_command_stream(...);
int m12core_present(...);
int m12core_import_oracle_pack(...);
int m12core_prewarm_pipelines(...);
```

## Phase 0: Document and freeze current boundaries

### Work

- Document every current runtime artifact and source file responsibility.
- Record the current D3D12/DXGI/winemetal load chain.
- Add loader diagnostics that prove which DLL/SO/dylib path was loaded.
- Preserve current runtime hash manifests.

### Files involved

- `tools/d3d12-metal-sdk/plans/`
- `tools/d3d12-metal-sdk/scripts/stage-dxmt-runtime.py`
- `vendor/dxmt/src/dxgi/dxgi_bootstrap.c`
- `vendor/dxmt/src/winemetal/main.c`
- `vendor/dxmt/src/winemetal/unix/winemetal_unix.c`

### Done when

- A bounded launch log can prove:
  - `dxgi.dll` bootstrap loaded
  - `dxgi_dxmt.dll` loaded
  - `winemetal.dll` loaded
  - `winemetal.so` loaded
  - M12 shader cache path selected

## Phase 1: Add `libm12core.dylib` as a loadable but inert native core

Status: implemented in `tools/d3d12-metal-sdk/results/m12-libm12core-phase1-loader-20260617-000501/summary.md`.

### Work

- Add a new native target under `vendor/dxmt/src/m12core/`.
- Build `libm12core.dylib` for x86_64 macOS/Rosetta runtime compatibility.
- Export only basic ABI/version functions.
- Load it from `winemetal.so` behind an env gate:

```text
DXMT_M12CORE_ENABLE=1
DXMT_M12CORE_PATH=/path/to/libm12core.dylib
```

- If loading fails, fall back to current path unless `DXMT_M12CORE_REQUIRED=1`.

### Files to add

```text
vendor/dxmt/src/m12core/m12core.h
vendor/dxmt/src/m12core/m12core.cpp
vendor/dxmt/src/m12core/m12core_loader.c/.h
vendor/dxmt/src/m12core/meson.build
```

### Files to modify

```text
vendor/dxmt/src/winemetal/unix/winemetal_unix.c
vendor/dxmt/src/winemetal/meson.build
vendor/dxmt/meson.build
scripts/stage-dxmt-runtime.py
```

### Done when

- Runtime stages `libm12core.dylib`.
- Bounded AC6 launch proves it loaded or cleanly fell back.
- No rendering behavior changes when inert.

## Phase 2: Move runtime counters and diagnostics into `libm12core`

Status: complete. Counter ABI/storage foundation is recorded in `tools/d3d12-metal-sdk/results/m12-libm12core-phase2-counters-20260617-001327/summary.md`; PE hot-path `PSO_PRESSURE` batching through winemetal is recorded in `tools/d3d12-metal-sdk/results/m12-libm12core-phase2-pe-counter-bridge-20260617-004528/summary.md`.

### Work

- Move `PSO_PRESSURE` counter ownership into native core.
- PE/D3D12 side calls counter ABI instead of owning all static counters locally.
- Keep existing log format stable.

### Why first

Counters are low-risk and prove the library is participating without moving rendering ownership yet.

### Done when

- `perf-analysis.json` still reports:
  - graphics/compute PSO requests
  - unique/repeated counts
  - shader cache hits/misses
  - Metal pipeline creates
  - pipeline cache hits/misses
- AC6 bounded smoke remains `drawn_present == present`, failures `0`.

## Phase 3: Move shader function cache into `libm12core`

Status: shader bytecode keying and DXIL-detection foundation implemented in `tools/d3d12-metal-sdk/results/m12-libm12core-phase3-shader-introspection-20260617-005139/summary.md`; PE shader-key bridge implemented in `tools/d3d12-metal-sdk/results/m12-libm12core-phase3-pe-shader-key-bridge-20260617-010627/summary.md`; shader cache path policy bridge implemented in `tools/d3d12-metal-sdk/results/m12-libm12core-phase3-shader-cache-policy-20260617-011528/summary.md`; shader cache lookup result ownership implemented in `tools/d3d12-metal-sdk/results/m12-libm12core-phase3-shader-cache-lookup-20260617-012804/summary.md`. Remaining work: move DXIL->MSL compilation, Metal function ownership, and reflection compatibility handling in later slices.

### Work

Centralize:

- shader bytecode hashing
- DXIL container detection
- DXIL -> MSL compile path
- in-process Metal function cache
- metallib/source cache lookup policy
- shader reflection summary structure

### Current source to extract from

```text
vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp
vendor/dxmt/src/airconv/dxil/*
vendor/dxmt/src/airconv/*
```

### New core APIs

```c
int m12core_compile_shader(...);
int m12core_lookup_shader(...);
int m12core_get_shader_reflection(...);
```

### Important constraints

- The PE side can pass bytecode bytes and layout metadata.
- `libm12core` owns Metal functions and returns opaque shader handles.
- If D3D12-side code still needs legacy SM50 reflection handles, keep a compatibility fallback until replaced.

### Done when

- Repeated DXIL shader work is served from `libm12core` cache.
- AC6 still renders.
- Existing offline `airconv` validation remains unchanged.

## Phase 4: Move graphics/compute PSO keying and Metal pipeline cache into `libm12core`

### Work

Centralize normalized PSO keys:

- shader handles/hashes
- root signature key
- input layout key
- RTV/DSV/sample formats
- blend/raster/depth/stencil state
- topology
- vertex descriptor/stage-in mode
- device identity

`libm12core` owns:

- Metal render pipeline cache
- Metal compute pipeline cache
- pipeline creation retry policy
- pipeline creation diagnostics

### Current source to extract from

```text
vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp
vendor/dxmt/src/d3d12/d3d12_vertex_input.hpp
```

### Done when

- D3D12 PE side no longer calls `newRenderPipelineState` directly.
- AC6 counters clearly distinguish:
  - D3D12 PSO requests
  - normalized unique PSO keys
  - Metal pipeline cache hits/misses
  - actual Metal pipeline creates

## Phase 5: Move root signature + descriptor binding mapper into `libm12core`

### Work

Centralize translation from D3D12 root/descriptor model to Metal argument buffer model:

- root descriptor tables
- root CBV/SRV/UAV descriptors
- static samplers
- descriptor visibility
- register/space lookup
- argument buffer layout
- null/default resource policy

### Current source to extract from

```text
vendor/dxmt/src/d3d12/d3d12_root_signature.cpp/.hpp
vendor/dxmt/src/d3d12/d3d12_descriptor_heap.cpp/.hpp
vendor/dxmt/src/d3d12/d3d12_command_queue.cpp
```

### Done when

- Argument buffer construction can be driven by `libm12core` binding plans.
- D3DMetal oracle root/pipeline linkage can be imported into the same key space.
- AC6/Elden/Subnautica root mapping diffs become first-class diagnostics.

## Phase 6: Add oracle/prewarm ingestion

### Work

Teach `libm12core` to consume compact oracle/prewarm packs, not raw copied D3DMetal payloads:

- pipeline keys
- root signature structural keys
- shader bytecode hashes
- stage linkage
- expected resource layout summaries
- optional precompile queue order

### Inputs

Existing tools/artifacts:

```text
tools/d3d12-metal-sdk/scripts/decompile-d3dmetal-cache-map.py
tools/d3d12-metal-sdk/scripts/materialize-d3dmetal-oracle-pack.py
tools/d3d12-metal-sdk/scripts/link-d3dmetal-root-pipelines.py
tools/d3d12-metal-sdk/results/d3dmetal-root-pipeline-linkage-*/summary.md
```

### Done when

- AC6 can prewarm a known subset of high-value PSOs before gameplay.
- Prewarm is offline/profile-gated.
- No raw D3DMetal metallibs or cache payloads are committed.

## Phase 7: Move command replay planning, not necessarily command execution

### Work

Before moving all command execution, move the planning pieces:

- render pass planning
- resource usage classification
- redundant binding suppression
- command/replay counters
- PSO/binding validation before draw

### Current source to extract from

```text
vendor/dxmt/src/d3d12/d3d12_command_queue.cpp
vendor/dxmt/src/d3d12/d3d12_command_defs.hpp
vendor/dxmt/src/d3d12/d3d12_command_stats.hpp
```

### Done when

- Command replay can ask `libm12core` for a draw plan:

```text
PSO handle + root state + descriptor state + draw call
  -> binding plan / validation / resource usage list
```

- Existing D3D12 command queue still performs actual replay until fully migrated.

## Phase 8: Optional full command replay/presenter migration

### Work

Move full replay/present ownership only after previous phases are stable.

This is highest risk because it touches:

- command encoder lifetime
- render pass boundaries
- swapchain images
- synchronization
- resource hazard handling
- readback/diagnostic safety

### Current source to extract from

```text
vendor/dxmt/src/d3d12/d3d12_command_queue.cpp
vendor/dxmt/src/d3d12/d3d12_swapchain.cpp/.hpp
vendor/dxmt/src/dxmt/dxmt_presenter.cpp/.hpp
```

### Done when

- `libm12core` owns command replay and present path.
- PE side mostly records COM calls into stable command streams.
- Bounded AC6/Elden/Subnautica validation passes.

## Phase 9: Thin PE DLLs after core ownership is proven

Only after `libm12core` owns shader/PSO/binding/replay should PE DLLs be simplified.

Potential final shape:

```text
d3d12.dll      COM object wrappers + command serialization + ABI bridge
dxgi.dll       bootstrap only
dxgi_dxmt.dll  DXGI COM wrappers + ABI bridge
winemetal.dll  thunk transport
winemetal.so   native loader + Wine unixlib integration
libm12core     actual renderer/runtime core
```

## Validation matrix

Each phase should pass:

### Build/preflight

```text
tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime
tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime
tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight
```

### Rust launcher if touched

```text
cd app/src-rust
cargo fmt --all --check
cargo clippy --all-targets -- -D warnings
cargo test
```

### Runtime smoke

- AC6 bounded smoke, max 150 seconds when explicitly approved.
- Elden Ring bounded smoke after AC6 stability.
- Subnautica 2 bounded smoke only after cache-cold/compiler instability is isolated.

### Required evidence

- strict staged runtime hashes
- bounded run summary
- perf-analysis summary
- no raw D3DMetal cache/metallib payloads committed
- compact phase artifact under `tools/d3d12-metal-sdk/results/.../summary.md`

## Risk register

### ABI/lifetime risk

PE MinGW C++ objects and macOS native C++ objects must not cross the boundary. Use handles only.

### Device identity risk

Metal objects are device-scoped. Cache keys must include device identity or be stored per-device.

### Reflection mismatch risk

DXIL MSL lowering and SM50 reflection do not expose identical metadata. Migration must preserve D3D12 binding semantics.

### Loader fallback risk

A bad `libm12core` loader must not brick the runtime. Keep fallback and `DXMT_M12CORE_REQUIRED=1` only for strict tests.

### Rebase/upstream risk

Do not move everything at once. Keep small extraction patches and compatibility wrappers.

### Performance illusion risk

A unified core improves ownership and observability, but does not automatically reduce AC6 PSO count. Prewarm/key normalization still need measured proof.

## First practical implementation slice

The first low-risk code slice should be:

1. [done] Add inert `libm12core.dylib` with version ABI.
2. [done] Stage it beside `winemetal.so`.
3. [done] Load it from `winemetal_unix.c` behind `DXMT_M12CORE_ENABLE=1`.
4. [done] Log loaded path/version.
5. [done] Add a compact bounded AC6 proof artifact.
6. [done] Commit as Phase 1.

Do **not** move PSO/shader code until the loader is proven stable.

## Success definition

The roadmap succeeds when M12 has:

- one native core owning hot renderer state and caches
- thin PE DLLs preserving Windows ABI compatibility
- stable loader/fallback behavior
- normalized PSO/shader/root/binding keys
- oracle/prewarm ingestion path
- bounded validation across AC6, Elden Ring, and Subnautica 2
- improved performance evidence, not just architectural cleanliness
