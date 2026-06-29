# M12 Native Vertex/Tessellation GPU Roadmap

Status: planning roadmap for PR #230 follow-up work  
Branch/worktree: `feat/m12-fresh-proof-game-harness` at `/Volumes/AverySSD/MetalSharp-SM6-UE-Lab/10-worktrees/metalsharp-m12-fresh-proof-pr`  
Primary post-crash evidence root: `/tmp/pr230-elden-postcrash-persistent-evidence-20260629-002331`

## Goal

Build a native D3D12 vertex/index/tessellation path for M12 that renders on the GPU through DXMT/Metal without accepting a fallback rendering path as success. Existing fallback logs are treated as defects to remove or convert into explicit unsupported/blocked states until native GPU support exists.

Proposed project name for the new path: **M12 Native Vertex/Tessellation Path** (`M12-NVTP`).

Working module names:

- `d3d12_native_vertex_path.*` for IA/VB/IB/draw-state resolution, validation, and Metal binding.
- `d3d12_native_tessellation_path.*` for HS/DS patch topology, tessellation-factor generation, and Metal patch draw encoding.
- `m12_native_vertex_tessellation` for logs, probes, counters, and proof gates.

This is not a plan to broaden `MSLLowering`. Metal execution still ultimately needs Metal functions/metallibs, but this roadmap separates D3D12 pre-raster/IA/tessellation correctness from generic shader-text cleanup. The goal is a native GPU path with D3D12-specific semantics, not a VS/PS-only fallback, CPU emulation lane, or silent draw skip.

## Non-negotiables

- No commercial Elden Ring relaunch until the mini-game/probe suite proves the targeted draw classes without `vertex_range_oob`, tessellation fallback warnings, compute encode failure, or command-buffer/resource-lifetime faults.
- Existing `D3D12 tessellation fallback` behavior is a failure signal, not an acceptable plan outcome.
- Existing `M12 skipping unsafe DrawIndexedInstanced reason=vertex_range_oob` is a failure signal, not an acceptable visual workaround.
- If native support is incomplete, the runtime should fail closed with a precise diagnostic and proof artifact instead of trying to render through a degraded fallback path.
- All new validation must use the PR runtime/backend authority and `m12_fresh_game` / local probes before any live game launch.

## Evidence from the failed Elden run

Evidence files:

- Final launch log copy: `/tmp/pr230-elden-postcrash-persistent-evidence-20260629-002331/logs/launch-1782713377.log`
- Final run inventory: `/tmp/pr230-elden-postcrash-persistent-evidence-20260629-002331/analysis/final-run-error-inventory.txt`
- Shader cache summary: `/tmp/pr230-elden-postcrash-persistent-evidence-20260629-002331/shader-cache/shader-cache-summary.json`
- System evidence rollup: `/tmp/pr230-elden-postcrash-persistent-evidence-20260629-002331/EVIDENCE_ROLLUP.json`

Observed user-facing symptoms:

- Elden progressed farther than before: loading icon appeared and the user got past character creation.
- The 3D character model still failed to initialize/render correctly; it appeared missing/red/solid-color.
- Runtime slowed progressively.
- The machine eventually crashed/rebooted.
- Audio glitches were observed by the user during the slowdown, but final launch logs had no direct audio error matches, so audio is treated as a system-stall symptom until proven otherwise.

Active final-run runtime issues:

