# Research: D3D12 Runtime Layering & D3D12→Metal Translation Architecture

*Target: inform M12 / `libm12core` architecture and the post-Continue AC6 GPU-lockup failure.
Scope: general D3D12Core/Agility separation, command recording vs execution, runtime vs driver responsibilities, and how GPTK/D3DMetal/DXMT-style layers divide PE-shim vs native-core work. No private code is reproduced; this is a synthesis of public + academic sources plus standard Wine/Metal architecture.*

---

## Summary

On Windows, D3D12 is a strictly layered stack: a thin `d3d12.dll` API front, a heavyweight `D3D12Core.dll` "core" device implementation (the piece the **Agility SDK** redistributes ahead of the OS), a debug/GPU-based-validation layer, the vendor **User-Mode Driver (UMD)**, and the kernel-mode **DirectX Graphics Kernel (`Dxgkrnl`)** that owns scheduling, residency, and **TDR** (timeout detection & recovery). A D3D12→Metal translation layer like GPTK's `d3dmetal`/DXMT collapses **all of D3D12Core + UMD + scheduler into one Mach-O core** behind a thin Wine **PE shim** that exists solely to be a COM-compatible `d3d12.dll`/`D3D12Core.dll` import for the Windows process. Because there is no `Dxgkrnl` and no TDR on macOS, the translation core must itself own descriptor lifetime, resource-state/barrier modeling, in-flight tracking, and a hang watchdog — the pillars that most likely decide whether a scene transition like AC6's "Continue" survives.

---

## The Windows reference stack (app → silicon)

The layered model is what every translation layer implicitly reconstructs:

| # | Layer | Owns | Notes |
|---|-------|------|-------|
| 1 | App | API calls | `ID3D12Device`, command lists, descriptor heaps |
| 2 | `d3d12.dll` (API front) | COM export surface, `D3D12CreateDevice`, loads debug layer | Thin shim; forwards to D3D12Core |
| 3 | **D3D12Core.dll** | Device, queue, command list/allocator, PSO, descriptor heaps, resource creation, fence coordination | This is what the **Agility SDK** ships as a redistributable so apps run a newer runtime than the OS |
| 4 | Debug / GPU-Based Validation | API + state validation, barrier legality, on-GPU checks | Optional, sits around D3D12Core |
| 5 | **User-Mode Driver (UMD)** | DDI consumers, shader compile (DXIL→ISA), command-buffer gen, residency hints | Vendor DLL |
| 6 | **`Dxgkrnl` (DxgKernel)** | Allocation, scheduling, sync primitives, **TDR**, paging | WDDM kernel component |
| 7 | Kernel-Mode Driver (KMD) | Hardware-specific kernel piece | Vendor |

The split that matters for translation is **#3 ⇄ #5 (D3D12Core ⇄ UMD)**: that is the seam a Metal translator takes over wholesale, because on macOS there is no real #5/#6/#7 visible to userspace — Metal *is* the API, and the closed macOS GPU driver sits behind IOKit.

---

## Findings

### 1. The Agility SDK = a redistributable D3D12Core that ships next to the app

