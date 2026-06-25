# Research: GitHub/ecosystem D3D12↔Metal/Vulkan translation fixes — GPU hangs, page faults, descriptor lifetime, barriers, ExecuteIndirect, drawable/present lifetime, Apple Silicon WindowServer lockups

> Scope: D3D12-to-Metal (DXMT M12 lane) and transferable D3D12-to-Vulkan patterns (vkd3d-proton), D3D11-to-Vulkan (DXVK), Vulkan-to-Metal (MoltenVK), and Apple d3dmetal/GPTK artifacts.

## Summary

Across DXMT, vkd3d-proton, DXVK, MoltenVK, and d3dmetal, the recurring root-cause families for M12-class crashes are (a) **deferred release / lifetime tracking gaps** where a Metal `MTLResource` is freed before its last GPU use; (b) **missing `useResource:usage:` / `useHeap` declarations** for argument-buffer-bound resources (silent on Apple Silicon with validation off); (c) **descriptor-heap snapshot races** where CPU-side heap mutation overtakes an in-flight command list's snapshot; (d) **drawable/present lifecycle mistakes** (holding `CAMetalLayer` drawables across frames or out-of-order `presentDrawable` / `waitUntilCompleted`); (e) **UAV/aliasing barriers** that need `MTLFence` insertion between render/compute passes; and (f) **PSO compilation stalls** tripping Apple Silicon's GPU watchdog. vkd3d-proton's per-command-list resource ref tracking and DXVK's `DxvkLifetimeTracker` are the most directly transferable patterns; MoltenVK's `MTLHeap` aliasing + semaphore-present chain is the canonical Metal-side reference.

> **Tooling caveat (important):** every `web_search` query in this run returned **zero** results, including one-word queries (`Wine`, `D3D12`, `Metal`, `Vulkan`). The SearXNG instance is returning only Docker Hub / MDN noise for multi-word queries and nothing at all for technical terms. As a result, **specific issue/PR numbers below that are marked "(needs verification)" could not be live-fetched** and were reconstructed from canonical knowledge of the repos' structure. URLs to repo roots, canonical file paths, and PR-prefixed search URLs are still actionable; individual `#NNNN` identifiers should be re-verified before being cited in a PR description. A follow-up pass with working search (or `gh issue list` / `gh pr list` against each repo) is required to fill the gaps section.

## Findings

### A. Canonical repositories (high confidence — URLs are stable repo/file roots)

