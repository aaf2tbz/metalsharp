# M12 Winemetal Backend Unification Roadmap

## Architecture decision

M12/D3D12 should extend DXMT's existing Winemetal backend rather than build a
second native renderer beside it.

Proven D3D10/11 shape:

```text
Game
  -> d3d10core.dll / d3d11.dll
  -> winemetal.dll
  -> winemetal.so
  -> macOS Metal
```

Target D3D12/M12 shape:

```text
Game
  -> custom dxmt d3d12.dll / dxgi.dll / dxgi_dxmt.dll
  -> winemetal.dll
  -> winemetal.so
  -> macOS Metal
```

Rejected split-brain shape:

```text
d3d12.dll
  -> winemetal.dll
  -> winemetal.so
  -> env-gated libm12core.dylib sidecar
  -> partial native Metal ownership outside winemetal.so
```

`winemetal.dll` remains the PE/Wine thunk layer. `winemetal.so` is the native
macOS Mach-O backend. `m12core` becomes internal deterministic M12 logic inside
or behind `winemetal.so`, not a separately required runtime authority.

## Phase 0: freeze the corrected shell

Preserve public loading and ABI:

- D3D12/DXGI PE exports stay in `d3d12.dll`, `dxgi.dll`, and `dxgi_dxmt.dll`.
- `WMTM12Core*` PE exports stay in `winemetal.dll`.
- Wine unix-call table shape stays compatible.
- D3D10/11 Winemetal behavior must not regress.

Confidence gate:

- Source/ABI inventory documents the current shell.
- No game launch required.
- No untracked result files committed.

## Phase 1: use D3D10/11 as the backend oracle

Build and maintain a reference matrix from working D3D10/11 code to D3D12/M12
needs. D3D10 is largely layered over D3D11, and D3D11 already exercises the WMT
object model and native Winemetal backend.

Reference areas:

- device and WMT object handle ownership
- command queue and command buffer lifecycle
- async encoding and finish/completion threads
- shared event/fence-like sequencing
- resource initialization and staging allocators
- argument buffer allocation/encoding
- shader and pipeline caches
- render/compute PSO creation
- texture creation and texture views
- swapchain/present mechanics

Confidence gate:

- `tools/d3d12-metal-sdk/plans/m12-d3d11-winemetal-reference-map.md` exists and
  maps D3D11/WMT substrate to D3D12 responsibilities.

## Phase 2: internalize m12core into winemetal.so

Build `m12core.cpp` and `m12core_metal.c` into `winemetal.so` and bind
`WMTM12Core*` unix thunks to internal `m12core_*` functions by default.

Success criteria:

- `DXMT_M12CORE_ENABLE` is not required for M12 core functionality.
- `DXMT_M12CORE_PATH` is not required for M12 core functionality.
- `DXMT_M12CORE_REQUIRED` is not required for M12 core functionality.
- `otool -L winemetal.so` does not list `libm12core.dylib` as a dependency.
- The sidecar dylib may remain buildable for offline/dev probes, but game
  runtime must not require it.

## Phase 3: hard ownership boundaries

D3D12 frontend owns:

- COM/API objects
- command lists and allocators
- descriptor heaps and root signatures
- D3D12 resource state tracking and barrier interpretation
- D3D12 PSO descriptions
- D3D12 fence API
- DXGI API glue

`winemetal.dll` owns:

- PE exports
- Wine thunking
- `WINE_UNIX_CALL` routing

`winemetal.so` owns:

- `MTLDevice`, `MTLCommandQueue`, `MTLCommandBuffer`, and encoders
- `MTLBuffer`, `MTLTexture`, and texture views
- `MTLLibrary`, `MTLFunction`, render/compute pipeline states
- `MTLBinaryArchive`
- drawable/present execution
- native Metal object lifetime

Internal m12core logic owns:

- DXIL -> MSL lowering
- shader hashing and cache keys
- root binding summaries/plans
- packet classification and replay/present planning
- diagnostics/counters

Rule: m12core may plan; `winemetal.so` executes.

## Phase 4: shader path unification

Use D3D11 shader cache/function patterns as the backend reference while adding
D3D12-specific DXIL/SM6 handling.

Restore/regression coverage:

- explicit callee fixes
- inactive slot normalization
- ctz/semantic fixes
- SM5/DXIL reflection reports
- shader/metallib freshness checks

Offline gates:

- AC6, Elden Ring, Subnautica2, PEAK shader corpora
- generic DXIL corpus
- no game launch

## Phase 5: PSO creation/cache unification

Native PSO creation belongs to `winemetal.so` using D3D11 pipeline/cache code as
the model.

D3D12-specific additions:

- PSO stream parsing
- root signature linkage
- DXIL shader stages
- D3D12 input layout and RT/depth format mapping
- compute PSO mapping
- pipeline compatibility keys

Offline gates:

- `probe_compute_pso`
- `probe_graphics_pso`
- `offline-pso-factory`
- title PSO corpora

## Phase 6: binary archive single ownership

Binary archive lookup/population/serialization must be owned end-to-end by
`winemetal.so`.

Implement/verify:

- archive create/open
- archive lookup list
- descriptor `binaryArchives` assignment
- `FailOnBinaryArchiveMiss` behavior
- `addRenderPipelineFunctionsWithDescriptor`
- `addComputePipelineFunctionsWithDescriptor`
- `serializeToURL`
- atomic write/replace and mutexing
- exception handling
- empty/corrupt archive rejection
- compatibility keying

Offline progression only:

1. archive off
2. lookup bypassed
3. lookup only
4. single compute population
5. single render population
6. title corpus population
7. archive reload/lookup

## Phase 7: descriptor/root signature/argument buffer bridge

Map D3D12 binding semantics onto the WMT/argument-buffer substrate already used
by DXMT.

Offline probes:

- `probe_descriptors`
- `probe_descriptor_mutation_graphics`
- `probe_descriptor_table_indexing`
- `probe_root_descriptor_lifetime`
- `probe_argbuf_residency_graphics`
- `probe_argbuf_residency_compute`

## Phase 8: command queue/command buffer integration

Extend `dxmt::CommandQueue` and Winemetal command-buffer lifecycle for D3D12
command lists and queue semantics.

Offline probes:

- `probe_command_replay`
- `probe_command_conformance`
- `probe_execute_indirect_draw_replay`
- `probe_queues`
- `probe_m12_pso_submit_pressure`

## Phase 9: resource state/barrier/texture-view safety

Map D3D12 explicit resource states to DXMT's Metal resource model. The PEAK
`Depth32Float -> R32Float` crash is a native texture-view safety issue and must
be handled before Metal throws.

Offline probes:

- `probe_resource_views_formats`
- `probe_resources`
- `probe_heap_aliasing`
- `probe_barriers_render_pass`
- `probe_barriers_uav_aliasing_subresource`

## Phase 10: fence/completion/synchronization semantics

D3D12 fences must align with WMT command-buffer completion and shared-event
sequencing. This is the Subnautica2 stall track.

Offline gates:

- `probe_queues`
- `probe_command_conformance`
- `probe_present_render_queue_sync`
- queue/fence stress probes

## Phase 11: DXGI/swapchain/present

D3D12 present should use the proven Winemetal presenter/drawable mechanics while
respecting D3D12 command-queue-backed swapchains and backbuffer states.

Offline probes:

- `probe_dxgi_factory`
- `probe_dxgi_ue_bootstrap_sequence`
- `probe_present_windowed`
- `probe_present_lifetime_resize`
- `probe_present_render_queue_sync`
- `probe_standalone_dxgi_bootstrap`

## Phase 12: ABI/runtime cohort gates

Runtime cohort must be coherent:

```text
d3d12.dll
dxgi.dll
dxgi_dxmt.dll
d3d11.dll
d3d10core.dll
winemetal.dll
winemetal.so
```

`libm12core.dylib` may remain for offline/dev tooling but must not be required
for game runtime.

Gates:

- PE export parity
- `WMTM12Core*` export parity
- unix-call index parity
- `otool -L`, `nm`, and `strings` checks
- hash parity
- runtime preflight
- Winemetal ABI preflight
- dry-run env hygiene

## Phase 13: offline full corpus matrix

No game launches until this phase passes.

Matrix:

- shader lowering/reflection/MSL compile/metallib load
- compute and graphics PSOs
- binary archive population/reload
- root signatures/descriptors/argument buffers
- resource views/barriers
- command replay/queues/fences
- present/windowed/DXGI bootstrap

Title corpora:

- AC6
- Elden Ring
- Subnautica2
- PEAK
- Schedule I later

## Phase 14: classifier updates

Strong M12 evidence:

```text
d3d12.dll / dxgi_dxmt.dll
  -> winemetal.dll
  -> winemetal.so
  -> internal m12core DXIL->MSL / PSO / Metal path
```

Weak/ambient evidence:

- bare MoltenVK
- `VkInstance`
- Wine Vulkan bootstrap

Legacy/split-brain evidence:

- required runtime `dlopen(libm12core.dylib)`
- required `DXMT_M12CORE_ENABLE`
- sidecar-only m12core path

## Phase 15: final live validation only

Only after offline gates pass.

Order:

1. AC6
2. Subnautica2
3. PEAK
4. Elden Ring, avoiding character creator
5. Schedule I last