The DirectX 12 "Agility SDK" is simply a newer `D3D12Core.dll` (+ matching thin `d3d12.dll`) that an application ships in its own directory and selects by exporting `D3D12SDKVersion`/`D3D12SDKPath`. The architectural lesson is that the *entire device implementation is a separable, replaceable binary* behind the stable COM API surface of `d3d12.dll`. A translation layer exploits the same seam: it implements the `ID3D12*` interfaces directly, so to the app it is indistinguishable from a real D3D12Core. [Performance & Architectural Study of Direct3D 11 and Direct3D 12](https://vincentvd.com/wp-content/uploads/2023/03/gw_2223_vincent_vandenberghe_en_paper.pdf); [Parallel game programming (dynamically linking d3d12.dll, ID3D12Device creates all interfaces)](https://upcommons.upc.edu/entities/publication/a2517c66-8fc3-4eaf-a527-7a9a99ec65d4).

### 2. Command *recording* is CPU-side and immediate; *execution* is the replay boundary

In D3D12 there is **no immediate context** (unlike D3D11): every command is recorded into an `ID3D12CommandAllocator` via an `ID3D12GraphicsCommandList`, then submitted to an `ID3D12CommandQueue` via `ExecuteCommandLists`. Recording writes into a CPU buffer instantly; the GPU only acts at `ExecuteCommandLists`. This deliberately decouples "what the app said" from "what the GPU does," which is exactly what lets a translation layer **buffer, reorder, validate, and replay** the recorded stream into the target API. Bundles are reusable sub-lists replayed inside other lists via `ExecuteBundle`, which maps naturally to Metal's *indirect command buffers* and to "record once, replay every frame with one call." [Synchronization problems in modern graphics APIs (EXECUTECOMMANDLISTS, fences, 64-bit values)](https://research.ou.nl/files/45504039/Bruijn_de_D_IM9906_AF_SE_scriptie_Pure.pdf); [Video streaming augmentation with modern graphics API (Bundle command lists, delayed playback/seek/pause)](https://is.muni.cz/th/q2lmq/); [An exploratory study of high-performance graphics APIs (record a command buffer and replay it every frame)](https://scholar.utc.edu/theses/446/); [Indirect command buffers for graphics processing (encode/execute indirect command buffers on GPU)](https://patents.google.com/patent/US10789756B2/en).

### 3. The allocator-must-not-reset hazard is the canonical lifetime rule

While the GPU is executing a command list, the backing **allocator must not be reset**; the app must wait on a fence value first. Resetting early means the GPU reads from recycled/uninitialized command memory — a direct path to a GPU fault. The same lifetime discipline must be enforced by the translation core for *every* object a recorded stream references (not just the allocator). [Performance & Architectural Study of D3D11/D3D12 (CPU-GPU sync, per-frame fence, "if GPU hasn't caught up, wait one frame")](https://vincentvd.com/wp-content/uploads/2023/03/gw_2223_vincent_vandenberghe_en_paper.pdf).

### 4. Runtime vs driver: runtime tracks *state*, driver does *work*; translator owns both

The runtime (D3D12Core) is responsible for API object lifetimes, descriptor-heap bookkeeping, pipeline-state compilation coordination, command-list recording/translation, and the validation layer. The driver (UMD+KMD) owns actual allocation, shader→ISA compilation, command-buffer generation, residency, scheduling, paging, page-fault handling, and **TDR**. WDDM is explicitly structured as *user-mode* (UMD, in-process) + *kernel-mode* (`Dxgkrnl` scheduler + KMD), with the kernel owning scheduling and recovery. A Metal translator has **no** `Dxgkrnl` to lean on: it is simultaneously runtime + UMD + scheduler, so every "driver" responsibility — including hang detection — becomes its own job. [Understanding the virtualization tax of scale-out pass-through GPUs (WDDM = user-mode + kernel-mode; UMD interacts with DxgKernel)](https://ieeexplore.ieee.org/abstract/document/7056038/); [Graphics compute process scheduling (user-mode↔kernel-mode transitions; WDDM; DirectX graphics kernel subsystem)](https://patents.google.com/patent/US9176794B2/en); [Apparatus for efficient GPU processing in a virtual execution environment (command streamer; driver software translates API calls; pipeline flush required immediately)](https://patents.google.com/patent/US9996892B2/en).

### 5. TDR is an OS-level watchdog that a translator must reimplement

On Windows, when a GPU command exceeds the timeout, the OS **TDR** mechanism resets the device. Translation layers on macOS have no such watchdog: a hung `MTLCommandBuffer` simply never completes, and without an explicit per-buffer timer the whole process locks up. This is *the* reason a real-world GPU lockup on Metal is catastrophic where the same bug on Windows would produce a recoverable TDR. [System and method for long-running compute using buffers as timeslices (TDR = OS mechanism to detect when a GPU has hung)](https://patents.google.com/patent/US20130162661A1/en); [Prevention of DoS attack by a rogue graphics application (TDR; repetitive GPU hangs indicate hardware not recovering)](https://patents.google.com/patent/US8872835B2/en); [Sugar: Secure GPU acceleration in web browsers (commands that hang the GPU trigger a TDR)](https://dl.acm.org/doi/abs/10.1145/3296957.3173186).

### 6. Resource barriers model state explicitly — Metal has none, so the core must synthesize them

D3D12 forces the app to emit `ResourceBarrier`s to declare state transitions (e.g. `RENDER_TARGET`→`PIXEL_SHADER_RESOURCE`), UAV barriers, and aliasing barriers for placed/reserved resources sharing memory. **Metal has no explicit inter-pass barrier**; it relies on render-pass load/store actions, texture-usage flags, and blit syncs. Therefore the translation core must keep a per-subresource logical-state machine and *insert* Metal-side synchronization (blit encoder flush, `MTLFence`/`MTLEvent`, or a forced render-pass boundary) wherever D3D12 semantics demand it. The hazard is explicit and well documented: "a missing or over-broad barrier can" corrupt execution, and "barriers will transition the state of the resources or alias the different resources on the placed or reserved resources." Getting this wrong manifests as data races, reads of uninitialized memory, or write-after-write corruption — textbook GPU-hang triggers. [Synchronization problems in modern graphics APIs (runtime tracks resource state; resolve aliasing by remembering which resource)](https://research.ou.nl/files/45504039/Bruijn_de_D_IM9906_AF_SE_scriptie_Pure.pdf); [Parallel game programming (barriers transition state / alias placed & reserved resources)](https://upcommons.upc.edu/entities/publication/a2517c66-8fc3-4eaf-a527-7a9a99ec65d4); [Xylem: Comparative Analysis of GPU Dispatch Pipelines (explicit-API hazard: missing/over-broad barrier)](https://digitalcommons.calpoly.edu/theses/3294/); [Modern Graphics APIs: Design Principles (heaps, views, descriptors; updating resources after binding while enforcing validity)](https://www.zte.com.cn/content/dam/zte-site/res-www-zte-com-cn/mediares/magazine/publication/com_en/article/en202601/20260113.pdf).

### 7. Descriptor heaps are GPU-visible tables the core must keep alive as long as the GPU reads them

D3D12 descriptors live in heaps; shader-visible (CBV_SRV_UAV / Sampler) heaps are read by the GPU during draws. A descriptor is just a handle pointing at a resource; **the resource it points at must outlive every in-flight reference**. A translation layer maps this to Metal **argument buffers** (bindless) or to per-draw `setVertexTexture:`/`setVertexBuffer:` calls. The crucial ownership rule: the core must keep a strong ref on every Metal object referenced by any recorded-but-unexecuted or in-flight command buffer; `Release()` of the D3D12 object only drops the *core's* ref, not the in-flight tracker's. "There is a large descriptor table for 'bindless' resource[s]" and "GPU-visible-only memory … currently being referenced by executing GPU commands" — exactly the lifetime window where a post-scene-transition hang lives. [Moving Cyberpunk 2077 to D3D12 (bindless descriptor table; aliasing model; "effective tools are scarce")](https://dl.acm.org/doi/fullHtml/10.1145/3450623.3464664); [DirectX 12 and Real-Time Ray Tracing in DXR (descriptor heap = GPU memory portion for descriptors; root signature = function signature; fence signal/wait)](https://link.springer.com/chapter/10.1007/979-8688-1691-8_9); [Using bindless resources with DirectX raytracing (descriptor table referenced by executing GPU commands; GBV-style tracking)](https://link.springer.com/content/pdf/10.1007/978-1-4842-7185-8_17); [VanityX: root signature implements core binding behavior + class linkage; explicit CPU-GPU sync](https://www.mdpi.com/2076-3417/13/9/5468).

### 8. PE shim vs native core is the canonical Wine architecture — not an optimization, a *necessity*

Metal frameworks are Mach-O / Unix-only and **cannot be loaded into a PE (Windows) module**. So the translation layer is split: a Windows-side PE DLL (the Wine `d3d12.dll`/`D3D12Core.dll` override) that presents COM `IUnknown` vtables and marshals arguments across the **Wine PE↔Unix boundary** (`__wine_unix_call` / `ntdll` thunks; WoW64 thunks for 32-bit PE clients), and a Unix/Mach shared library that links `Metal`/`MetalKit`/`IOSurface` and does all real work. This is the documented Wine model: "DLLs are implemented by Wine as a Unix shared library," and NTDLL is replaced by its Unix implementation. MetalSharp's `winemetal.so` (x86_64-unix only, WoW64 thunks for i386 PE) is exactly this seam, and DXMT's `x86_64-windows/` PE DLLs + `x86_64-unix/` sidecars follow the same pattern. [A survey on virtualization technologies (replace NTDLL.DLL with Wine Unix implementation; DLLs as Unix shared libraries)](http://132.248.181.216/MV/CursoMaquinasVirtuales/Bibliograf%C3%ADaMaquinasVirtuales/VirtualizationSurveyTR179.pdf); [A tale of two standards (NTDLL core of Win32; Wine includes winelib Linux shared library)](https://books.google.com/books?hl=en&lr=lang_en&id=q9GnNrq3e5EC); [Defending against return-oriented programming (Wine provides alternative DLL implementations for Unix)](https://search.proquest.com/openview/bbb8f34f4778704c07fb43679591ba31/1).

### 9. The GPTK / D3DMetal and VKD3D-Proton precedents confirm the "collapsed core" design

`vkd3d` / `vkd3d-proton` is the canonical precedent: it "translates Direct3D 12 calls to Vulkan, enabling cross-platform" execution — i.e. one library is both the D3D12Core *and* the UMD against a target low-level API. Apple's Game Porting Toolkit applies the identical pattern against Metal (`d3dmetal`/`libd3dshared`), and DXMT does the same against Metal for D3D11/D3D12, each with an explicit **ExecuteCommandLists replay engine** that consumes the recorded stream and emits target-API encoder work. The replay engine is the single most important component for diagnosability: it is the place where the recorded stream can be validated, reordered, captured, and re-emitted in isolation. [Programming Interfaces for Cross-platform Image Rendering (VKD3D translates Direct3D 12 calls to Vulkan)](https://ieeexplore.ieee.org/abstract/document/10315835/); [Platform leadership strategy for ARM adoption (Wine + DXVK + VKD3D-Proton covering D3D12)](https://lutpub.lut.fi/handle/10024/172209); [Mastering Graphics Programming with Vulkan (MoltenVK = translation layer into Metal)](https://sciendo.com/2/v2/download/book/9781803230207.pdf).

### 10. Metal's command-buffer/encoder model is the execution substrate the core targets

Metal centers on `MTLCommandQueue` → `MTLCommandBuffer` → a sequence of **command encoders** (render/compute/blit); "Metal signals the app when command buffers finish execution," and an encoder is committed to a command buffer whose state changes are expensive. The translation core's replay engine is essentially: per D3D12 command list → allocate `MTLCommandBuffer`, open the right encoder type, translate recorded draw/dispatch/copy/state into encoder calls, `endEncoding`, `commit`, and gate reset on the completion handler / shared event. [Working with Metal: Overview (command encoders generate commands; Metal signals app on completion)](http://f3smartdevices.pbworks.com/w/file/fetch/81879608/603%20-%20Working%20with%20Metal%3A%20Overview.pdf); [Working with Metal: Advanced (commandBuffer = [commandQueue commandBuffer]; create and submit)](https://docs.huihoo.com/apple/wwdc/2014/605_working_with_metal_advanced.pdf); [Metal programming guide (command buffer = central control object; encoder committed to command buffer; expensive state change)](https://books.google.com/books?hl=en&lr=lang_en&id=A55BDwAAQBAJ).

### 11. Validation + IR capture are the only way to triage a lockup with "scarce tools"

CDPR's D3D12 porting account is blunt: reproducing a GPU crash is painful and "effective tools are scarce"; they rely on D3D12's built-in debug layer for some errors and **GPU-based validation (GBV)** for others, with "resource states … tracked and managed entirely within" their abstraction. On macOS there is no D3D12 debug layer and no GBV, so the translation core must ship its own equivalent: an API-layer checked build (argument validity, heap bounds, barrier legality), a subresource-state tracker mirroring GBV, an in-flight/lifetime tracker asserting no object is destroyed while referenced, a per-command-buffer **hang watchdog** (the macOS TDR), and a **command-IR capture** facility so the failing stream can be replayed minimally. Capture/replay of an API stream is a well-trodden technique ("recorded to corresponding and individual capture streams"; "multi-threaded API stream replay"). [Moving Cyberpunk 2077 to D3D12 (debug layer + GBV; resource states tracked in abstraction; scarce tools)](https://dl.acm.org/doi/fullHtml/10.1145/3450623.3464664); [Method and system for implementing a multi-threaded API stream replay (capture streams; multi-threaded replay)](https://patents.google.com/patent/US9477575B2/en); [GPU-based runtime verification (GPU-based monitoring lowers overhead vs CPU)](https://ieeexplore.ieee.org/abstract/document/6569882/).

---

## Mapping to M12 / `libm12core` — what belongs where, and why

| Component | Lives in | Responsibilities | Why here, not elsewhere |
|-----------|----------|------------------|--------------------------|
| **PE shim** (`x86_64-windows/` DLL: `d3d12.dll`/`D3D12Core.dll`/`dxgi.dll`) | Wine PE module inside the Windows process | COM `IUnknown`/`ID3D12*` vtables; `D3D12CreateDevice`/factory entry points; argument marshalling across the PE↔Unix boundary; WoW64 thunks for 32-bit PE clients; refcount bookkeeping visible to the PE world | Metal frameworks can't load into a PE; the shim exists only to be a COM-compatible import target. Keep it *thin* — any real logic here is un-debuggable and un-instrumentable from the Unix side. |
| **Native core** (`libm12core`, x86_64-unix `.so`/dylib) | Unix/Mach side, links `Metal`/`IOSurface` | Owns `MTLDevice`/`MTLCommandQueue`; all `MTLBuffer`/`MTLTexture`/`MTLHeap` allocation; shader translation (DXIL/SM6 → Metal IR / `MTLLibrary`); PSO → `MTLRenderPipelineState`/`MTLComputePipelineState`; **descriptor heap → Metal argument buffers**; fence→`MTLSharedEvent` mapping; **resource-state machine + barrier synthesis**; residency | This is the collapsed D3D12Core+UMD. It is the only place that can talk to Metal, so all correctness-critical ownership (lifetime, state, sync) must live here. |
| **Command replay engine** (sub-module of native core) | Unix side | Consumes the recorded D3D12 stream from `ID3D12CommandAllocator`; maintains an intermediate command IR; on `ExecuteCommandLists` translates IR → `MTLCommandBuffer`+encoder calls; handles bundle inlining; supports **capture/replay** and **isolated single-stream replay** | Recording↔execution is the explicit seam D3D12 exposes; centralizing translation here makes validation, reorder, and capture possible — essential for lockup triage. |
| **Descriptor & lifetime ownership** (core subsystem) | Native core | Per-descriptor strong refs on referenced Metal objects held by an **in-flight tracker** until the owning command buffer completes; allocator-reset gating on fence; shader-visible heap → argument-buffer flushing before each draw; aliasing-barrier bookkeeping for placed/reserved resources | Dangling descriptors (resource freed before GPU reads the table) and premature allocator resets are the most common silent causes of GPU faults; centralizing ownership makes the invariant enforceable. |
| **Validation & instrumentation** (core subsystem, checked build) | Native core | API-layer checks (heap bounds, null descriptors, barrier legality); **subresource-state tracker (GBV-equivalent)**; in-flight/lifetime assertions; **hang watchdog** (per-`MTLCommandBuffer` timer → dump IR + force device reset); Metal capture (`MTLCaptureManager`) + counters (`MTLCounterSampleBuffer`) integration | No D3D12 debug layer, no GBV, no TDR on macOS; without these the core is opaque and lockups are unrecoverable rather than diagnosable. |

### Ownership invariants the core must enforce (checklist)

1. **Allocator reset** ⟵ only after the fence value for every submitted list from that allocator has completed.
2. **Resource lifetime** ⟵ `Release()` drops the core ref; the *in-flight tracker* keeps the Metal object alive until all referencing command buffers complete.
3. **Descriptor validity** ⟵ a shader-visible descriptor table must be fully flushed/synced (argument-buffer encoded) before any draw that consumes it; bounds-checked on the checked build.
4. **Resource state** ⟵ every transition recorded in the state machine; a missing or over-broad barrier is a validation error, not a silent corruption.
5. **Aliasing** ⟵ `MakeResident`/alias transitions force a pipeline flush + blit sync before the aliased resource is used.
6. **Hang watchdog** ⟵ every committed `MTLCommandBuffer` carries a deadline; expiry dumps the IR up to the hang and resets the device (the macOS TDR).

---

## Why the post-Continue AC6 lockup fits this model

The reported failure — title/menu render fine, then magenta screen + irrecoverable GPU/MMU/firmware lockup immediately after selecting **Continue**, with high-intensity glitching — is the signature of a correctness bug in the *lifetime/state* pillars exposed at the **first heavy scene render**, not a rendering-quality bug. Specifically, the first world/scene transition is the first time the app simultaneously exercises:

- the first large **shader-visible descriptor heap** binding many resources at once (⟵ invariant 3 — a descriptor whose backing resource was already `Release`d, or whose argument buffer wasn't flushed, reads freed/uninitialized `MTLTexture` and faults the GPU);
- the first batch of **render-target ↔ SRV / depth-write ↔ depth-read transitions** and the first **UAV barrier** (⟵ invariant 4 — a missing or wrong-direction transition gives a write-after-write or read-of-uninitialized hazard);
- the first **placed/reserved resource reuse / aliasing** in the world scene (⟵ invariant 5 — an aliasing barrier missing before reuse corrupts memory);
- the first **command-allocator reset** racing an executing list (⟵ invariant 1 — GPU reads recycled command memory).

Because macOS has **no TDR** (finding 5), any one of these — which on Windows would surface as a recoverable device-removal/TDR — instead becomes an *irrecoverable* process-wide lockup. That is precisely why the recommended architecture puts (a) a subresource-state tracker and (b) an in-flight/lifetime tracker and (c) a hang watchdog with IR capture into `libm12core`: they convert an opaque, unrecoverable hang into a captured, replayable, assertable failure at the exact point it occurs. The replay engine (finding 9) then lets the failing command stream be re-emitted in isolation to localize the offending transition/descriptor/allocation without re-driving the whole game to the Continue screen.

---

## Sources

### Kept (directly cited)
- **Synchronization problems in modern graphics APIs** (research.ou.nl) — D3D12 internals, the validation library, fences as 64-bit values, `EXECUTECOMMANDLISTS`, aliasing resolved by remembering source resource. Core reference for runtime state tracking. https://research.ou.nl/files/45504039/Bruijn_de_D_IM9906_AF_SE_scriptie_Pure.pdf
- **Performance and Architectural Study of Direct3D 11 and Direct3D 12** (vincentvd.com) — command list/queue/allocator, per-frame fence CPU-GPU sync, barrier-of-entry analysis. https://vincentvd.com/wp-content/uploads/2023/03/gw_2223_vincent_vandenberghe_en_paper.pdf
- **Moving Cyberpunk 2077 to D3D12** (ACM/CDPR) — resource-state tracking in `GpuApi`, debug layer + GPU-based validation, aliasing model, bindless descriptor table, "effective tools are scarce." Industry-grade corroboration of the validation-pillar thesis. https://dl.acm.org/doi/fullHtml/10.1145/3450623.3464664
- **Parallel game programming** (UPCommons) — `ID3D12Device`, dynamically linking `d3d12.dll`, barriers transitioning state and aliasing placed/reserved resources. https://upcommons.upc.edu/entities/publication/a2517c66-8fc3-4eaf-a527-7a9a99ec65d4
- **Modern Graphics APIs: Design Principles, A Use Case, and New Perspectives** (ZTE Comms, 2026) — heaps/views/descriptors, updating resources after binding while enforcing validity. https://www.zte.com.cn/content/dam/zte-site/res-www-zte-com-cn/mediares/magazine/publication/com_en/article/en202601/20260113.pdf
- **DirectX 12 and Real-Time Ray Tracing in DXR** (Springer, 2026) — descriptor heap = GPU memory for descriptors; root signature as function signature; fence signal/wait. https://link.springer.com/chapter/10.1007/979-8-8688-1691-8_9
- **Using bindless resources with DirectX raytracing** (Springer) — GPU-visible-only descriptor table referenced by executing commands; GBV-style tracking. https://link.springer.com/content/pdf/10.1007/978-1-4842-7185-8_17
- **VanityX: an agile 3D rendering platform supporting mixed reality** (MDPI) — root signature core behavior + class linkage; explicit CPU-GPU sync. https://www.mdpi.com/2076-3417/13/9/5468
- **Video streaming augmentation with modern graphics API** (Masaryk U) — Bundle command lists, delayed playback/seek/pause. https://is.muni.cz/th/q2lmq/
- **Understanding the virtualization tax of scale-out pass-through GPUs in GaaS clouds** (IEEE) — WDDM = user-mode + kernel-mode; UMD ↔ DxgKernel. https://ieeexplore.ieee.org/abstract/document/7056038/
- **Graphics compute process scheduling** (Google Patents) — user↔kernel mode transitions; WDDM; DirectX graphics kernel subsystem. https://patents.google.com/patent/US9176794B2/en
- **System and method for long running compute using buffers as timeslices** (Google Patents) — TDR definition; WDDM signals UMD. https://patents.google.com/patent/US20130162661A1/en
- **Prevention of DoS attack by a rogue graphics application** (Google Patents) — TDR; repetitive GPU hangs. https://patents.google.com/patent/US8872835B2/en
- **Sugar: Secure GPU acceleration in web browsers** (ACM) — GPU-hang commands trigger TDR. https://dl.acm.org/doi/abs/10.1145/3296957.3173186
- **A survey on virtualization technologies** (TR179) — Wine replaces NTDLL with Unix impl; DLLs as Unix shared libraries. http://132.248.181.216/MV/CursoMaquinasVirtuales/Bibliograf%C3%ADaMaquinasVirtuales/VirtualizationSurveyTR179.pdf
- **A tale of two standards** (Google Books) — NTDLL as Win32 core; Wine's winelib shared library. https://books.google.com/books?hl=en&lr=lang_en&id=q9GnNrq3e5EC
- **Defending against return-oriented programming** (ProQuest) — Wine provides alternative DLL implementations on Unix. https://search.proquest.com/openview/bbb8f34f4778704c07fb43679591ba31/1
- **Programming Interfaces for Cross-platform Image Rendering and Deep Learning GPGPU** (IEEE) — VKD3D translates Direct3D 12 calls to Vulkan. https://ieeexplore.ieee.org/abstract/document/10315835/
- **Platform leadership strategy for ARM adoption** (LUT) — Wine + DXVK + VKD3D-Proton covering D3D12. https://lutpub.lut.fi/handle/10024/172209
- **Mastering Graphics Programming with Vulkan** (Sciendo) — MoltenVK = translation layer into Metal. https://sciendo.com/2/v2/download/book/9781803230207.pdf
- **Working with Metal: Overview** (WWDC 2014 PDF) — command encoders; Metal signals app on command-buffer completion. http://f3smartdevices.pbworks.com/w/file/fetch/81879608/603%20-%20Working%20with%20Metal%3A%20Overview.pdf
- **Working with Metal: Advanced** (WWDC 2014 PDF) — `commandBuffer = [commandQueue commandBuffer]`; create/submit. https://docs.huihoo.com/apple/wwdc/2014/605_working_with_metal_advanced.pdf
- **Indirect command buffers for graphics processing** (Google Patents) — encode/execute indirect command buffers on GPU (analogue of D3D12 bundles). https://patents.google.com/patent/US10789756B2/en
- **Method and system for implementing a multi-threaded API stream replay** (Google Patents) — capture streams; multi-threaded API replay. https://patents.google.com/patent/US9477575B2/en
- **An exploratory study of high-performance graphics APIs** (UTC thesis) — record a command buffer and replay it each frame with one call. https://scholar.utc.edu/theses/446/
- **Apparatus and method for efficient graphics processing in a virtual execution environment** (Google Patents) — command streamer; driver translates API calls; pipeline flush. https://patents.google.com/patent/US9996892B2/en
- **GPU-based runtime verification** (IEEE) — GPU-side monitoring lowers overhead vs CPU. https://ieeexplore.ieee.org/abstract/document/6569882/
- **Xylem: A Comparative Analysis of GPU Dispatch Pipelines** (CalPoly thesis) — explicit-API hazard: missing/over-broad barrier. https://digitalcommons.calpoly.edu/theses/3294/

### Dropped
- *Performance Comparison on Parallel CPU/GPU Algorithms (Unified Gas-Kinetic Scheme)*, *Direct3D-S2 (3D generation)*, *AES via Direct3D 10*, *WebGlitch (WebGPU)* — off-topic (HPC/generative-AI/crypto), included only incidentally in result sets.
- *MDN/Docker "SDK/runtime/download/toolkit" matches* — the `it`/general search categories returned MDN web-extension pages and Docker Hub images for DirectX-adjacent keywords; not sources on D3D12 architecture.

---

## Gaps

- **GPTK `d3dmetal` / `libd3dshared` internals** — Apple ships no public architecture document; the PE↔Unix split and the exact replay-engine design are inferred from the Wine+Metal necessity (finding 8) and from the VKD3D-Proton precedent (finding 9), not from Apple primary docs. A targeted read of the GPTK Wine prefix layout (when accessible on a dev machine) would confirm the boundary.
- **DXMT / `dxmt-m12` source specifics** — the repo's `x86_64-windows/` + `x86_64-unix/` split and its `ExecuteCommandLists` replay engine are referenced from project context, but the in-repo implementation details (descriptor-table → argument-buffer mapping, state-tracker granularity, watchdog presence) were not independently fetched here. Recommend a focused read of `dxmt-m12` source for: (1) the replay-engine entry point, (2) the in-flight/lifetime tracker, (3) whether a hang watchdog already exists.
- **Microsoft D3D12 primary docs** — `learn.microsoft.com` D3D12 programming-guide pages (command lists, descriptor heaps, resource barriers, fences) did not surface via the available search backend in this pass; the behavioral facts are corroborated by the academic theses above but the canonical MS pages would strengthen the "what the runtime guarantees" claims.
- **AC6-specific reproduction** — this brief is architecture-only; it does not confirm *which* of the four invariants (lifetime, state, aliasing, allocator-reset) is the actual AC6 trigger. That requires IR capture at the Continue transition (see the "next steps" below).

### Suggested next steps
1. Read the `dxmt-m12` replay engine + lifetime tracker in-repo; confirm a hang watchdog exists (or is missing) — a *missing* watchdog directly explains the "irrecoverable" nature of the AC6 lockup.
2. Add a **checked-build** path through `libm12core` that asserts the six ownership invariants above; run AC6 to Continue under it to catch the violation at its source.
3. Implement **command-IR capture** around the Continue transition so the failing stream can be replayed in isolation, turning the irrecoverable hang into a deterministic, reproducible unit.
4. Cross-check the in-repo state-tracker granularity against D3D12 GBV semantics (subresource-level, including implicit promotions/decays) — a coarser tracker is a likely source of "missing/over-broad barrier" faults at scene transitions.

---

## Supervisor coordination

No decision needed; this is a self-contained research brief. The only cross-cutting dependency worth flagging to the parent: the highest-leverage follow-up is an in-repo read of `dxmt-m12` (replay engine + lifetime tracker + watchdog presence), which a code-exploration subagent should run next to convert these architectural inferences into concrete findings about the actual AC6 failure path.
