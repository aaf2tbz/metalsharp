# Research: D3D12 Resource-Hazard Semantics for the MetalSharp / DXMT-M12 Metal Backend

**Target failure:** Armored Core VI (AC6) runs through title/menu, but faults only after
**Continue** drives the engine into heavy real-world rendering/compute.

**Scope of this brief:** Official Microsoft DirectX 12 / Direct3D 12 documentation on
descriptor lifetime, descriptor-heap mutation after command-list recording, command-list/
queue/allocator execution lifetime, resource-state/barrier semantics (UAV / aliasing /
subresource transitions), `ExecuteIndirect` argument/count buffers, render-target/depth/UAV
clear descriptor semantics, copy-to-SRV hazards, and device-removal / page-fault GPU faults —
translated into exact Metal-backend implications and concrete fix requirements.

---

## Summary

The "works at menu, faults after Continue into gameplay" pattern is the textbook signature of
a **resource-lifetime or barrier/synchronization correctness bug in a translation layer**, not a
capability gap. Title/menu exercises a small, stable descriptor set, a trivial barrier graph,
static indirect draws, and few per-frame copies. Heavy gameplay exercises the full hazard
surface at once: GPU-driven rendering (`ExecuteIndirect` consuming GPU-written count buffers),
multiple compute passes joined by **UAV barriers**, streaming texture uploads (copy→SRV),
aggressive **per-frame descriptor-heap slot reuse**, and **async-compute + graphics queues**.
Microsoft's D3D12 docs put the entire burden of descriptor lifetime, allocator-reset timing,
barrier insertion, and indirect/count-buffer synchronization on the application (the runtime
performs **no validation** of these), so a Metal backend that drops, defers, or mis-resolves any
of these gets silent corruption or a GPU fault — surfacing exactly when AC6 starts stressing the
paths. The dominant concrete fixes are: (1) retain descriptor→Metal-resource bindings until
fence completion, (2) snapshot descriptors at record time rather than late-binding them,
(3) tie Metal command-buffer storage lifetime to the D3D12 allocator, (4) map every
transition/UAV/aliasing barrier to a real Metal sync primitive and never coalesce/drop UAV
barriers, (5) honor GPU-written `ExecuteIndirect` count buffers without CPU readback, and
(6) resolve `Clear*` targets to concrete Metal resources at record time and translate Metal
command-buffer errors into `DXGI_ERROR_DEVICE_REMOVED`.

---

## Tooling note (read this first)

The web-search backend in this session was degraded: `general`/`news`/`it`/`science` categories
could not surface Microsoft Learn content at all (returning Docker Hub, MDN, or unrelated
results), and the local `read` tool cannot fetch remote URLs. The `images` category **did**
surface the real Microsoft Learn and DirectX-Specs URLs. All URLs below were **confirmed live**
via search result titles/URLs; they are marked **[confirmed]**. Semantic content is synthesized
from established, stable D3D12 specification knowledge (the D3D12 programming guide and API
reference have been stable since ~2015 with additive changes) and should be re-read directly at
the cited URLs before committing code. Where a canonical URL is inferred from the documented
URL structure but not surfaced by search, it is marked **[canonical]**.

---

## Findings

### 1. Descriptor lifetime — descriptors referenced by in-flight GPU work must be immutable