| Issue | Evidence | What it indicates |
|---|---:|---|
| Tessellation path is not native | `238` warnings shaped as `D3D12 tessellation fallback: compiling VS/PS-only render PSO ... topology=4` | HS/DS shaders and patch topology are reaching M12, but the current path compiles a VS/PS-only render PSO. Character/body geometry can plausibly fail if patch/tessellation geometry is discarded or approximated incorrectly. |
| Indexed draws skipped for vertex OOB | `256` warnings shaped as `M12 skipping unsafe DrawIndexedInstanced reason=vertex_range_oob` | Current IA/VB/IB range resolution believes draw parameters would read beyond bound vertex buffers. This directly matches missing geometry. |
| Compute encode failure | `1` line: `M12 compute encoder encode failed label=Dispatch pso=0x5312eaa0 dispatch=1x1x16` | Likely invalid resource/argument binding, poisoned encoder state, incomplete compute root/descriptor binding, or command-buffer resource lifetime/residency issue. |
| Present path alive | `M12 present ... classification=drawn`, final present count around `960` | This was not a pure no-present failure; the renderer was alive while geometry/command state degraded. |
| Shader compile path clean in final epoch | `shader_compile_fail=0`, `pso_compile_failed=0`, `CreateGraphicsPipelineState_failed=0`, `post_final_launch_msl_err_count=0` | Latest crash should be investigated as runtime draw/resource/command correctness, not just shader syntax compilation. |
| Historical/stale MSL sidecars still present | `total_msl_err_count=15`, `post_final_launch_msl_err_count=0` | Prior shader bugs were real and fixed/replaced by newer `.msl`; sidecars must stay tracked as historical hazards but not confused with active final-run errors. |
| WindowServer/system stall and reboot | WindowServer ping timeouts before reboot; `kern.boottime Mon Jun 29 00:15:02`; watchdog/panic-flow boot messages | The run destabilized the graphics/session environment. No direct preserved AGX panic backtrace was found, so do not overclaim a proven GPU panic, but the watchdog/reboot evidence is serious. |

Representative `vertex_range_oob` line:

```text
warn:  M12 skipping unsafe DrawIndexedInstanced reason=vertex_range_oob pso=0x635227d0 vs=0a9d2ab3d81f9b7d ps=cfee6b49d62139d5 ... gpu=0x10063560000 size=10944 stride=12 required=6708 available=912 elems=1716 inst=1 start=4992 base=0 start_inst=0 indexed=1
```

Representative tessellation warning:

```text
warn:  D3D12 tessellation fallback: compiling VS/PS-only render PSO HS bytes=12796 DS bytes=24286 topology=4
```

Historical MSL sidecar classes found in the Elden shader cache:

- `9x` `float2` constructor errors, e.g. `no matching constructor for initialization of 'float2'`.
- `5x` `int4` constructor over-arity errors, e.g. `expected at most 4, have 8/9/18`.
- `1x` bool vector/scalar mismatch, e.g. `assigning to 'bool' from incompatible type 'bool4'`.

Key affected historical stems include `1a45b731c0e4fe83`, `541f079ea76591c4`, `664f77848ee358b1`, `6c4971b857643392`, `90f3e6ee48cc6ba0`, `ce00e60cb04556b9`, `d11ba18f4a2db366`, `da999dae38812a81`, `e802a4479e7393fe`, `672ea3ead7e49d6c`, `bacc06724ae0df15`, `bcb47c5245ffbb98`, `ce0bfbedf57c229d`, `cffde66df3b3c364`, and `bcfd3010eba1f51d`.

## Source-backed technical facts used by this roadmap

Direct3D 12 IA/draw semantics:

- `IASetVertexBuffers(StartSlot, NumViews, pViews)` binds vertex-buffer views starting at a zero-based slot. Source: Microsoft Learn, `ID3D12GraphicsCommandList::IASetVertexBuffers`.
- `D3D12_VERTEX_BUFFER_VIEW` consists of `BufferLocation`, `SizeInBytes`, and `StrideInBytes`; stride is the size of each vertex entry. Source: Microsoft Learn, `D3D12_VERTEX_BUFFER_VIEW`.
- `D3D12_INDEX_BUFFER_VIEW` consists of `BufferLocation`, `SizeInBytes`, and `Format`. Source: Microsoft Learn, `D3D12_INDEX_BUFFER_VIEW`.
- `DrawIndexedInstanced` reads `IndexCountPerInstance` indices beginning at `StartIndexLocation`, adds signed `BaseVertexLocation` to each fetched index before reading vertex buffers, and uses `StartInstanceLocation` for per-instance data. Source: Microsoft Learn, `ID3D12GraphicsCommandList::DrawIndexedInstanced`.
- D3D12 places resource-state responsibility on the application/runtime. Microsoft’s barrier docs explicitly describe D3D12 moving per-resource state management from the driver to the application to reduce CPU overhead and enable multithreading. Source: Microsoft Learn, `Using Resource Barriers to Synchronize Resource States in Direct3D 12`.
- DRED auto-breadcrumbs are inserted after render ops such as draw, dispatch, copy, and resolve to diagnose GPU faults/device removal. Source: Microsoft Learn, `Use DRED to diagnose GPU faults`.

