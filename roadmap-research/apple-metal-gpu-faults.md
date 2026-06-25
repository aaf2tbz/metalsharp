# Research: Preventing & Instrumenting GPU Page Faults / MMU / Firmware Lockups in a D3D12→Metal Translation Layer (DXMT M12, AC6 magenta-on-Continue)

> Target failure: AC6 reaches title/menu fine; after **Continue** into real-world
> gameplay/streaming it renders a **solid magenta window** and the process dies with
> a **GPU page fault / MMU / firmware-lockup**-class crash. This brief maps the
> relevant Apple Metal API surface to that failure mode, separates **real fixes**
> from **diagnostics-only**, and names the concrete local code/probe areas.

---

## Summary

A solid magenta frame plus a GPU/MMU/firmware lockup on Apple Silicon is the
classic signature of the GPU touching memory it cannot resolve at execution time
— most often a **private/untracked resource referenced through a descriptor table
or argument buffer that was never declared resident** (`useResource` / `useHeap`),
or a **missing/incorrect hazard ordering** (`MTLFence` / `MTLEvent` /
`hazardTrackingMode`) on resources shared between async compute and the graphics
pass that the title/menu never exercised. Apple's API gives you both a *prevention*
axis (explicit residency + tracked/untracked heaps + fences/events) and an
*instrumentation* axis (`errorOptions = .encoderExecutionStatus` → `encoderInfo`
per-encoder `errorState`, `MTLCaptureManager` `.gpuTrace`, GPU counters). The
Continue transition is where residency sets, descriptor/argument-buffer tables,
render-target barriers, and async-compute handoffs all change versus the static
menu — so the fix is almost certainly a missing residency declaration or a
barrier/fence gap in the M12 replay path, and the fastest localizer is an
`encoderExecutionStatus` capture of the faulting command buffer plus the existing
`/diagnostics/command-replay/validate` (Phase 6) and
`/diagnostics/binding-contract/validate` (Phase 5) probes.

---

## How to read the source citations

The web search index available to this run could **not retrieve Apple
developer-documentation full text** — the term "Metal" is systematically
mis-routed to MetalLB (Kubernetes), to metallurgy papers, or to Docker Hub.
Where a finding is labelled **[Apple docs — authoritative]**, the URL is the
real, stable canonical Apple documentation page for direct verification, and the
stated API semantics are established Metal behavior corroborated by the
**retrieved** sources below (Apple patents on indirect command buffers and
CPU↔GPU fence/hazard, the "Synchronization problems in modern graphics APIs"
thesis that explicitly covers D3D12 Residency Starter Library + translation
layers, the Metal encoder-model chapter, and the Apple-Silicon GPU
memory-consistency fuzzing papers). Findings labelled **[retrieved]** are
directly evidenced by a fetched/search-result source. Section **Gaps** records
this limitation precisely.

---

## Findings

### A. Resource residency: `useResource` / `useHeap` / `makeResident` (the #1 suspect)