The **Shader Visible Descriptor Heaps** doc is explicit that the contents of a shader-visible
descriptor heap are read by the GPU **at execution time**, and the application must keep the
referenced descriptor slots stable/valid for the entire window the GPU may read them. The
runtime does not validate this. [Shader Visible Descriptor Heaps [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/shader-visible-descriptor-heaps)

1. **GPU reads the heap late, at execution time — not at record time.** A draw that binds a
   descriptor table at offset *X* does not snapshot the descriptor at offset *X*; the GPU reads
   whatever is at offset *X* when the command list executes. Therefore any descriptor slot that
   is referenced by a *submitted* (in-flight) command list must not be overwritten until the GPU
   is done — verified via fence signal. [Source [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/shader-visible-descriptor-heaps)

2. **Non-shader-visible heaps are staging areas; descriptors are copied CPU-side into the
   shader-visible heap** via `ID3D12Device::CopyDescriptors` / `CopyDescriptorsSimple`. The copy
   is a CPU operation performed before `ExecuteCommandLists`. [Descriptor Heaps overview [canonical]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/descriptor-heaps) — also discussed in
   [Diligent: Managing Descriptor Heaps [confirmed]](http://diligentgraphics.com/diligent-engine/architecture/d3d12/managing-descriptor-heaps/)
   and [GameDev: Efficient D3D12 Descriptor Heap Management [confirmed]](https://www.gamedeveloper.com/programming/efficient-d3d12-descriptor-heap-management-system).

3. **The bound heap must stay bound for the recording+execution window.** `SetDescriptorHeaps`
   associates CBV_SRV_UAV/Sampler heaps with the command list; swapping/unbinding a heap that a
   recorded command references is invalid. [Shader Visible Descriptor Heaps [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/shader-visible-descriptor-heaps)

**Metal implication.** Metal has no descriptor heaps; resources are bound as `MTLTexture`/
`MTLBuffer` arguments (directly or via **argument buffers**). DXMT must retain the underlying
Metal resource that backs each descriptor for the lifetime of every command buffer that
references it. If the layer frees or recycles the Metal texture/buffer backing a live descriptor,
you get a Metal validation error or a real GPU page-fault equivalent.

**Concrete fix.** Keep a strong reference (retain) from each in-flight command buffer to the
Metal resources that its descriptors resolve to; defer the release of those resources to the
fence-signal callback for the queue that executed the work. Do not pool/recycle Metal resource
backings by D3D12 frame index alone. The non-shader-visible→shader-visible copy path must be a
**deferred materialization into stable slots**, not an immediate overwrite of a slot the GPU is
still reading.

---

### 2. Descriptor-heap mutation after command-list recording — late-binding is the hazard

Because the GPU reads the heap at execution time (Finding 1), mutating a descriptor slot **after
the command list is recorded but before/while it executes** changes what the GPU reads. This is
legal in D3D12 *only* if the slot is not referenced by in-flight work; the app owns the
discipline. [Shader Visible Descriptor Heaps [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/shader-visible-descriptor-heaps)

4. **Record-time snapshot vs. execution-time resolution is a backend choice.** D3D12 leaves it
   open: a driver may snapshot at record time or read the heap live. A translation layer that
   **late-binds** (re-resolves the D3D12 heap offset at `ExecuteCommandLists` time) will pick up
   mutations the app made *after* recording — which the app expects only for slots it has not yet
   submitted. If DXMT late-binds *all* slots, it silently changes bindings the GPU already
   "committed" to. [Porting from Direct3D 11 to Direct3D 12 [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/porting-from-direct3d-11-to-direct3d-12)
   emphasizes the app now owns all lifetime/sync discipline.

**Metal implication.** If DXMT resolves descriptors to concrete Metal resources at record time
(snapshot), subsequent heap mutation cannot affect in-flight work — this is the robust model. If
DXMT binds a pointer/offset that is re-read later (e.g., an argument buffer re-resolved at
execute time), then post-record mutation produces the wrong resource. Title/menu: descriptor
layout is stable, so the bug is latent. Heavy gameplay: AC6 reuses CBV_SRV_UAV heap slots every
frame, so any late-binding path binds a recycled/stale descriptor.

**Concrete fix.** **Snapshot descriptor→Metal-resource bindings at the point the draw/dispatch
is recorded** into the Metal command buffer — capture the actual `MTLTexture`/`MTLBuffer` and
offset/stride then, so later heap mutation does not retroactively change in-flight bindings. If
argument buffers are used, ring/fence per-frame heap regions so a region is never rewritten while
in flight. Verify with `POST /diagnostics/binding-contract/validate` (root-signature + reflection
ABI validation from the Phase 5 route) and `POST /diagnostics/command-replay/validate` (Phase 6).

---

### 3. Command-list / command-queue / command-allocator execution lifetime

5. **A command allocator must outlive all command lists recorded into it, and must not be Reset
   until the GPU has finished executing those command lists.** Resetting an in-flight allocator
   recycles its backing memory while the GPU may still be reading it → undefined behavior
   (typically a TDR or page fault). [Command Lists and Queues [canonical]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/command-lists-and-queues);
   see also [Porting from Direct3D 11 to Direct3D 12 [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/porting-from-direct3d-11-to-direct3d-12)
   and [Pipelines and Shaders with Direct3D 12 [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/pipelines-and-shaders-with-directx-12).

6. **A command list can be Reset and reused immediately after `Close`** (and after
   `ExecuteCommandLists`), independent of GPU completion — **but** it must be re-`Reset` with a
   *different*, GPU-completed allocator. The list is cheap; the allocator is the scarce, GPU-tied
   resource. [Command Lists and Queues [canonical]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/command-lists-and-queues)

7. **`ExecuteCommandLists` is asynchronous.** Multiple command lists can be submitted in one call;
   execution ordering within a queue is FIFO, but cross-queue hazards require explicit
   shared-fence synchronization the app must insert. [Command Lists and Queues [canonical]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/command-lists-and-queues);
   [Fence-Based Synchronization [canonical]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/user-mode-heap-synchronization) (fence sync topics).

**Metal implication.** DXMT maps D3D12 command lists/allocators to Metal command encoders /
command buffers. The classic, high-frequency bug: **resetting a D3D12 command allocator while its
Metal command buffer is still in-flight** recycles the backing storage → the GPU reads
freed/recycled memory → fault. A second bug: **cross-queue hazards** (async-compute + graphics
queues sharing a UAV/texture) with no Metal fence inserted between the two command buffers →
race. Title/menu submits few, long-lived command lists on a single queue; heavy gameplay submits
many short command lists per frame across multiple queues — exactly the workload that exposes
both bugs.

**Concrete fix.** Tie each Metal command buffer's storage lifetime to its D3D12 command
allocator: only release/reset Metal command-buffer backing after the **fence associated with the
submitting queue** signals completion. Implement real per-queue GPU-completion tracking (not a
2-frame ring keyed on frame index). For multi-queue, insert `MTLSharedEvent`/`MTLFence` waits
between command buffers derived from D3D12 shared-fence signals/waits. Route this through the
Phase 6 `POST /diagnostics/command-replay/validate` and the `barriers-render-pass-only` probe.

---

### 4. Resource states / barriers — UAV, aliasing, and subresource transitions

This is the highest-value area for the AC6 failure. The canonical reference is
[Using Resource Barriers to Synchronize Resource States in Direct3D 12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12)
([GitHub source confirmed](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12.md))
plus the authoritative [Enhanced Barriers spec [confirmed]](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html)
and the [DirectX blog: D3D12 Enhanced Barriers Preview [confirmed]](https://devblogs.microsoft.com/directx/d3d12-enhanced-barriers-preview/).

8. **Transition barrier** (`D3D12_RESOURCE_BARRIER_TYPE_TRANSITION`): changes a (sub)resource from
   `StateBefore` to `StateAfter`, inserting a combined execution+memory barrier. Buffers and
   **simultaneous-access** textures implicitly **promote** from COMMON to read states and **decay**
   back to COMMON at `ExecuteCommandLists` boundaries; ordinary textures generally require an
   explicit transition. [D3D12_RESOURCE_STATES enum [canonical]](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_states);
   [Subresources [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/subresources).

9. **UAV barrier** (`D3D12_RESOURCE_BARRIER_TYPE_UAV`): orders all prior unordered-access
   reads/writes of a resource to complete before any subsequent UAV read/write of the **same**
   resource. **No state change.** This is the critical barrier for compute→compute and
   compute→graphics data dependencies. Omitting it between two passes that touch the same UAV is
   undefined ordering → race. [Enhanced Barriers spec [confirmed]](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html);
   a real-world omission is documented in [DirectX-Graphics-Samples issue #789 [confirmed]](https://github.com/microsoft/DirectX-Graphics-Samples/issues/789).

10. **Aliasing barrier** (`D3D12_RESOURCE_BARRIER_TYPE_ALIASING`): used between **placed** /
    reserved resources that overlap in a heap. Both resources should be in COMMON (or
    simultaneous-access); the barrier flushes/invalidates caches for the physical memory they
    share, and the contents of the previously-aliased resource become **undefined** afterwards.
    [AMD D3D12MemoryAllocator: Resource Aliasing (overlap) [confirmed]](https://gpuopen-librariesandsdks.github.io/D3D12MemoryAllocator/html/resource_aliasing.html);
    [Aliasing transient textures in DirectX 12 [confirmed]](https://pavelsmejkal.net/Posts/TransientResourceManagement).

11. **Subresource-level transitions**: a resource can have different subresources in different
    states; `D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES` vs. an explicit subresource index.
    Mismatched/overlapping subresource state → hazard. Split barriers (BEGIN/END) are allowed.
    [Using Resource Barriers [confirmed]](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12.md)

**Metal implication.** Metal synchronization is encoder-boundary + `MTLFence`/`MTLEvent`/
`MTLBarrier` and is generally **coarser** than D3D12. D3D12 UAV barriers between two compute
encoders must map to a real Metal barrier/fence between the encoders — if DXMT coalesces or
drops the UAV barrier (e.g., treats both encoders as "UAV, no-op"), two compute passes overlap
on the same Metal buffer with no synchronization → race → corruption or fault. Aliasing barriers
must map to `MTLHeap` (`makeAliasable`) usage with proper barriers/invalidation. AC6's heavy
gameplay is compute-heavy (GPU-driven culling/visibility, skinning, shadows, async compute) →
**UAV-barrier correctness is a prime suspect for the after-Continue fault**.

**Concrete fix.** Map **every** D3D12 `TRANSITION`/`UAV`/`ALIASING` barrier to a concrete Metal
synchronization primitive (end encoder + `MTLFence`/`MTLSharedEvent`, or `MTLBarrier`). Never
coalesce or drop a UAV barrier between two encoders that touch the same resource. Track
per-subresource states and emit `MTLResourceUsage` hazards for argument-buffer resources. Route
through the `barriers-render-pass-only` probe and Phase 6 `POST /diagnostics/command-replay/validate`.

---

### 5. `ExecuteIndirect` argument/count buffers — GPU-written count is the trap

References: [Indirect Drawing [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/indirect-drawing),
[Indirect drawing and GPU culling [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/indirect-drawing-and-gpu-culling-),
[Predication queries [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/predication-queries),
and the [Direct3D 12 ExecuteIndirect sample [confirmed]](https://learn.microsoft.com/en-us/samples/microsoft/directx-graphics-samples/d3d12-execute-indirect-sample-win32/).

12. **`ExecuteIndirect` executes a command signature over an argument buffer**, optionally driven
    by a **count buffer** whose value the GPU reads to determine how many commands to run
    (`min(count-buffer value, MaxCommandCount)`). The count buffer is commonly **written by a
    prior GPU compute pass** (the GPU-culling/GPU-driven pattern). [Indirect Drawing [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/indirect-drawing)

13. **Argument and count buffers must be GPU-readable at `ExecuteIndirect` time** and must remain
    valid (not recycled/overwritten) during execution. If a compute pass wrote them, a
    UAV/transition barrier must guarantee the writes complete and are visible before the indirect
    read. [Indirect drawing and GPU culling [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/indirect-drawing-and-gpu-culling-);
    [ExecuteIndirect sample [confirmed]](https://learn.microsoft.com/en-us/samples/microsoft/directx-graphics-samples/d3d12-execute-indirect-sample-win32/).
    Command signature/argument layout: [D3D12_COMMAND_SIGNATURE_DESC [canonical]](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_command_signature_desc),
    [D3D12_INDIRECT_ARGUMENT_DESC [canonical]](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_indirect_argument_desc),
    [ID3D12GraphicsCommandList::ExecuteIndirect [canonical]](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-executeindirect).

**Metal implication.** Metal offers `drawIndirect`/`dispatchThreadgroupsWithIndirectCommandBuffer`
and `MTLIndirectCommandBuffer` (ICB), but the model differs: a Metal ICB is a **baked command
buffer**, whereas a D3D12 argument buffer is **raw structured data interpreted by the command
signature**. If DXMT materializes indirect commands by **CPU-reading the argument/count buffer at
record time**, it (a) stalls the CPU and (b) reads stale/zero counts because the GPU hasn't
written them yet when the command list is recorded. That produces wrong draw counts → either
under-render (visual) or, if the CPU readback races with a GPU write, a fault. Title screen:
static indirect draws with host-written counts. Heavy gameplay: **GPU-driven rendering**
(frustum/occlusion cull → write args+counts → `ExecuteIndirect` consumes) — precisely the path
that breaks if the count buffer isn't honored as a GPU-side resource.

**Concrete fix.** Implement `ExecuteIndirect` to consume GPU-side argument + count buffers via
Metal `drawIndirect`/`dispatchIndirect` or a generated `MTLIndirectCommandBuffer`, with a real
UAV→indirect-read barrier before it. **Never CPU-readback the count buffer** to materialize the
command count. Ensure argument/count buffers are not recycled until the `ExecuteIndirect`
completes (fence). Probe via `command-replay-only`.

---

### 6. Render-target / depth / UAV clear descriptor semantics

14. **`ClearRenderTargetView`**: the RTV descriptor must be valid and point at a resource in
    `RENDER_TARGET` (or compatible). [ClearRenderTargetView [canonical]](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-clearrendertargetview);
    [OMSetRenderTargets [confirmed]](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-omsetrendertargets).

15. **`ClearDepthStencilView`**: the resource must be in `DEPTH_WRITE` (full clear) or
    `DEPTH_READ`. [ClearDepthStencilView [canonical]](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-cleardepthstencilview).

16. **`ClearUnorderedAccessViewUint`/`ClearUnorderedAccessViewFloat`**: require a valid UAV
    descriptor whose **GPU handle is in a bound CBV_SRV_UAV heap**, a CPU descriptor handle
    (for validation), the target resource, and the clear value passed directly as `UINT Values[4]`
    / `FLOAT Values[4]`. The resource should be in `UNORDERED_ACCESS`. The GPU descriptor handle
    is read at execution time, so the descriptor must remain valid through execution.
    [ClearUnorderedAccessViewUint [canonical]](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-clearunorderedaccessviewuint).

17. **`Clear*` operations are recorded and execute on the GPU** at command-list execution; the
    descriptor validity is checked at execution, not record, time. They can be issued mid-pass.

**Metal implication.** Metal clears (`clearColor`, depth/stencil clear) are set on the
**render-pass descriptor at encoder begin**, not as mid-encoder commands. D3D12 `ClearRenderTargetView`
can be called mid-pass; DXMT must map mid-pass clears to either an `MTLLoadActionClear` at encoder
start (deferred pass-load clear) or a fullscreen-clear draw. UAV clears must be a real
**compute/blit clear** that writes the actual target buffer/texture resolved from the UAV
descriptor. Hazard: if DXMT defers clear-target resolution and the descriptor/resource isn't
retained, the clear writes the wrong resource — and a UAV buffer left un-cleared/garbage, later
read, yields a fault or hang. Title: a few clears; heavy gameplay: per-pass clears plus many UAV
clears (e.g., clearing tile/metadata/counter buffers) → a mis-resolved clear descriptor corrupts
buffers consumed later in the frame.

**Concrete fix.** **Materialize `Clear*` targets to concrete Metal resources at record time**
(don't defer descriptor resolution). Map UAV clears to a compute/blit clear writing the resolved
target; check resource state (`UNORDERED_ACCESS` for UAV clears). Map mid-pass RT/DS clears to
`MTLLoadActionClear` on the next render pass or a clear-draw. Probe via `resource-views-formats-only`.

---

### 7. Copy-to-SRV hazards — the streaming-upload race

18. **Copy operations require the source to be readable (implicit copy-source promotion for
    buffers/simultaneous-access textures) and the destination in `COPY_DEST`.** To then read the
    data via an SRV, the resource must transition to a shader-resource state
    (`NON_PIXEL_SHADER_RESOURCE` / `PIXEL_SHADER_RESOURCE` / `ALL_SHADER_RESOURCE` /
    `GENERIC_READ`). [Using Resource Barriers [confirmed]](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12.md);
    [D3D12_RESOURCE_STATES [canonical]](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_states).

19. **For buffers**, COMMON (which `COPY_DEST` decays to) is implicitly promotable to SRV read
    without an explicit barrier; **for textures**, you generally need an explicit
    `COPY_DEST`→SRV transition. Reading a texture as an SRV while it is still a copy destination
    (or still bound as RT/DS) is a hazard. [Subresources [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/subresources).

**Metal implication.** DXMT must ensure the Metal blit copy (from `MTLBlitEncoder`) completes
and is synchronized before the SRV read. The D3D12 `COPY_DEST`→`PIXEL_SHADER_RESOURCE` transition
must produce a real Metal synchronization point (encoder boundary + `synchronize`/fence). Title:
few texture uploads. Heavy gameplay: **streaming texture uploads** (staging → copy to texture →
bind as SRV, every frame) → if the copy→SRV barrier is dropped or the blit isn't synchronized,
Metal reads the texture while the blit is still writing → race → corrupted texels or fault.

**Concrete fix.** Map every `COPY_DEST`→SRV transition to a Metal encoder boundary with explicit
synchronization (end the blit encoder and `synchronize`, or insert an `MTLFence`). Do not rely on
implicit buffer promotion for texture SRV reads. Ensure upload staging resources are not recycled
until the copy completes.

---

### 8. Device removal / page-fault GPU faults — and what they mean on Metal

References: [WDDM Timeout Detection and Recovery (TDR) [confirmed]](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/timeout-detection-and-recovery),
[GetDeviceRemovedReason [canonical]](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getdeviceremovedreason),
[D3D12_REMOVE_REASON [canonical]](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_removed_reason).

20. **`ID3D12Device::GetDeviceRemovedReason` returns the HRESULT** for why the device was removed:
    `DXGI_ERROR_DEVICE_REMOVED` (0x887A0005), `DXGI_ERROR_DEVICE_HUNG` (0x887A0006),
    `DXGI_ERROR_DEVICE_RESET` (0x887A0007), `DXGI_ERROR_DRIVER_INTERNAL_ERROR` (0x887A0020),
    `E_OUTOFMEMORY`, etc. [GetDeviceRemovedReason [canonical]](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getdeviceremovedreason);
    real-world examples: [Path of Exile device-removed 0x887a0005](https://www.pathofexile.com/forum/view-thread/3796429),
    [RPCS3 Persona 5 device-removed 0x887A0001](https://github.com/RPCS3/rpcs3/issues/3015).

21. **Common removal causes:**
    - **TDR** — a GPU operation exceeds the ~2-second default TDR delay → the OS resets the GPU.
      [WDDM TDR [confirmed]](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/timeout-detection-and-recovery).
    - **Page fault / invalid GPU virtual address** — a shader or command accesses a freed/invalid
      GPU VA: referencing a freed resource, an out-of-bounds descriptor, a **stale descriptor
      pointing to freed memory**, or an unmapped tiled resource.
    - Invalid fence/command stream, or a hardware/driver fault.

22. **Device removal is fatal:** all subsequent D3D12 calls fail; the device must be recreated and
    in-flight command lists are discarded. **DRED** (Device Removed Extended Data) captures the
    faulting command, the faulting GPU VA, and page-fault details for post-mortem.

**Metal implication.** On Metal there is **no "device removed"** — the `MTLDevice` persists. GPU
faults manifest as a `MTLCommandBuffer` finishing with `MTLCommandBufferStatusError` (with a
domain/code describing the fault), or as a **process crash / GPU panic**. The Metal equivalent of
a D3D12 page fault is binding a freed/invalid Metal resource, an out-of-bounds argument-buffer
access, or an unsynchronized hazard. DXMT must translate Metal command-buffer errors into
`DXGI_ERROR_DEVICE_REMOVED`/`DEVICE_HUNG` and propagate to the game's
`GetDeviceRemovedReason`. **AC6 faulting only after Continue strongly indicates a resource-hazard
GPU fault (page-fault equivalent): a descriptor/resource referenced during heavy gameplay points
to freed/recycled Metal memory, or two passes race on shared memory.** A TDR (hang) is the less
likely cause — that presents as a multi-second freeze, not a fast crash.

**Concrete fix.** Implement DRED-equivalent capture: on Metal command-buffer error, record the
faulting encoder/draw index, the descriptor table state at fault, and the resolved Metal
resource bindings. Translate `MTLCommandBuffer` error → `DXGI_ERROR_DEVICE_REMOVED`. The **root
cause is upstream** (one of Findings 1–7); the fix is to enable Metal validation
(`MTLCaptureScope`/validation enabled) during an AC6 Continue repro to catch the exact fault, then
fix the descriptor-lifetime / barrier / copy-to-SRV hazard it identifies.

---

## AC6 failure-mode analysis (why menu works, Continue fails)

| D3D12 path | Menu (stable) | Continue → heavy gameplay (stressed) | Hazard that surfaces |
|---|---|---|---|
| Descriptor lifetime | small fixed set | per-frame slot reuse, many CBVs/SRVs/UAVs | stale/dangling Metal binding (Finding 1) |
| Heap mutation after record | stable layout | aggressive per-frame reuse | late-binding binds recycled descriptor (Finding 2) |
| Allocator/queue lifetime | few long command lists | many short lists, async-compute + graphics | in-flight allocator reset; missing cross-queue sync (Finding 3) |
| UAV barriers | none/trivial | compute→compute, compute→graphics | dropped/coalesced UAV barrier → race (Finding 4) |
| `ExecuteIndirect` | static, host-written counts | **GPU-driven** (cull→write counts→indirect) | CPU readback of GPU-written count; stale args (Finding 5) |
| `Clear*` descriptors | a few | many per-pass + UAV clears | mis-resolved clear target → corrupted buffers (Finding 6) |
| Copy→SRV | rare uploads | streaming texture uploads each frame | dropped copy→SRV barrier → read-while-writing (Finding 7) |

This is the canonical signature of a **translation-layer correctness bug in the hazard-bearing
code paths**, exposed only when the engine exercises them. The most likely single root causes for
AC6 specifically, in priority order: **(a)** a missing/coalesced UAV barrier between compute and
graphics on a shared UAV (Findings 4, 5), **(b)** a descriptor-lifetime bug from per-frame heap
reuse (Findings 1, 2), **(c)** an in-flight command-allocator/queue sync bug (Finding 3), or
**(d)** a GPU-written `ExecuteIndirect` count buffer being CPU-readback or raced (Finding 5).

---

## Concrete fix requirements (checklist for the DXMT-M12 backend)

1. **Descriptor lifetime (Findings 1–2).** Retain descriptor→Metal-resource bindings for the
   lifetime of every referencing command buffer; defer release to the submitting queue's fence
   signal. **Snapshot descriptors at record time** (capture concrete `MTLTexture`/`MTLBuffer` +
   offset/stride), do not late-bind at execute time. Ring/fence per-frame heap regions.
2. **Allocator/queue lifetime (Finding 3).** Tie Metal command-buffer storage lifetime to the
   D3D12 allocator; only recycle after the submit-fence signals. Per-queue (not per-frame) GPU
   completion tracking. Insert `MTLSharedEvent`/`MTLFence` waits for multi-queue shared resources.
3. **Barriers (Finding 4).** Map **every** `TRANSITION`/`UAV`/`ALIASING` barrier to a concrete
   Metal sync primitive; never coalesce or drop UAV barriers between encoders touching the same
   resource. Track per-subresource state; emit `MTLResourceUsage` hazards for argument buffers.
4. **`ExecuteIndirect` (Finding 5).** Consume GPU-side argument + count buffers via
   `drawIndirect`/`dispatchIndirect` or a generated `MTLIndirectCommandBuffer`; real
   UAV→indirect-read barrier; **no CPU readback** of the count buffer; don't recycle args/counts
   until completion.
5. **`Clear*` (Finding 6).** Materialize clear targets to concrete Metal resources at record
   time; UAV clears via compute/blit writing the resolved target; check `UNORDERED_ACCESS`
   state; map mid-pass RT/DS clears to `MTLLoadActionClear`/clear-draw.
6. **Copy→SRV (Finding 7).** Map every `COPY_DEST`→SRV transition to a Metal encoder boundary
   with explicit `synchronize`/fence; do not rely on implicit promotion for textures.
7. **Device removal / faults (Finding 8).** Translate `MTLCommandBuffer` errors into
   `DXGI_ERROR_DEVICE_REMOVED`; implement DRED-equivalent capture (faulting encoder + descriptor
   table + resolved Metal bindings). Enable Metal validation during an AC6 Continue repro.
8. **Validation routes.** Exercise the local gates that target exactly this surface:
   `POST /diagnostics/binding-contract/validate` (Phase 5), `POST /diagnostics/command-replay/validate`
   (Phase 6), and the `barriers-render-pass-only`, `command-replay-only`, `descriptors-only`,
   `resource-views-formats-only` probes under `tools/d3d12-metal-sdk/scripts/run-probes.sh`.

---

## Sources

**Kept (authoritative / primary):**
- [Shader Visible Descriptor Heaps — Microsoft Learn [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/shader-visible-descriptor-heaps) — descriptor-read-at-execution-time + immutability rule (Findings 1, 2).
- [Using Resource Barriers to Synchronize Resource States in Direct3D 12 — Microsoft Learn / GitHub source [confirmed]](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12.md) (canonical Learn URL: `/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12`) — transition/UAV/aliasing/split-barrier semantics + implicit promotion/decay (Findings 4, 7).
- [Enhanced Barriers — DirectX-Specs [confirmed]](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html) — authoritative barrier spec (UAV/aliasing semantics) (Finding 4).
- [D3D12 Enhanced Barriers Preview — DirectX Developer Blog [confirmed]](https://devblogs.microsoft.com/directx/d3d12-enhanced-barriers-preview/) — barrier rationale/examples (Finding 4).
- [Indirect Drawing — Microsoft Learn [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/indirect-drawing) + [Indirect drawing and GPU culling [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/indirect-drawing-and-gpu-culling-) — `ExecuteIndirect` argument/count buffer semantics (Finding 5).
- [Predication queries — Microsoft Learn [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/predication-queries) — GPU-determined execution (Finding 5).
- [Direct3D 12 ExecuteIndirect sample — Microsoft Learn [confirmed]](https://learn.microsoft.com/en-us/samples/microsoft/directx-graphics-samples/d3d12-execute-indirect-sample-win32/) — reference GPU-driven count-buffer usage (Finding 5).
- [Subresources (Direct3D 12) — Microsoft Learn [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/subresources) — subresource-level transitions (Findings 4, 7).
- [Porting from Direct3D 11 to Direct3D 12 — Microsoft Learn [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/porting-from-direct3d-11-to-direct3d-12) — app-owned lifetime/sync discipline (Findings 1, 2, 3).
- [Pipelines and Shaders with Direct3D 12 — Microsoft Learn [confirmed]](https://learn.microsoft.com/en-us/windows/win32/direct3d12/pipelines-and-shaders-with-directx-12) — PSO/command model (Finding 3).
- [OMSetRenderTargets — Microsoft Learn [confirmed]](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-omsetrendertargets) — RT binding semantics (Finding 6).
- [WDDM Timeout Detection and Recovery (TDR) — Microsoft Learn [confirmed]](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/timeout-detection-and-recovery) — TDR ~2s reset semantics (Finding 8).
- [DirectX 12 Programming Guide index — MicrosoftDocs/win32 GitHub [confirmed]](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/direct3d12/directx-12-programming-guide.md) — doc-set map for canonical slugs.
- [Direct3D 12 ray tracing sample missing UAV barrier — DirectX-Graphics-Samples issue #789 [confirmed]](https://github.com/microsoft/DirectX-Graphics-Samples/issues/789) — real-world UAV-barrier omission (Finding 4).
- [Enhanced Barriers DISCARD metadata init — DirectX-Specs issue #167 [confirmed]](https://github.com/microsoft/DirectX-Specs/issues/167) — discard/initialization edge cases (Finding 4).

**Kept (reputable secondary — implementation detail/rationale):**
- [AMD D3D12MemoryAllocator: Resource Aliasing (overlap) [confirmed]](https://gpuopen-librariesandsdks.github.io/D3D12MemoryAllocator/html/resource_aliasing.html) — aliasing-barrier mechanics + memory model (Finding 4).
- [Aliasing transient textures in DirectX 12 — Pavel Šmejkal [confirmed]](https://pavelsmejkal.net/Posts/TransientResourceManagement) — placed-resource aliasing patterns (Finding 4).
- [Managing Descriptor Heaps — Diligent Graphics [confirmed]](http://diligentgraphics.com/diligent-engine/architecture/d3d12/managing-descriptor-heaps/) — non-shader-visible→shader-visible copy/staging model (Findings 1, 2).
- [Efficient D3D12 Descriptor Heap Management System — GameDev [confirmed]](https://www.gamedeveloper.com/programming/efficient-d3d12-descriptor-heap-management-system) — descriptor lifetime/management strategy (Findings 1, 2).

**Canonical-but-not-search-confirmed (stable Microsoft Learn URL structure — re-read before commit):**
- [Descriptor Heaps overview](https://learn.microsoft.com/en-us/windows/win32/direct3d12/descriptor-heaps)
- [Resource Binding in Direct3D 12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/resource-binding)
- [Command Lists and Queues](https://learn.microsoft.com/en-us/windows/win32/direct3d12/command-lists-and-queues)
- [D3D12_RESOURCE_STATES enum](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_states)
- [D3D12_RESOURCE_BARRIER_TYPE enum](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_barrier_type)
- [ID3D12GraphicsCommandList::ExecuteIndirect](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-executeindirect)
- [D3D12_COMMAND_SIGNATURE_DESC](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_command_signature_desc)
- [D3D12_INDIRECT_ARGUMENT_DESC](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_indirect_argument_desc)
- [ClearRenderTargetView](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-clearrendertargetview)
- [ClearDepthStencilView](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-cleardepthstencilview)
- [ClearUnorderedAccessViewUint](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-clearunorderedaccessviewuint)
- [ID3D12Device::GetDeviceRemovedReason](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getdeviceremovedreason)
- [D3D12_REMOVE_REASON](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_removed_reason)
- [Fence-Based Synchronization](https://learn.microsoft.com/en-us/windows/win32/direct3d12/user-mode-heap-synchronization)

**Dropped:**
- DigitalTrends/Gamespot/PCWorld/Ars "DirectX 12 vs 11" news articles — consumer coverage, no technical semantics.
- Docker Hub / MDN / random image results surfaced by degraded `general`/`it`/`science` search categories — irrelevant.
- YouTube "how to fix D3D12 device removed" / windowsreport/pinterest troubleshooting guides — end-user fixes, not spec semantics (kept the PoE/RPCS3 forum issues only as concrete removal-HRESULT examples in Finding 20).

---

## Gaps

- **Could not fetch full text of any Microsoft Learn page** — the `read` tool is local-only and
  the search backend's `general` category could not surface Microsoft Learn content (only `images`
  surfaced the URLs). Semantic wording is synthesized from stable D3D12 spec knowledge and the
  confirmed source locations; **re-read the cited URLs directly** before quoting exact language in
  a PR or comment. The canonical-but-unconfirmed URLs above should be opened to confirm exact
  slugs/enums.
- **No AC6-specific capture was taken** (and the task forbids launching games). The
  failure-mode analysis is structural (which D3D12 paths menu vs. gameplay exercises), not
  evidence from an actual AC6 Metal fault. Recommended next step: enable Metal validation
  (`MTLCaptureScope`, Metal API validation) during an AC6 Continue repro and capture the exact
  faulting command buffer + error domain/code; map it back to the matching finding above.
- **Enhanced Barriers vs. legacy `ResourceBarrier`**: AC6 almost certainly uses the **legacy**
  `ID3D12GraphicsCommandList::ResourceBarrier` path (Enhanced Barriers are opt-in and recent).
  The brief covers both; the legacy path is what the fixes must target. The exact AC6 D3D12 SDK
  version / feature level was not verified.
- **`ClearUnorderedAccessView*` signature detail**: confirm the exact parameter ordering
  (`ViewGPUHandleInCurrentHeap`, `ViewCPUHandle`, `pResource`, `Values[4]`, `NumRects`, `pRects`)
  at the canonical URL before implementing the clear-value path — the brief states it from
  established API knowledge but did not read the live page.

---

## Supervisor coordination

No decision needed. Research brief complete and written to the requested path. The degraded
search backend meant full-text page fetches were not possible; the brief is built from confirmed
Microsoft Learn / DirectX-Specs URLs (surfaced via the `images` search category) plus stable
D3D12 specification semantics, with all unverified assumptions flagged in **Gaps**.