Metal semantics and hazards:

- Metal render encoders expose explicit `setVertexBuffer:offset:atIndex:` APIs and indexed draw APIs with `indexBufferOffset`, `baseVertex`, and `baseInstance`. Source: local Apple SDK header `MTLRenderCommandEncoder.h` lines around `150-168` and `723-768`.
- Metal `useResource`/`useResources` calls do **not** retain resources; the caller remains responsible for retaining resources until GPU access completes. Source: local Apple SDK header `MTLRenderCommandEncoder.h` lines around `946-983`.
- Metal heap/argument-buffer resource usage does not protect against all data hazards; hazards must be addressed with `MTLFence` where required. Source: local Apple SDK header `MTLRenderCommandEncoder.h` lines around `988-1009` and `MTLComputeCommandEncoder.h` lines around `282-290`.
- Metal has command-buffer error categories including timeout, page fault, invalid resource, out of memory, and stack overflow. Source: local Apple SDK header `MTLCommandBuffer.h` lines around `73-120`.
- `CAMetalLayer nextDrawable` may wait up to one second when drawables are exhausted, and `allowsNextDrawableTimeout = NO` can block forever. Source: local Apple SDK header `CAMetalLayer.h` lines around `99-152`.
- Metal tessellation uses patch/tessellation pipeline properties and tessellation-factor buffers, not a direct D3D HS/DS pass-through. Source: Apple Metal Programming Guide tessellation chapter and local Apple SDK `MTLRenderPipeline.h` lines around `206-212`.

Local code facts:

- Existing safety code is in `vendor/dxmt/src/d3d12/d3d12_vertex_input.hpp` and `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp`.
- Existing IA command recording is in `vendor/dxmt/src/d3d12/d3d12_command_list.cpp` and `vendor/dxmt/src/d3d12/d3d12_command_defs.hpp`.
- Existing tessellation behavior is explicitly named fallback in `vendor/dxmt/src/d3d12/d3d12_pipeline_state.cpp`, where `!m_hs.empty() || !m_ds.empty()` logs `D3D12 tessellation fallback: compiling VS/PS-only render PSO` and marks `m_uses_tessellation_fallback`.
- Existing draw replay applies root bindings, argument buffers, vertex buffers, safety validation, and draw encoding in `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp`.
- Existing compute encode failure originates around `ReplayComputeDispatch` in `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp`, where `comp.encodeCommands(chain_head)` failure logs `M12 compute encoder encode failed`.
- Existing Metal bridge applies `useResource`, `waitForFence`, and `updateFence` in `vendor/dxmt/src/winemetal/unix/winemetal_unix.c`; wrapper methods exist in `vendor/dxmt/src/winemetal/Metal.hpp`.
- `tools/d3d12-metal-sdk/probes/m12_fresh_game/m12_fresh_game.cpp` already has indexed draw, tessellation-shaped, execute-indirect, barrier, descriptor, UAV, and readback proof lanes. It should become the first proof vehicle for native vertex/tessellation correctness.

## Roadmap phases

### Phase 0 — Freeze evidence and define hard fail gates

Deliverables:

- Add a short checklist artifact to each future M12 proof run that reports:
  - `vertex_range_oob=0`
  - `D3D12 tessellation fallback=0`
  - `M12 compute encoder encode failed=0`
  - `post_launch_msl_err_count=0`
  - `MTLCommandBufferErrorDomain=0`
  - `WindowServer timeout/new reboot evidence=0`