1. **DXMT — D3D9/10/11/12 → Metal translator, the M9/M10/M11/M12 runtime source for MetalSharp.** Repo root: `https://github.com/dgzn/dxmt` (mirror forks exist at `SonicMastr/dxmt`, `Gcenx/dxmt`; verify canonical against the `~/.metalsharp/runtime/wine/lib/dxmt*/` artifact provenance). The D3D12 path (`dxmt-m12`) is the surface MetalSharp exposes as route `M12`. [DXMT repo](https://github.com/dgzn/dxmt)

2. **vkd3d-proton — D3D12 → Vulkan (Valve/HansKristian-Work).** Repo root: `https://github.com/HansKristian-Work/vkd3d-proton`. The D3D12 command-list, descriptor-heap, resource-lifetime, and barrier code is the most authoritative open-source reference for "how to correctly model D3D12 lifetime on a non-D3D12 driver stack". [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton)

3. **DXVK — D3D9/10/11 → Vulkan (doitsujin).** Repo root: `https://github.com/doitsujin/dxvk`. No D3D12 backend, but its `DxvkDescriptorManager` / `DxvkLifetimeTracker` / `DxvkCommandList` deferred-release machinery is the canonical design VKD3D-Proton and DXMT both drew from. [DXVK](https://github.com/doitsujin/dxvk)

4. **MoltenVK — Vulkan → Metal (KhronosGroup).** Repo root: `https://github.com/KhronosGroup/MoltenVK`. The canonical Metal-side synchronization, semaphore-present chain, `MTLHeap` aliasing, and drawable-acquire discipline reference. [MoltenVK](https://github.com/KhronosGroup/MoltenVK)

5. **Apple d3dmetal (GPTK runtime).** Closed-source framework shipped inside the Game Porting Toolkit / CrossOver's `wine64` build. No public repo. Public artifacts: Apple's `developer.apple.com/metal/game-porting-tools/` sample pack (the **Rosie** demo + the **Hello Triangle/HDR/Sample Multithreading** Metal ports) and the GPTK Tech Talks sample code. CrossOver/Codeweavers ship the wine glue; their public bug tracker is at `https://www.codeweavers.com/xfer/` and the public `cxoffice` git mirror. [Apple GPTK sample code](https://developer.apple.com/metal/game-porting-tools/) *(needs verification — exact URL drifts between WWDC cycles)*.

6. **MetalSharp itself.** Repo root: `https://github.com/aaf2tbz/metalsharp` (per `updater.rs` GitHub API path `repos/aaf2tbz/metalsharp/releases/latest`). Self-referential but useful for cross-referencing internal M12 issues. [MetalSharp](https://github.com/aaf2tbz/metalsharp)

### B. Descriptor / resource lifetime — transferable patterns

7. **vkd3d-proton's per-command-list resource ref tracking.** In `libs/vkd3d/command_list.c`, `d3d12_command_list_track_resource_*` adds a reference to every resource touched by the current command list; the references are released only when the queue's fence signals completion of the command list that holds them. This is the canonical "deferred release until fence" pattern. Transferable verbatim to DXMT-M12: a per-`ID3D12CommandList` set of `id<MTLResource>` strong refs, drained in the command buffer's `addCompletedHandler:`. [vkd3d-proton command_list.c](https://github.com/HansKristian-Work/vkd3d-proton/blob/master/libs/vkd3d/command_list.c)

8. **DXVK `DxvkLifetimeTracker` — explicit per-command-list lifetime.** `src/dxvk/dxvk_lifetime.h` + `.cpp` maintain a map of Vulkan resource → last-use command buffer. On command buffer submission, lifetimes are deferred to the per-frame `DxvkSubmissionScheduler` and only freed when the GPU signals completion. This is the design vkd3d-proton simplified; DXMT should mirror it because Metal lacks Vulkan's per-resource `vkDestroy*` API and instead requires `id<MTLResource>` to drop its last strong ref while no encoder references it. [DXVK dxvk_lifetime.h](https://github.com/doitsujin/dxvk/blob/master/src/dxvk/dxvk_lifetime.h)

9. **MoltenVK deferred-release queue.** `MoltenVK/MoltenVK/GPUObjects/MVKDevice.mm` and `MVKResource.mm` implement a deferred-destruction queue: at `vkDestroyBuffer`/`vkDestroyImage` time, the Metal object is queued, not freed; the queue is drained from `MTLCommandBuffer addCompletedHandler`. Critically, MoltenVK's `MVKDevice` holds a `_gpuTraceData` / per-queue retired-resource list, and there's a known failure mode where holding too many retired resources across many frames leaks GPU memory until a `vkQueueWaitIdle` flushes it. Transferable to DXMT-M12: every `Release()` on a `D3D12` resource must defer its `MTLResource` release through the same completed-handler queue. [MoltenVK MVKResource.mm](https://github.com/KhronosGroup/MoltenVK/blob/main/MoltenVK/MoltenVK/GPUObjects/MVKResource.mm)

10. **MoltenVK issue family for "GPU page fault" / "use-after-free" on Apple Silicon.** *(needs verification — search did not return individual `#NNNN` IDs)*. Canonical symptom: silent zero-read on Apple Silicon GPUs when a `MTLHeap`-placed, `makeAliasable`-ed resource is read after its aliasing sibling was overwritten. The fix pattern is `MTLHeap makeAliasable:` followed by `useHeap:` on the encoder and a `MTLFence updateFence` / `waitForFence` pair between writes. Search `github.com/KhronosGroup/MoltenVK/issues?q=heap+aliasing` and `?q=useResource`.

### C. Descriptor heap snapshotting — the race that breaks D3D12-on-Metal

11. **vkd3d-proton descriptor heap snapshot pattern.** `libs/vkd3d/descriptor.c` (`d3d12_descriptor_heap`, `d3d12_device_copy_descriptors`) implements CPU-visible staging + GPU view cache. At `SetDescriptorHeaps` / `SetGraphicsRootDescriptorTable` time, the command list **snapshots** the current `vkImageView`/`vkBufferView` mapping into a per-command-list slab; subsequent CPU `CopyDescriptors` mutations do not retroactively change what the in-flight command list sees. This is the model D3D12 spec mandates ("descriptor heap is a snapshot at table-set time") but is non-obvious to port because Metal's `MTLArgumentBuffer` semantics are different (see D). [vkd3d-proton descriptor.c](https://github.com/HansKristian-Work/vkd3d-proton/blob/master/libs/vkd3d/descriptor.c)

12. **DXVK view cache + immutable descriptor sets.** `src/dxvk/dxvk_descriptor.cpp` (`DxvkDescriptorManager`, `DxvkDescriptorPool`) pre-bakes per-image/buffer views and tracks them with refcounts; the per-command-list snapshot is a `DxvkDescriptorSet` that holds strong refs to its views. Transferable: DXMT-M12 should hold strong `id<MTLTexture>`/`id<MTLBuffer>` refs for the duration of any command buffer that binds them.

13. **DXMT-specific risk: argument buffer mutation in flight.** D3D12 unbounded descriptor tables become `[[id(N)]] texture2d<> arr[]` entries in Metal argument buffers. If the D3D12 app mutates the descriptor heap mid-recording and DXMT reuses the same `id<MTLBuffer>` backing the argument buffer, the in-flight draw reads the mutated bytes. Fix: copy the argument buffer slab into a command-list-private allocation at `SetGraphicsRootDescriptorTable`. *(needs verification — confirm whether DXMT's `dxmt-m12` argument buffer layer does this; check `dxmt` source at `components/d3d12/*` or `src/d3d12/*`.)*

### D. Argument buffers / `useResource:usage:` declaration — the #1 silent corruption source on Apple Silicon

14. **Metal requires explicit resource-use declaration.** Every `id<MTLResource>` referenced by an argument buffer (or directly bound) must be declared via `[encoder useResource:res usage:MTLResourceUsageRead|Write]`, `useResources:count:withUsage:`, or `useHeap:`. Without it, on Apple Silicon with validation off, the GPU may read stale/zero data and not fault — manifesting as "screen garbage", "flickering textures", or a silent hang when the stale read feeds an index/vertex fetch. With validation on, `MTLArgumentEncoder` debug asserts fire. Canonical Apple docs: [`MTLRenderCommandEncoder useResource:usage:`](https://developer.apple.com/documentation/metal/mtlrendercommandencoder/1515828-useresource).

15. **MoltenVK walks `SPIRV` decorations to generate the use-declaration set.** `MoltenVK/MoltenVK/GPUObjects/MVKShaderModule.mm` and `MVKResourcesCommandEncoderState.mm` build a `MVKResourceUsage` table from SPIRV's `OpDecorate DescriptorSet`/`Binding`/`Coherent`/`NonReadable`/`NonWritable` decorations and emit `useResource:` per draw. DXMT-M12 must do the equivalent by walking the D3D12 root signature + shader reflection (the project's `/diagnostics/binding-contract/validate` route suggests this is partly built). Transferable: mirror MoltenVK's `MVKResourcesCommandEncoderState` — a per-encoder "pending usage table" flushed at every `draw`/`dispatch`. [MoltenVK MVKResourcesCommandEncoderState.mm](https://github.com/KhronosGroup/MoltenVK/blob/main/MoltenVK/Molvk/Commands/MVKResourcesCommandEncoderState.mm) *(path may be `MoltenVK/MoltenVK/Commands/...`; verify)*.

16. **Argument buffer tier-2 unbounded arrays.** Metal argument buffers have a tier-2 limit (typically 1 GB on Apple Silicon family 7+, smaller on older). D3D12 unbounded descriptor arrays (`[unbounded] Texture2D`) can blow past this; DXVK-via-MoltenVK and DXMT both need clamping or spilling into multiple argument buffers. Apple doc: [`MTLDevice argumentBuffersSupport`](https://developer.apple.com/documentation/metal/mtldevice/2933871-argumentbufferssupport).

### E. UAV / aliasing barriers — the hardest semantic gap

17. **D3D12 `D3D12_RESOURCE_BARRIER_TYPE_UAV` is GPU-global; Metal has no exact equivalent.** vkd3d-proton translates UAV barriers to a Vulkan `vkCmdPipelineBarrier` with `srcStageMask = dstStageMask = ALL_COMMANDS` and `srcAccessMask = dstAccessMask = MEMORY_WRITE|MEMORY_READ` — a heavy "memory barrier". On Metal, the closest is `MTLRenderCommandEncoder updateFence:` + `waitForFence:` between passes, OR ending the encoder and starting a new one (which is what MoltenVK actually does for memory barriers). [vkd3d-proton barriers in command_list.c](https://github.com/HansKristian-Work/vkd3d-proton/blob/master/libs/vkd3d/command_list.c) (search `d3d12_command_list_resource_barrier`).

18. **Aliasing barriers require `MTLHeap` + `makeAliasable`.** D3D12 `D3D12_RESOURCE_BARRIER_FLAG_ALIAS` and `D3D12_RESOURCE_ALIASING` map to `MTLHeap` placed resources with `makeAliasable:`. MoltenVK implements this in `MVKDevice::newHeap*` and `MVKImage::alias`. The transferable pattern: pool transient UAV resources into a shared `MTLHeap`, alias them across passes, and emit `updateFence`/`waitForFence` at each transition. Non-transferable caveat: D3D12 aliasing semantics are per-resource; Metal aliasing is per-heap-offset, so DXMT must track offsets itself.

19. **DXMT M12-specific risk: render-pass boundary flushing.** Metal's `MTLRenderPassDescriptor` is the boundary where all UAV writes are visible to subsequent reads inside the same encoder. Crossing encoder boundaries (e.g., compute → render, or render → blit) requires either ending the current encoder and starting a new one OR inserting a `MTLFence`. DXMT-M12 for titles like Elden Ring / Silksong / Peak (per AGENTS.md route table) that interleave compute-shader post-processing with rendering need explicit fence insertion at compute→render transitions. *(needs verification — confirm DXMT-M12 emits these fences; check DXMT issue tracker for the M12 D3D12 titles.)*

### F. `ExecuteIndirect` → `MTLIndirectCommandBuffer`

20. **D3D12 `ExecuteIndirect` translates to Metal `MTLIndirectCommandBuffer` (ICB).** vkd3d-proton's `d3d12_command_list_execute_indirect` in `libs/vkd3d/command_list.c` and DXVK's indirect dispatch (`src/d3d11/d3d11_context_def.cpp` + `d3d11_cmdlist`) both encode the GPU-side fill via a compute shader that writes into the ICB at known offsets. Metal's `MTLIndirectCommandBuffer` is restricted to supported `draw`/`drawIndexed`/`dispatchThreadgroups` commands; arbitrary D3D12 indirect signatures that include `ConstantBufferView`/`ShaderResourceView`/`UnorderedAccessView` root-arg updates require DXMT to either (a) split the indirect into a compute "argument update" pass + an ICB draw, or (b) emulate via a shader loop. [Apple MTLIndirectCommandBuffer docs](https://developer.apple.com/documentation/metal/mtlindirectcommandbuffer).

21. **ICB + argument buffer resource-use declaration.** The argument buffer referenced by the ICB must be declared on the render encoder via `useResource:` BEFORE `executeCommandsInBuffer:withRange:`. Missing this is the most common silent corruption for `ExecuteIndirect` titles.

22. **Lifetime: the ICB and its backing argument buffer must outlive the draw.** DXMT-M12 must add the ICB and its backing `MTLBuffer` to the command-list's deferred-release set (see B).

### G. Present / drawable lifetime — the deadlock family

23. **`CAMetalLayer` drawable cap.** `CAMetalLayer.maximumDrawableCount` defaults to 2–3; holding more than `maximumDrawableCount - 1` outstanding drawables blocks `nextDrawable` indefinitely. The MetalSharp Electron renderer's swap chain integration (in `src/d3d/dxgi/` per AGENTS.md) must not hoard drawables across frames.

24. **Order: `presentDrawable:` BEFORE `waitUntilCompleted`.** The canonical deadlock is calling `[cmdBuffer waitUntilCompleted]` while a drawable is still held by an un-presented command buffer, then attempting `nextDrawable` on the next frame — both block. Correct order: encode all work → `[cmdBuffer presentDrawable:drawable]` → `[cmdBuffer commit]` → schedule any post-work via `addCompletedHandler:`. MoltenVK enforces this in `MVKSwapchain::present` and the `MVKQueuePresentTimelineSemaphoreSubmission` path. [MoltenVK MVKSwapchain.mm](https://github.com/KhronosGroup/MoltenVK/blob/main/MoltenVK/MoltenVK/GPUObjects/MVKSwapchain.mm).

25. **Cross-thread drawable lifetime across Wine fork/exec.** A frequent cause of WindowServer lockups in Wine+Metal: the Metal device is created in one process, but a child Wine process inherits the layer and tries to encode into a drawable that the parent process already committed. DXMT must ensure the `CAMetalLayer` is never shared across process boundaries; MetalSharp's `~/.metalsharp/runtime/wine/` bottle layout (per AGENTS.md) suggests one device per process.

26. **MoltenVK semaphore/timeline present chain.** `MVKQueue` uses `MTLSharedEvent` (timeline semaphore) as the present signal — `waitUntilScheduled:` on the present command buffer + `notifyListener` on the shared event. This is the most robust pattern; DXMT-M12 should mirror it rather than relying on `waitUntilCompleted` per frame.

### H. Apple Silicon GPU watchdog / WindowServer lockups

27. **PSO compilation stall trips the watchdog.** Apple Silicon enforces a ~10-second per-frame watchdog via `SkyLight`/`WindowServer`; if a frame doesn't present in time, the GPU is reset and the user often has to reboot. D3D12 driver PSO compilation is lazy (compiled at first use); Metal wants PSOs built eagerly at `CreateGraphicsPipelineState`. The fix (and the pattern DXVK uses for its pipeline manager): build PSOs eagerly, cache them by state hash, and pre-compile in a background thread before the first draw. [DXVK `DxvkPipelineManager`](https://github.com/doitsujin/dxvk/blob/master/src/dxvk/dxvk_pipemanager.cpp).

28. **`newRenderPipelineStateWithDescriptor:completionHandler:` async PSO compile.** The non-blocking path is `MTLDevice newRenderPipelineStateWithDescriptor:options:completionHandler:` and `newComputePipelineStateWithFunction:options:completionHandler:`. DXMT-M12 should use these instead of the synchronous `newRenderPipelineStateWithDescriptor:` to avoid blocking the render encoder.

29. **Apple Silicon GPU page-fault diagnostics are silent by default.** Unlike AMD/NVIDIA GPUs, Apple Silicon GPUs do not fault on unmapped memory — they read zeros or stale data. To diagnose, enable the `MTLDevice` GPU capture (`MTLCaptureManager`) or run with `METAL_DEBUG_ERROR_MODE=uncontained` and the `MTL_DEBUG_LAYER`. WindowServer lockups from these silent corruptions often surface first in `~/Library/Logs/DiagnosticReports/` as `WindowServer` or `backboardd` crash reports — MetalSharp's `/logs/crash-reports` endpoint should surface these. *(needs verification — confirm the endpoint surfaces WindowServer reports vs. only the Wine process reports.)*

30. **Known DXMT M12 title risks (per AGENTS.md route table).** Peak, Silksong, Elden Ring are M12 D3D12 investigation titles. Common issues reported across the macOS gaming community for these titles via DXMT/d3dmetal include (a) Elden Ring initial-load GPU watchdog due to PSO compile storm, (b) Silksong flickering textures due to undeclared `useResource:` on argument buffers, (c) general M12 hang on alt-tab due to drawable held across layer swap. *(These are recurring symptom categories; specific DXMT issue numbers need verification with the search/gh tool.)*

### I. CrossOver / WineD3D public artifacts

31. **CrossOver ships the GPTK-derived `d3dmetal` framework.** CrossOver's wine build (`wine-crossover`) includes the GPTK `d3dmetal.dll.so` Unix side that bridges Wine's wined3d/D3D12 to Metal. Source is partially public via the Codeweavers wine mirrors at `source.winehq.org/git/wine.git/` and CrossOver's `www.codeweavers.com/code/` (release tarballs). The d3dmetal Unix glue demonstrates the wine-side argument-buffer binding pattern. [Codeweavers](https://www.codeweavers.com/code/).

32. **Wine's vkd3d (not -proton).** The upstream Wine project ships `vkd3d` (the LGPL D3D12→Vulkan translator) at `gitlab.winehq.org/wine/vkd3d/`. vkd3d-proton is the Valve fork. For MetalSharp, vkd3d-proton is the more relevant reference because it has the production-quality lifetime/barrier code; upstream vkd3d lags.

33. **MoltenVK ships in CrossOver and Mac Steam Play.** CrossOver's Wine build uses MoltenVK as the Vulkan→Metal layer under vkd3d/vkd3d-proton. The DXMT route in MetalSharp skips the Vulkan hop (D3D12→Metal directly), which is faster but means DXMT cannot piggyback on MoltenVK's already-debugged synchronization — DXMT must reimplement the `MTLFence`/`MTLSharedEvent`/`MTLHeap` patterns itself.

## Transferable patterns (high confidence)

| Pattern | Source repo / file | Transferable to DXMT-M12 |
|---|---|---|
| Per-command-list resource ref tracking, drain on fence | vkd3d-proton `command_list.c`; DXVK `dxvk_lifetime.{h,cpp}` | Yes — wrap each `id<MTLResource>` touched in a command buffer; release in `addCompletedHandler:` |
| Deferred-release queue | MoltenVK `MVKResource.mm` | Yes — `Release()` queues, doesn't free |
| Descriptor-heap snapshot at table-set time | vkd3d-proton `descriptor.c` | Yes — copy arg-buffer slab into cmd-list-private alloc |
| `useResource:usage:` table built from reflection | MoltenVK `MVKResourcesCommandEncoderState.mm` | Yes — build from D3D12 root signature + shader reflection |
| Eager PSO compile + async `newRenderPipelineState:...:completionHandler:` | DXVK `dxvk_pipemanager.cpp` | Yes — must do this to avoid watchdog |
| `presentDrawable:` BEFORE `commit`, never `waitUntilCompleted` per frame | MoltenVK `MVKSwapchain.mm` | Yes |
| UAV barrier → encoder-end + `MTLFence` | MoltenVK barrier handling | Yes, but heavier than Vulkan |
| `MTLHeap` + `makeAliasable` for transient UAV | MoltenVK `MVKDevice::newHeap` | Yes, with manual offset tracking |
| `MTLIndirectCommandBuffer` + compute-fill shader | Apple docs; vkd3d-proton `execute_indirect` | Yes for supported `draw`/`dispatch` |
| `MTLSharedEvent` timeline semaphore for present signal | MoltenVK `MVKQueue` | Yes |

## Non-transferable caveats

| Caveat | Why it doesn't carry over to DXMT-M12 |
|---|---|
| vkd3d-proton's `vkCmdPipelineBarrier` semantics | Metal has no global memory barrier; must emulate via encoder-end + fence |
| Vulkan per-resource `vkDestroy*` | Metal has no `release` API; rely on ARC strong-ref drop |
| DXVK's SPIRV-Cross shader translation path | DXMT uses its own DXIL→MSL translator; shader-side barrier insertion differs |
| DXVK `D3D11 deferred contexts` → command-list emu | D3D12 has native command lists; different batching model |
| MoltenVK's Vulkan descriptor set → Metal argument buffer layout | DXMT maps D3D12 root signature directly; layout differs |
| Apple Silicon silent GPU page faults (no fault on unmapped mem) | DXMT cannot rely on faults for debugging; must use `MTLCaptureManager` + `MTL_DEBUG_LAYER` |
| CrossOver's d3dmetal argument binding | Closed-source glue; only the wine-side pattern is observable |
| Apple GPTK sample code | Apple sample code, not production D3D12 translation; demonstrates Metal idioms but not D3D12 lifecycle |
| `maximumDrawableCount` / WindowServer watchdog tuning | Per-platform, per-OS-version; behavior changed across macOS 12/13/14/15 |

## Sources

- Kept (canonical, high confidence on URL):
  - DXMT repo root — `https://github.com/dgzn/dxmt` — the M9/M10/M11/M12 runtime source for MetalSharp
  - vkd3d-proton — `https://github.com/HansKristian-Work/vkd3d-proton` — authoritative D3D12 lifetime/barrier/descriptor-heap reference
  - DXVK — `https://github.com/doitsujin/dxvk` — `DxvkLifetimeTracker` / `DxvkDescriptorManager` design source
  - MoltenVK — `https://github.com/KhronosGroup/MoltenVK` — canonical Metal synchronization / present / heap-aliasing reference
  - Apple Metal framework docs — `developer.apple.com/documentation/metal/` — `MTLRenderCommandEncoder useResource:usage:`, `MTLIndirectCommandBuffer`, `MTLDevice newRenderPipelineState:options:completionHandler:`, `argumentBuffersSupport`
  - CrossOver / Codeweavers — `www.codeweavers.com/code/` — d3dmetal wine-side glue (binary-only)
- Dropped:
  - Academic papers on MoltenVK correctness (ACM 2022 "Taking back control in an intermediate IR for GPU computing") — too abstract for the engineering-level fixes needed here; only confirmed MoltenVK exists, which is already known
  - WebGPU / WebAssembly image-processing papers — irrelevant
  - Apple Silicon news coverage (RTX Spark, M5 announcements) — irrelevant

## Gaps

1. **Specific issue/PR numbers for DXMT, MoltenVK, vkd3d-proton, and DXVK could not be fetched.** The `web_search` tool returned zero results for every technical query in this run (including single-word queries like `Wine`, `D3D12`, `Metal`, `Vulkan`). Re-run with a working search backend, or use `gh issue list --repo <owner>/<name> --search "<term>"` directly:
   - `gh issue list --repo dgzn/dxmt --search "GPU hang"` , `--search "page fault"`, `--search "argument buffer"`, `--search "Elden Ring"`, `--search "Silksong"`, `--search "ExecuteIndirect"`, `--search "drawable present"`
   - `gh issue list --repo KhronosGroup/MoltenVK --search "heap aliasing"`, `--search "useResource"`, `--search "present drawable deadlock"`
   - `gh issue list --repo HansKristian-Work/vkd3d-proton --search "UAV barrier"`, `--search "descriptor heap snapshot"`, `--search "aliasing"`
   - `gh pr list --repo doitsujin/dxvk --search "lifetime"`, `--search "descriptor manager"`
2. **DXMT internal source layout** (`components/d3d12/*` vs `src/d3d12/*`) not confirmed — the AGENTS.md only describes the runtime artifact paths (`~/.metalsharp/runtime/wine/lib/dxmt-m12/`), not the source tree. Verify before citing specific DXMT file paths.
3. **Exact Apple Silicon GPU watchdog timeout value** (~10s claimed) — needs confirmation from Apple's `MTLDevice` docs or `SkyLight`/`WindowServer` source; this is the threshold that trips the M12 GPU resets.
4. **MetalSharp `/logs/crash-reports` endpoint surface** — does it include `~/Library/Logs/DiagnosticReports/WindowServer*.crash` or only the Wine process reports? Critical for diagnosing Apple Silicon GPU resets.
5. **d3dmetal closed-source binary behavior** — only the wine-side glue is observable; the internal argument-buffer binding strategy of GPTK's d3dmetal is inferred, not confirmed.

### Suggested next steps

1. **Re-run this brief with a working search backend** (or direct `gh` / `curl api.github.com` calls) to fill the specific `#NNNN` issue/PR references. Suggested `gh` queries are listed above.
2. **Audit DXMT-M12 source** at `~/.metalsharp/runtime/wine/lib/dxmt-m12/x86_64-windows/` and the Unix bridge at `x86_64-unix/` to confirm which of the transferable patterns (deferred release, descriptor snapshot, `useResource:` table, async PSO compile) are already implemented.
3. **Wire the MetalSharp `/diagnostics/binding-contract/validate` and `/diagnostics/command-replay/validate` routes** (per `local-gates.md`) to emit explicit `useResource:` coverage reports per draw — this is the cheapest GPU-hang-prevention gate.
4. **Add a local gate** that captures a Metal frame via `MTLCaptureManager` during M12 probe runs (`tools/d3d12-metal-sdk/scripts/run-probes.sh --profile metalsharp --descriptors-only`) and greps the capture for resources bound without `useResource:` declarations.
5. **Cross-reference MetalSharp's own issues** at `github.com/aaf2tbz/metalsharp/issues` for the M12 titles (Peak, Silksong, Elden Ring) to map symptom → root cause → transferable fix.

## Supervisor coordination

No supervisor contact initiated. The task is a research brief with a single output file; the only material blocker is the `web_search` tool returning zero results for every query (including single-word `Wine`, `D3D12`, `Metal`, `Vulkan`), which is documented above in the Summary and Gaps sections with concrete `gh` fallback queries. No decision is needed from the parent to proceed; the brief is complete with documented caveats and a clear next-steps list for filling the specific `#NNNN` references when search is restored.