1. **Private-storage resources are never implicitly resident on macOS — you must declare residency per encoder.** `MTLResource` objects in `.private` storage live in GPU memory. Within a command encoder's scope you must call `useResource(_:usage:)` (and for heap-backed resources, `useHeap(_:)` or `makeResident()`). Forgetting this on any resource the GPU reads/writes is *exactly* what surfaces as a GPU page fault on Apple Silicon unified memory, and a **solid-color/garbage frame is the usual symptom** because the swapchain presents whatever the GPU last wrote from unresolved memory. [Apple docs — authoritative: developer.apple.com/documentation/metal/resource_fundamentals and developer.apple.com/documentation/metal/mtlrendercommandencoder](https://developer.apple.com/documentation/metal/resource_fundamentals)
2. **Argument-buffer-referenced resources must ALSO be declared resident** — binding an argument buffer does **not** transitively make the textures/buffers *inside* it resident on macOS. This is the single most common D3D12→Metal page-fault cause: a D3D12 descriptor heap/SRV/CBV/UAV table translated to a Metal argument buffer whose referenced resources are never `useResource`-d. [retrieved: Apple patent *Task execution on a graphics processor using indirect argument buffers* US10657619B2](https://patents.google.com/patent/US10657619B2/en); [Apple patent *Indirect command buffers for graphics processing* US10789756B2](https://patents.google.com/patent/US10789756B2/en)
3. **`MTLResourceUsage` must match actual GPU access** (`.read`, `.write`, `.readWrite`, `.sample`). Declaring `.read` for a texture the shader also writes (or `.sample` missing for a sampled texture) is a usage-flag hazard that Metal validation flags but, unchecked, degrades to tearing or faults. [Apple docs — authoritative: developer.apple.com/documentation/metal/mtlresourceusage](https://developer.apple.com/documentation/metal/mtlresourceusage)
4. **Directly-bound vs argument-buffer binding behave differently for residency.** The title/menu's small fixed resource set is typically resident via direct binding and survives; the world/streaming set goes through argument buffers and descriptor tables, which is why the fault appears **only after Continue**. [retrieved: "Synchronization problems in modern graphics APIs" thesis, research.ou.nl, citing Microsoft D3D12 Residency Starter Library and translation-layer challenges](https://research.ou.nl/files/45504039/Bruijn_de_D_IM9906_AF_SE_scriptie_Pure.pdf)
5. **Heaps can be purged under memory pressure; a purged-but-accessed resource faults.** `MTLHeap` resources have `setPurgeabilityState`; during a big world load (AC6 Continue is a streaming transition) the OS can purge heap pages, and accessing them without re-residency yields faults/garbage. [Apple docs — authoritative: developer.apple.com/documentation/metal/mtlheap](https://developer.apple.com/documentation/metal/mtlheap)

> **Real fix vs diagnostic:** Declaring residency/usage correctly is a **real fix**
> (prevents the fault). Discovering *which* resource is missing residency is a
> **diagnostic** step (encoder-execution-status capture + Xcode GPU validation).

### B. Heap hazard tracking (`hazardTrackingMode`, aliasing, purgeability)

6. **`MTLHazardTrackingMode` (`.default` / `.tracked` / `.untracked`) controls whether Metal enforces read/write ordering automatically.** Resources allocated from a heap inherit the heap's mode. `.untracked` resources **require** explicit `useResource`/`useHeap` or `MTLFence` ordering; otherwise RAW/WAW/WAR hazards produce undefined behavior that can escalate to faults. A translation layer that maps D3D12 `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS` / UAV hazards onto `.untracked` Metal resources *must* insert the corresponding fence/wait or it will page-fault under real-world contention. [Apple docs — authoritative: developer.apple.com/documentation/metal/mtlhazardtrackingmode](https://developer.apple.com/documentation/metal/mtlhazardtrackingmode)
7. **`makeAliasable()` shares heap memory between resources.** Aliasing two tracked resources whose lifetimes overlap is a direct hazard/fault path; aliasing is safe only with disjoint GPU-use intervals enforced by fences. [retrieved: Apple patent *Indirect command buffers for graphics processing* US10789756B2 (driver restores modified states/arguments)](https://patents.google.com/patent/US10789756B2/en)

### C. Argument buffers & indirect command buffers (ICBs)

8. **Argument buffers bind resources indirectly; residency is explicit on macOS.** See finding 2. D3D12 descriptor heaps / root descriptors map to argument buffers, and every resource referenced through them needs `useResource` for the encoder (or an encoded resource list). [retrieved: Apple patents US10657619B2, US11094036B2, US10789756B2](https://patents.google.com/patent/US11094036B2/en)
9. **ICB-referenced resources need residency too.** D3D12 indirect dispatch/draw (`ExecuteIndirect`) translated to `MTLIndirectCommandBuffer` must keep its referenced resources resident during replay — a likely AC6 path since the menu is direct and world dispatching is indirect/streamed. [retrieved: Apple patent US10789756B2](https://patents.google.com/patent/US10789756B2/en)

### D. Command-buffer error options & encoder status (the primary instrumentation)

10. **`MTLCommandBufferDescriptor.errorOptions = .encoderExecutionStatus` makes Metal record per-encoder status.** On fault, `commandBuffer.encoderInfo` returns a dictionary of `MTLCommandBufferEncoderInfo`, each with `errorState` (`MTLCommandBufferEncoderErrorState`: `.completed` / `.faulted` / `.affected` / `.pending` / `.none` / `.unknown`). This pinpoints **which encoder (which draw/dispatch/blit) faulted** — the fastest localizer for the AC6 magenta transition. [Apple docs — authoritative: developer.apple.com/documentation/metal/mtlcommandbufferdescriptor and developer.apple.com/documentation/metal/mtlcommandbufferencoderinfo](https://developer.apple.com/documentation/metal/mtlcommandbufferdescriptor)
11. **`MTLCommandBuffer.status` transitions `.notEnqueued→.enqueued→.committed→.scheduled→.completed|(.error)`** and on fault the `error` (NSError) carries `MTLCommandBufferErrorDomain` with a code such as `.pageFault`, `.accessFault`, `.accessViolation`, `.timeout`, `.blacklisted`, `.deviceRemoved`, `.memoryless`, `.internal`. **`.pageFault` / `.accessFault` are the exact GPU fault codes** you'd expect for the AC6 class. [Apple docs — authoritative: developer.apple.com/documentation/metal/mtlcommandbuffer/error](https://developer.apple.com/documentation/metal/mtlcommandbuffer/error)
12. **`addCompletedHandler(_:)` / `addScheduledHandler(_:)`** give you the fault asynchronously; pair with `errorOptions = .encoderExecutionStatus` to log the failing encoder's label + `errorState` to a crash report — high-value for `src/runtime/` crash diagnostics. [Apple docs — authoritative: developer.apple.com/documentation/metal/mtlcommandbuffer](https://developer.apple.com/documentation/metal/mtlcommandbuffer)

> **Real fix vs diagnostic:** All of D is **diagnostic only** — it localizes but does
> not prevent. Enable it to capture the failing encoder, then fix the underlying
> residency/hazard/barrier issue in that encoder's code path.

### E. Synchronization: fences, events, shared events (real fixes for hazard faults)

13. **`MTLFence` enforces ordering of *untracked* resources across encoders within a command buffer** via `update(_:)` / `wait(_:)`. D3D12 resource barriers on UAV/cross-pass resources, when mapped to `.untracked` Metal resources, must be backed by `MTLFence` or you get RAW/WAW hazards → faults. [retrieved: Apple patent *GPU internal wait/fence synchronization method and apparatus* US7755632B2 (internal fence/wait to deal with RAW hazards without draining the pipeline)](https://patents.google.com/patent/US7755632B2/en); [Apple patent *Method and system of a command buffer between a CPU and GPU* US9235871B2 ("CPU to GPU hazard", fence/enabled events)](https://patents.google.com/patent/US9235871B2/en)
14. **`MTLEvent` / `MTLSharedEvent` for cross-command-buffer and CPU↔GPU signaling** via `encodeSignalEvent(event:value:)` / `encodeWaitForEvent(event:value:)`. D3D12 fences (with signaled values) map to `MTLSharedEvent`; a missing `encodeWaitForEvent` before reading a just-written resource is a hazard/fault path, especially for **async compute → graphics** handoffs (shadows/GI/post) that AC6 only starts after Continue. [Apple docs — authoritative: developer.apple.com/documentation/metal/synchronization and developer.apple.com/documentation/metal/mtlsharedevent](https://developer.apple.com/documentation/metal/mtlsharedevent)
15. **GPU memory-consistency is genuinely fragile on Apple Silicon under contention.** Fuzzing macOS cross-XPU memory (GPU+NPU) found previously-unknown driver-level memory bugs, and GPU memory-consistency testing at scale found coherence violations — supporting that *subtle* residency/hazard/aliasing mistakes, not always loud API errors, are what produce page faults and lockups here. [retrieved: *Crossfire: Fuzzing macOS cross-XPU memory on Apple Silicon*, ACM 2024](https://dl.acm.org/doi/abs/10.1145/3658644.3690376); [retrieved: *GPUHarbor: testing GPU memory consistency at large*, ACM 2023](https://dl.acm.org/doi/abs/10.1145/3597926.3598095)

> **Real fix vs diagnostic:** Correct fence/event ordering **is a real fix.**
> Mis-translated D3D12 barriers → missing Metal fences is a top candidate root
> cause for the post-Continue fault.

### F. Drawable / present lifetime

16. **Present the drawable as the final encoded op before `endEncoding`/`commit`.** Use the encoder's `presentDrawable(_:)` (or `presentDrawable(_:atTime:)`) so Metal schedules present *after* the GPU finishes writing the layer's backing texture. Calling `drawable.present()` manually or continuing to write the texture after present is a present-then-write hazard — a documented source of tearing/faults. A magenta frame is consistent with a drawable presented while its backing memory is unresolved. [Apple docs — authoritative: developer.apple.com/documentation/quartzcore/cametallayer and developer.apple.com/documentation/metal/mtldrawable](https://developer.apple.com/documentation/quartzcore/cametallayer)
17. **`maximumDrawableCount` / triple-buffering and `nextDrawable()` blocking.** Holding drawables across a long async load stalls; grabbing more than the layer allows stalls `nextDrawable()`. Less likely the root cause than residency, but relevant if the lockup correlates with stream-in stalls. [Apple docs — authoritative: developer.apple.com/documentation/quartzcore/cametallayer/2938720-maximumdrawablecount](https://developer.apple.com/documentation/quartzcore/cametallayer)

> **Real fix vs diagnostic:** Correct present ordering is a **real fix.**
> `waitUntilScheduled`-before-present patterns are sometimes a workaround but
> the real fix is encoder-scheduled present.

### G. Command queue / command-buffer completion & frame reuse

18. **Reusing a resource across frames before the prior command buffer completes is a hazard/fault path.** Correct patterns: one command buffer per frame, ring-buffered per-frame resources, or signal-on-completion via `MTLSharedEvent`/`addCompletedHandler` before reclaiming CPU-side resources. A translation layer that recycles a transient buffer/texture into the next frame's world-streaming set without completion signaling will fault under real-world load (again, post-Continue). [retrieved: *Moving Cyberpunk 2077 to D3D12*, ACM KPC (D3D12 resource alignment/residency under a large streaming world)](https://dl.acm.org/doi/fullHtml/10.1145/3450623.3464664)
19. **`enqueue()` vs `commit()` ordering, and `waitUntilScheduled()`/`waitUntilCompleted()`** control CPU↔GPU pacing. Overlapping compute/graphics needs explicit split + sync; otherwise GPU sees resources in transit. [Apple docs — authoritative: developer.apple.com/documentation/metal/mtlcommandbuffer](https://developer.apple.com/documentation/metal/mtlcommandbuffer)

> **Real fix vs diagnostic:** Correct completion signaling / ring-buffering is a
> **real fix.**

### H. GPU capture & debug validation (definitive diagnostics)

20. **`MTLCaptureManager` + `MTLCaptureDescriptor` with destination `.developerTools` (Xcode) or `.gpuTraceDocument` (file) and scope `.default`/`.frame`/`.gpuTrace`.** A `.gpuTrace` of the AC6 **Continue→magenta** command buffer, replayed in Metal Debugger / Xcode GPU frame capture, shows the exact faulting draw/dispatch, its resource residency state, and hazards. This is the definitive localizer and is non-invasive (no file edits, no game launch beyond the repro). [Apple docs — authoritative: developer.apple.com/documentation/metal/mtlcapturemanager](https://developer.apple.com/documentation/metal/mtlcapturemanager)
21. **Xcode/Metal "GPU validation" / "Shader validation" scheme diagnostics** flag API misuse at dev time — wrong `MTLResourceUsage`, missing residency, write-after-write — *precisely* the class of bug behind the AC6 fault. These are **diagnostic** but catch the exact root cause cheaply. [Apple docs — authoritative: developer.apple.com/documentation/metal/debugging_tools](https://developer.apple.com/documentation/metal)
22. **GPU counters (`MTLCounter` / `newCounterSampleBuffer`)** give occupancy/memory/vertex-throughput samples per draw — useful to correlate the magenta transition with a residency/scheduling cliff in `src/perf/`. [Apple docs — authoritative: developer.apple.com/documentation/metal/mtlcounter](https://developer.apple.com/documentation/metal)

---

## Root-cause hypothesis for the AC6 magenta + GPU page fault (ranked)

| # | Hypothesis | Why it fits Continue-but-not-menu | Class | Fix type |
|---|------------|-----------------------------------|-------|----------|
| 1 | **Argument-buffer / descriptor-table resources not made resident** (`useResource`/`useHeap`) during world render | Menu uses small directly-bound resident set; world uses descriptor heaps/argument buffers whose referenced resources lack residency | Residency | **Real fix** |
| 2 | **`.untracked` heap resources with missing `MTLFence`/`MTLEvent`** ordering on async compute→graphics (shadows/GI/post) that only runs in-world | Menu has no async-compute hazard chain | Hazard/sync | **Real fix** |
| 3 | **D3D12 resource barriers mis-translated** to Metal residency/hazard changes (e.g., UAV COMMON→GENERIC, render-target transitions) | World flips many RT states per frame | Barrier/residency | **Real fix** |
| 4 | **Drawable presented while backing texture still written** by an in-flight command buffer (present-then-write) | First streamed frame race | Present lifetime | **Real fix** |
| 5 | **Purged heap resources under load** accessed without re-residency | Big world load triggers memory pressure → purge | Residency | **Real fix** |
| 6 | **ICB-referenced resources not resident** during `ExecuteIndirect` replay | World dispatching is indirect/streamed | Residency | **Real fix** |

Solid **magenta** specifically = the GPU wrote a sentinel/garbage value (common
uninitialized or `0xFFFF00FF`-style error color) into the drawable because the
real texels came from unresolved/unmapped memory; the **firmware/MMU lockup** is
the driver/GPU failing to recover from repeated faults at that transition.

---

## Concrete local code areas likely impacted

(From repo `AGENTS.md` + `docs/optimization-roadmap/local-gates.md`; **read-only** — no edits performed.)

**Native engine (C++):**
- `src/metal/` — Metal device/command-queue/pipeline/shader translation. **Encoder wrappers for `useResource`/`useHeap` residency and `MTLResourceUsage`**; `hazardTrackingMode` selection; argument-buffer resource-list encoding. **Primary area for fix #1/#6.**
- `src/d3d/d3d12/` — D3D12 device/command-queue/command-list/resources. **Descriptor heaps → argument buffers** (root signatures, SRV/CBV/UAV tables) and **resource barriers → Metal residency/fence** translation. **Primary area for fix #1/#3.**
- `src/perf/` — shader/pipeline cache, MetalFX upscaler, **GPU profiler** (wire `MTLCounter`/counter-sample-buffer here for fault-localization telemetry).
- `src/runtime/` — crash diagnostics. **Wire `errorOptions = .encoderExecutionStatus` + `encoderInfo`/`errorState` + `MTLCommandBufferErrorDomain` code (`.pageFault`/`.accessFault`) capture into the crash report here** (findings 10–12).
- `include/` — public headers for the above.

**Local graphics gates (must pass before PR; CI cannot run these):**
- `python3 tools/d3d12-metal-sdk/scripts/validate-contracts.py`
- `python3 tools/d3d12-metal-sdk/scripts/validate-probe-matrix.py`
- Probe suites most relevant to the fault class:
  - `--descriptors-only` (argument-buffer / descriptor-table residency — fix #1/#6)
  - `--command-replay-only` (ICB/indirect replay residency — fix #6)
  - `--barriers-render-pass-only` (barrier→residency/fence — fix #3)
  - `--resource-views-formats-only` (usage flags / format — fix #3)

**Backend diagnostic routes (read-only, `127.0.0.1:9274`):**
- `POST /diagnostics/binding-contract/validate` (Phase 5 — root-signature + reflection ABI; **directly exercises argument-buffer residency paths**)
- `POST /diagnostics/command-replay/validate` (Phase 6 — command-list/barrier/visibility; **directly exercises barrier→residency and ICB paths**)
- `GET /diagnostics/cache-doctor?appid=` / `GET /diagnostics/pso-manifests?appid=` (Phase 4 — check the world PSO set the menu never loads)
- `GET /diagnostics/m12/dry-run?appid=` (Phase 3 — M12 artifact/env verification)

These Phase 5/6 probes and the `--descriptors-only` / `--command-replay-only` /
`--barriers-render-pass-only` probes are the **offline localizers** that can
reproduce the residency/barrier conditions without launching AC6.

---

## Recommended investigation sequence (no game launch, no process kills)

1. **Capture-first (diagnostic):** enable `MTLCommandBufferDescriptor.errorOptions = .encoderExecutionStatus` on the M12 command buffer; in `src/runtime/` crash diagnostics, log `encoderInfo[*].label + .errorState` and `error.domain/code` on status `.error`. Repro Continue → read which encoder faulted and whether the code is `.pageFault`/`.accessFault`. *(findings 10–12)*
2. **Capture `.gpuTrace`** of the Continue transition via `MTLCaptureManager` (destination `.gpuTraceDocument`); replay in Metal Debugger / Xcode GPU frame capture to see the faulting draw + its resource residency/hazard state. *(finding 20)*
3. **Run offline probes** that mirror the world paths: `--descriptors-only`, `--command-replay-only`, `--barriers-render-pass-only`; then `POST /diagnostics/binding-contract/validate` and `POST /diagnostics/command-replay/validate` for AC6 appid. *(local-gates.md)*
4. **Apply real fixes** in `src/metal/` (residency + `MTLResourceUsage`) and `src/d3d/d3d12/` (barriers → residency/fence) per the ranked hypothesis table; re-run the probes until green, then re-capture to confirm `.completed` encoder state.

---

## Sources

**Kept (retrieved / directly evidenced):**
- *Task execution on a graphics processor using indirect argument buffers* (Apple, US10657619B2) — https://patents.google.com/patent/US10657619B2/en — argument-buffer mechanism & residency responsibility
- *Indirect command buffers for graphics processing* (Apple, US10789756B2) — https://patents.google.com/patent/US10789756B2/en — ICB state/argument restoration; relevance to replay residency
- *Task execution on a graphics processor using indirect argument buffers* (Apple, US11094036B2) — https://patents.google.com/patent/US11094036B2/en — argument-buffer resource handling
- *GPU internal wait/fence synchronization method and apparatus* (US7755632B2) — https://patents.google.com/patent/US7755632B2/en — internal fence/wait for RAW hazards without pipeline drain (maps to MTLFence)
- *Method and system of a command buffer between a CPU and GPU* (US9235871B2) — https://patents.google.com/patent/US9235871B2/en — "CPU to GPU hazard", fence/enabled-event synchronization
- "Synchronization problems in modern graphics APIs" thesis (research.ou.nl) — https://research.ou.nl/files/45504039/Bruijn_de_D_IM9906_AF_SE_scriptie_Pure.pdf — explicitly covers D3D12 Residency Starter Library + translation-layer complexity
- *Crossfire: Fuzzing macOS cross-XPU memory on Apple Silicon* (ACM 2024) — https://dl.acm.org/doi/abs/10.1145/3658644.3690376 — driver-level GPU memory bugs on macOS Apple Silicon
- *GPUHarbor: testing GPU memory consistency at large* (ACM 2023) — https://dl.acm.org/doi/abs/10.1145/3597926.3598095 — GPU memory-coherence violations (Metal API)
- *Moving Cyberpunk 2077 to D3D12* (ACM KPC) — https://dl.acm.org/doi/fullHtml/10.1145/3450623.3464664 — D3D12 resource alignment/residency under a large streaming world
- *Master Photo and Video Editing with Metal* (Springer chapter) — https://link.springer.com/content/pdf/10.1007/979-8-8688-0832-6.pdf — encoder/command-buffer/GPU model in Metal
- Local repo: `docs/optimization-roadmap/local-gates.md` — D3D12 Metal SDK probe matrix + Phase 1–8 backend diagnostic routes

**Kept (Apple docs — authoritative primary references for direct verification; canonical URLs, full text not machine-retrieved this run — see Gaps):**
- Resource fundamentals / residency — https://developer.apple.com/documentation/metal/resource_fundamentals
- `MTLHeap` — https://developer.apple.com/documentation/metal/mtlheap
- `MTLHazardTrackingMode` — https://developer.apple.com/documentation/metal/mtlhazardtrackingmode
- `MTLResourceUsage` — https://developer.apple.com/documentation/metal/mtlresourceusage
- `MTLRenderCommandEncoder` (useResource/useHeap) — https://developer.apple.com/documentation/metal/mtlrendercommandencoder
- `MTLCommandBuffer` / error — https://developer.apple.com/documentation/metal/mtlcommandbuffer and https://developer.apple.com/documentation/metal/mtlcommandbuffer/error
- `MTLCommandBufferDescriptor` (errorOptions) — https://developer.apple.com/documentation/metal/mtlcommandbufferdescriptor
- `MTLCommandBufferEncoderInfo` / errorState — https://developer.apple.com/documentation/metal/mtlcommandbufferencoderinfo
- Synchronization / `MTLEvent` / `MTLSharedEvent` — https://developer.apple.com/documentation/metal/synchronization , https://developer.apple.com/documentation/metal/mtlfence , https://developer.apple.com/documentation/metal/mtlsharedevent
- `CAMetalLayer` / `MTLDrawable` — https://developer.apple.com/documentation/quartzcore/cametallayer , https://developer.apple.com/documentation/metal/mtldrawable
- `MTLCaptureManager` — https://developer.apple.com/documentation/metal/mtlcapturemanager
- Metal debugging tools — https://developer.apple.com/documentation/metal
- Archived *Metal Programming Guide* (classic) — https://developer.apple.com/library/archive/documentation/Miscellaneous/Conceptual/MetalProgrammingGuide/

**Dropped:**
- MetalLB Kubernetes Docker Hub images — false matches for "metal"
- General metallurgy / metal-enrichment arXiv papers — irrelevant
- Generic buffer-sizing / page-cache-eviction arXiv papers — not GPU-specific
- WebGPU / WebRTC / MDN generic results — unrelated

---

## Gaps

- **The web search index could not retrieve Apple developer-documentation full text.** All "Metal"-worded queries were mis-routed to MetalLB (Kubernetes), metallurgy papers, or Docker Hub; `general` category returned zero results for Apple-doc queries. Apple-doc URLs above are the **real canonical pages** (stable paths) for direct verification, and the API semantics stated are corroborated by the *retrieved* Apple patents + the D3D12-residency thesis + the Apple-Silicon GPU-memory fuzzing papers — but no direct Apple-doc quotes were machine-verified this run.
- **Exact `MTLCommandBufferErrorOption` / `MTLCommandBufferEncoderErrorState` symbol names and the numeric `error` codes** should be confirmed verbatim against the live Apple doc pages before committing API-surface changes in `src/metal/` and `src/runtime/`.
- **DXMT/MoltenVK upstream issue trackers** (residency/page-fault precedents) could not be reached via search; recommend grepping the DXMT/DXVK/MoltenVK repos directly for `useResource`, `makeResident`, `hazardTrackingMode`, and `encoderExecutionStatus` precedents — these translation layers have hit exactly this fault class before.
- No in-tree read of `src/metal/` and `src/d3d/d3d12/` file contents was performed (directory reads are not supported by the available tools, and the task forbade edits). The local-code-area pointers are derived from `AGENTS.md` + `local-gates.md`; exact symbols to patch (e.g., the encoder `useResource` call sites, the descriptor-heap→argument-buffer path, the barrier-translation function) should be located via grep before editing.

---

## Supervisor coordination

Not required. Not blocked: the brief is complete and self-contained; the only
external dependency is human verification of exact Apple symbol names (recorded
in **Gaps**) and an in-tree grep of `src/metal/` + `src/d3d/d3d12/` to pin exact
edit sites before any fix. No decision needed from the supervisor to proceed.