- Teach proof scripts to fail if any of the above are nonzero.
- Preserve final crash evidence as the baseline failure snapshot.

Why first:

The last run proved shader syntax cleanliness but still crashed. Future progress must be measured by runtime correctness, not only by shader compilation.

Acceptance:

- A mini-game/probe run can fail fast with a single summary saying exactly which hard gate failed.
- No hard gate treats fallback rendering as a pass.

### Phase 1 — M12-NVTP IA/Draw truth table and mini-game repros

Purpose:

Create hard local proof cases that target every suspected cause of `vertex_range_oob` before changing the live game path.

Mini-game/probe additions:

- Indexed draw with nonzero `StartIndexLocation` and valid `BaseVertexLocation`.
- Indexed draw with negative and positive `BaseVertexLocation` where the final vertex IDs are still in range.
- Invalid indexed draw where IB range is OOB; expected result is precise diagnostic, not crash.
- Invalid indexed draw where VB range is OOB; expected result is precise diagnostic, not crash.
- Multi-slot vertex input with different strides and nonzero `AlignedByteOffset`.
- Per-instance input with `InstanceDataStepRate` and nonzero `StartInstanceLocation`.
- Dynamic `IASetVertexBuffers` stride changes across draws using the same PSO.
- Root descriptor/table mutation between draws to catch stale command/resource state.
- ExecuteIndirect DrawIndexed path with nonzero start/base values.
- Patch-list topology with HS/DS bytecode present; expected current result is blocked until native tessellation exists, not VS/PS-only fallback.

Validation data to print/read back:

- Per draw: slot, VB GPUVA, VB resource base, view offset, `SizeInBytes`, `StrideInBytes`, input-element offset, input slot class, instance step rate.
- Per indexed draw: IB GPUVA, IB resource base, IB view offset, IB format, `StartIndexLocation`, index size, sampled min/max index, `BaseVertexLocation`, computed min/max vertex ID.
- Render/readback proof that the intended geometry appears.

Acceptance:

- The new mini-game cases pass with visible/readback correctness.
- Invalid cases produce deterministic diagnostics and do not poison subsequent encoders.
- No test case relies on tessellation or vertex fallback.

### Phase 2 — Implement `d3d12_native_vertex_path`: exact IA/VB/IB state resolution

Purpose:

Replace ad-hoc draw safety and scattered vertex binding decisions with one D3D12-specific native vertex resolver.

Implementation shape:

- Create `D3D12NativeVertexPathState` containing:
  - current `D3D12_VERTEX_BUFFER_VIEW[32]`
  - current `D3D12_INDEX_BUFFER_VIEW`
  - current primitive topology
  - current PSO IA layout metadata
  - root signature epoch / descriptor heap epoch / command-list epoch
  - current draw arguments
- Create `ResolveNativeIndexedDraw()` that returns:
  - resolved IB resource and byte offset
  - index type
  - min/max sampled index when readable
  - final min/max vertex ID after `BaseVertexLocation`
  - resolved per-slot VB resource, offset, length, stride
  - per-input required byte ranges
- Compute OOB using D3D12 view sizes, not whole-resource sizes, and include input element offsets in byte-range checks.
- Validate `StartIndexLocation * indexSize + IndexCountPerInstance * indexSize <= IBV.SizeInBytes`.
- Validate `fetchedIndex + BaseVertexLocation` for signed underflow and max vertex range.
- Validate per-instance slots using `StartInstanceLocation`, `InstanceCount`, and `InstanceDataStepRate`.
- Validate resource lookup by GPUVA and detect when a VB/IB view points into the wrong resource/suballocation.

Specific failure suspects targeted:

- Wrong vertex buffer binding: resolved slot/resource table must match D3D12 input layout slots.
- Wrong stride/offset: use `D3D12_VERTEX_BUFFER_VIEW.StrideInBytes` plus input `AlignedByteOffset` for each attribute.
- Wrong index format: accept only valid IA index formats (`R16_UINT`, `R32_UINT`) and map to Metal index type exactly.
- Bad `BaseVertexLocation`: signed base is applied to every fetched index before vertex range validation.
- Bad `StartIndexLocation`: contributes to IB byte offset only once, not twice through shader helper state.
- Stale command/resource state: resolver logs command-list and binding epochs so stale root/IA state can be detected.
- Descriptor/root-table corruption: resolver snapshots descriptor heap/root table state before draw and compares it to shader binding completeness.
- Resource lifetime/fence bugs: resolved resources are retained/tracked until command-buffer completion.

Acceptance:

- `vertex_range_oob` is either eliminated for valid draws or reproduced locally with exact cause.
- Normal draw replay and ExecuteIndirect draw replay use the same resolver.
- Existing log message `M12 skipping unsafe DrawIndexedInstanced reason=vertex_range_oob` is replaced by richer `M12 native vertex path blocked` diagnostics only for intentionally invalid test cases.
- No visual proof accepts skipped character/model draws.

### Phase 3 — Native Metal binding for resolved vertex/index draws

Purpose:

Bind resolved D3D12 IA state to Metal deterministically without compact-slot races or stale pipeline layout mismatches.

Implementation shape:

- Prefer a raw D3D12 slot-indexed vertex table (`slot == D3D12 InputSlot`) for M12-NVTP instead of compact-by-slot-mask behavior when correctness is under test.
- If using Metal stage-in vertex descriptors, pipeline-cache keys must include every Metal layout property that can change with D3D12 IA state, especially stride.
- If using shader-side vertex pulling, bind a canonical raw-slot table and draw-argument block; the shader reads by D3D12 slot, not by a compacted runtime table index.
- Ensure Metal `drawIndexedPrimitives(... indexBufferOffset ... baseVertex ... baseInstance ...)` receives:
  - byte offset derived from IBV resource offset + `StartIndexLocation * indexSize`
  - `baseVertex` from D3D12 `BaseVertexLocation`
  - `baseInstance` from D3D12 `StartInstanceLocation`
- Every resolved MTLBuffer/MTLTexture used through direct bindings or argument buffers must be declared with `useResource`/`useResources` and retained by a command-buffer lifetime list.

Acceptance:

- Multi-slot/dynamic-stride mini-game draws are correct across repeated PSO reuse.
- Nonzero start index/base vertex/start instance tests pass readback.
- Logs prove raw slot mapping, resource handles, offsets, and draw arguments.
- No compact-slot fallback or VS/PS workaround is required for passing proofs.

### Phase 4 — Implement `d3d12_native_tessellation_path`: true GPU patch rendering

Purpose:

Remove the current VS/PS-only tessellation behavior for HS/DS PSOs by adding a native GPU patch path, or block unsupported tessellation explicitly until implemented.

What native tessellation must model:

- D3D12 patch-list topology and control-point count.
- Hull shader control-point output.
- Hull shader patch-constant output / tessellation factors.
- Domain shader evaluation.
- Partitioning, winding/order, domain type, factor format, and max tessellation factor mapped to Metal pipeline/tessellation properties.
- Metal tessellation-factor buffer creation and binding.
- Patch draw encoding with factor-buffer offsets and control-point/index behavior.

Implementation options to evaluate, in order:

- GPU prepass compute kernel that runs D3D12 HS/patch-constant logic into a Metal-compatible tessellation-factor/control-point buffer, then a Metal post-tessellation vertex/domain function consumes generated coordinates/control points.
- Direct Metal tessellation pipeline construction when the translated HS/DS metadata can map cleanly to Metal patch/tessellation descriptors.
- Explicit unsupported error for shapes that cannot yet map, with shader hashes and topology in the failure artifact.

Not acceptable:

- VS/PS-only rendering when HS/DS are present.
- CPU tessellation as a correctness path.
- Silent draw downgrade.

Mini-game/probe additions:

- Minimal quad/triangle patch HS/DS scene with visible/readback output.
- Patch-list indexed and non-indexed variants.
- Tessellation factor readback/debug buffer.
- Per-patch and per-control-point data validation.
- Negative test: unsupported patch shape fails closed with a named `native_tessellation_unsupported` diagnostic and no draw encode.

Acceptance:

- Final proof logs contain `D3D12 tessellation fallback=0`.
- Tessellated mini-game draw is visibly/readback correct on GPU.
- Elden-like HS/DS PSOs no longer compile as VS/PS-only render PSOs.

### Phase 5 — Compute encoder encode failure hardening

Purpose:

The final run had one `M12 compute encoder encode failed` after thousands of PSO/draw operations. This must become actionable and locally reproducible before live launch.

Plan:

- Build a compute dispatch resolver parallel to the native vertex resolver:
  - compute root signature snapshot
  - root CBV/SRV/UAV descriptors
  - descriptor table epoch
  - argument-buffer layout and required slots
  - bound resources and usage declarations
  - dispatch dimensions
- Convert `comp.encodeCommands(chain_head)` failure into a structured failure artifact:
  - PSO pointer/hash
  - CS hash
  - argument table qwords
  - missing buffers/textures/samplers
  - resource handles and GPUVAs
  - command-buffer status/error when available
- Add minigame cases that intentionally mutate descriptor tables/root descriptors across dispatches and verify no stale binding survives.
- Ensure compute pass creation closes any active render encoder in a known-good state and does not continue with a poisoned command buffer.

Acceptance:

- `M12 compute encoder encode failed=0` in mini-game proof.
- Intentional invalid compute descriptors fail closed with exact missing/invalid binding diagnostics.
- Compute dispatch after heavy graphics PSO/draw churn remains valid.

### Phase 6 — Command/resource lifetime and backpressure control

Purpose:

Prevent progressive slowdown and system instability by making command-buffer/resource lifetime explicit and bounded.

Plan:

- Add per-command-buffer retained-resource lists for every MTL object referenced directly or through an argument buffer:
  - buffers
  - textures
  - samplers
  - heaps
  - pipelines
  - argument buffers
  - draw/dispatch transient buffers
  - drawables
- Release retained resources only in command-buffer completion handlers.
- Enable command-buffer error options where available and record `MTLCommandBufferErrorDomain`, error code, encoder labels, and M12 breadcrumbs.
- Mirror DRED-style breadcrumbs in M12:
  - before/after draw
  - before/after dispatch
  - before/after copy/barrier
  - PSO/shader hashes
  - IA/descriptor summary
- Bound in-flight frames/command buffers and drawable acquisition:
  - cap queue depth
  - handle nil drawables
  - never block indefinitely on `nextDrawable`
  - avoid WindowServer/main-thread backpressure
- Audit `MakeTransientBuffer` use so transient data cannot be recycled before GPU completion.

Acceptance:

- Long mini-game stress run shows stable frame time and bounded in-flight command buffers.
- No command-buffer timeout/page-fault/invalid-resource errors.
- No new WindowServer timeout evidence during bounded tests.
- Resource lifetime evidence shows referenced objects retained until completion.

### Phase 7 — Shader translation guardrail remains, but is not the runtime root goal

Purpose:

Keep the shader syntax fixes from regressing while focusing the remaining roadmap on runtime geometry/command correctness.

Plan:

- Keep exact Elden DXIL/MSL replay gates.
- Keep post-launch `.msl.err.txt` freshness checks.
- Stale `.msl.err.txt` sidecars must be classified by mtime and paired `.msl` freshness before they are counted as active failures.
- Do not add more generic `MSLLowering` patches to paper over IA/tessellation/resource bugs.

Acceptance:

- Full cache replay remains clean.
- Mini-game proof reports `post_launch_msl_err_count=0`.
- Historical sidecars are reported separately from active launch failures.

### Phase 8 — Launch safety and proof gates before Elden

Required green gates before another Elden launch:

- `m12_fresh_game` native vertex proof passes:
  - nonzero start index
  - positive/negative base vertex
  - multi-slot dynamic stride
  - per-instance inputs
  - ExecuteIndirect indexed draw
  - invalid OOB negative tests fail closed
- `m12_fresh_game` native tessellation proof passes or blocks unsupported tessellation with no fallback draw.
- Compute encode stress proof passes.
- Command/resource lifetime stress proof passes with bounded in-flight command buffers.
- Full Elden shader cache replay remains clean.
- `/mtsp/prepare` proves game-local DLL hashes match the PR runtime.
- Runtime launch monitor has hard abort triggers for:
  - any `vertex_range_oob`
  - any tessellation fallback warning
  - any compute encode failure
  - any Metal command-buffer error
  - WindowServer/AGX/watchdog warning escalation
  - progressive frame/present stall beyond a bounded threshold

Acceptance:

- No live launch proceeds without explicit user approval after all gates pass.
- Any failure produces a persistent evidence directory and stops before system instability can escalate.

## Validation matrix

| Failure class | Local proof | Runtime gate | Launch gate |
|---|---|---|---|
| VB binding/stride/offset | Multi-slot dynamic-stride mini-game | raw-slot IA resolver logs | `vertex_range_oob=0` |
| IB format/start index | R16/R32 indexed mini-game | IB offset/index type logs | no index OOB |
| BaseVertex/StartInstance | positive/negative base and per-instance tests | min/max vertex ID logs | no missing model draws |
| Stale command/root/descriptor state | descriptor mutation tests | root/descriptor epoch logs | binding completeness OK |
| Native tessellation | patch HS/DS mini-game | `native_tessellation_path` logs | `D3D12 tessellation fallback=0` |
| Compute encode | descriptor/dispatch stress | compute resolver artifact | `compute encoder encode failed=0` |
| Resource lifetime | transient buffer/readback stress | retained-resource completion logs | no invalid-resource/page-fault/timeout |
| Backpressure | long bounded present stress | in-flight/drawable metrics | no WindowServer timeouts/reboot |
| Shader syntax | exact DXIL/MSL replay | `.msl.err.txt` freshness audit | post-launch active MSL errors `0` |

## Immediate implementation order

- Add the mini-game/probe cases first, especially indexed/base-vertex/dynamic-stride/per-instance cases.
- Introduce `d3d12_native_vertex_path.*` and route both direct and ExecuteIndirect indexed draws through it.
- Replace current tessellation fallback with `native_tessellation_required` blocking diagnostics, then implement `d3d12_native_tessellation_path.*` behind the same proof gates.
- Add compute resolver/error artifacts.
- Add command-buffer retained-resource/error/breadcrumb infrastructure.
- Only after all local gates pass, prepare a bounded, monitored Elden launch request for explicit user approval.

## Open risks

- Metal tessellation is not a one-to-one D3D12 HS/DS mapping. The native path may require a GPU prepass to generate Metal-compatible factor/control-point buffers.
- D3D12 dynamic VB strides conflict with Metal pipeline-state vertex descriptor assumptions unless M12 uses vertex pulling or includes stride/layout in pipeline keys.
- Some index buffers may not be CPU-readable when sampling min/max indices; the resolver needs both a fast proof path and a non-mapping production path.
- Resource lifetime bugs may only appear under long PSO/draw/dispatch pressure; bounded stress tests must run longer than the current five-frame proof.
- The final crash did not preserve a direct AGX panic backtrace, so launch monitors must gather better GPU/WindowServer evidence if another bounded launch is ever approved.

## Definition of done for this roadmap

The roadmap is complete when it is committed to the PR and future implementation work can use it as a hard gate list. The runtime work itself is complete only when:

- Native vertex/index mini-game proofs pass with no draw skips.
- Native tessellation proof passes or unsupported tessellation blocks explicitly without rendering fallback.
- Compute encode proof passes.
- Command/resource lifetime stress proof passes.
- Elden prelaunch gates pass.
- The user explicitly approves any next live commercial launch.
