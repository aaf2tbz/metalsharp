# Research: D3D12 resource lifetime / command replay on non-D3D backends

Scope: how vkd3d-proton, DXVK, DXMT, D3DMetal/GPTK, MoltenVK/Metal, WebGPU/wgpu,
IREE, and Chromium/Dawn model (a) keeping D3D12 resources alive across command
lists and submissions without over-retaining, (b) capturing descriptor/view data
at recording time, and (c) modeling command buffer completion. All findings are
filtered through the lens of what is directly useful for a D3D12-to-Metal
engine (the project's M12 surface).

## Summary

The cross-API consensus is a **per-command-buffer/per-submission resource
tracking set**: every recorded operation appends the resources it touches to a
list keyed by the submission's fence value, and a fence-completion callback
retires (unrefs) those resources. vkd3d-proton, DXVK, wgpu, MoltenVK, and IREE
all converge on this. On Metal specifically, there are two complementary escape
hatches — (1) `MTLResource`'s automatic hazard tracking (which D3DMetal and DXMT
lean on heavily) and (2) `MTLFence`/`MTLSharedEvent` for cross-encoder and
cross-command-buffer ordering that the auto-tracker cannot express (ExecuteIndirect,
untracked heap resources, UAV clears, aliasing). The biggest engineering lessons
are: (a) D3D12 descriptor views have no lifetime of their own so descriptor
*contents* must be captured (deep-copied) at record time; (b) Metal has no
native D3D12 command signature equivalent, so `ExecuteIndirect` requires a custom
batched emulation (vkd3d-proton 3.0.1 just shipped exactly such a system); (c)
the legacy D3D12 "promotion and decay" rules are a major correctness hazard
that every translation layer reimplements carefully (or pushes users to
Enhanced Barriers to avoid).

## Findings

### Resource lifetime across command lists / submissions

1. **DXVK guideline: deferred context state restoration pessimizes lifetime
   tracking** — the DXVK Developer Guidelines wiki explicitly warns that
   "fully restoring context state is expensive on the CPU and may pessimize
   resource lifetime tracking," and that DXVK will expose native command list
   support rather than faithfully replaying deferred contexts. Implication: a
   translation layer should *not* try to faithfully capture+replay D3D11-style
   deferred contexts; it should track per-resource usage directly.
   [DXVK Developer guidelines](https://github.com/doitsujin/dxvk/wiki/Developer-guidelines)

2. **wgpu codifies the per-command-buffer tracking pattern explicitly** — the
   wgpu CHANGELOG records adding "Per-command-buffer resource tracking —
   Command buffer knows what resources it expects in which states, and what
   states they end up with," and lists "Rust wgpu, Dawn, Godot, DXVK,
   vkd3d-proton" as the projects converging on this model. This is the closest
   to a canonical design statement of the pattern.
   [wgpu CHANGELOG](https://github.com/gfx-rs/wgpu/blob/trunk/CHANGELOG.md)

3. **DXVK improved D3D11 command submission to behave like native D3D11** — a
   changelog entry reads: "Improved D3D11 command submission logic in order to
   make overall performance more consistent, and to bring DXVK's behaviour more
   in line with native D3D11." This is the same problem space: matching native
   D3D11's lazy-flush heuristics without over-retaining.
   [dxvk-gplasync changelog](https://build.opensuse.org/projects/home:Maddie:dxvk-gplasync/packages/dxvk-gplasync-x86_64-v3/files/dxvk-gplasync.changes?expand=0)

4. **vkd3d-proton rewrote queue submission to cut dummy waits** — release
   notes: "Rewrite queue submission logic to use fewer 'dummy' wait/signal
   submissions." This is the canonical lesson for keeping the submission fence
   graph minimal: only insert a Vulkan semaphore/Metal event when a real
   cross-queue dependency exists, never pro-forma on every submit.
   [vkd3d-proton Releases](https://github.com/HansKristian-Work/vkd3d-proton/releases)
   [vkd3d-proton CHANGELOG](https://github.com/HansKristian-Work/vkd3d-proton/blob/master/CHANGELOG.md)

5. **Mesa d3d12 driver "Hold lock when removing resources from residency list"**
   — a Mesa 22.2 changelog entry points to the d3d12 driver having an explicit
   residency list with a lock around removal, i.e. resources are kept resident
   while referenced by a pending submission and removed when retired.
   [Mesa 22.2.0 relnotes](https://code.hyprland.org/fdo-mirrors/mesa/src/tag/mesa-25.1.0-rc3/docs/relnotes/22.2.0.rst)

6. **Mesa 25.0+: "Move all resource tracking to the residency set"** (Mesa
   26.0 release notes). Indicates the long-term direction in Mesa is to make
   residency the single source of truth for resource liveness, decoupling it
   from per-command-buffer tracking lists. Worth considering as a design
   target — but only after the simpler per-command-list pattern is working.
   [Mesa 26.0.0 relnotes](https://docs.mesa3d.org/relnotes/26.0.0.html)

### Descriptor / view capture at record time

7. **D3D12 descriptor views have no lifetime of their own** — Maister's DXIL
   translation post states: "view objects in D3D12 do not have a lifetime of
   their own. The lifetime is tied to the descriptor heap itself, as a
   descriptor is [just memory]." Practical consequence: when recording a draw,
   you cannot hold a pointer to the descriptor heap slot and dereference it at
   submit time — you must capture (snapshot) the descriptor's contents into
   the command buffer's resource tracking structure. This is what
   `VK_EXT_descriptor_buffer` enables natively on Vulkan.
   [My personal hell of translating DXIL to SPIR-V – part 3](https://themaister.net/blog/2021/11/07/my-personal-hell-of-translating-dxil-to-spir-v-part-3/)

8. **`VK_EXT_descriptor_buffer` removes the "consume at record time" pretense**
   — the proposal explicitly states: "With descriptor being modeled as buffer
   memory, we remove all pretense of the implementation being able to consume
   descriptors when recording the command buffer," and notes that "vkd3d-proton
   currently works around this problem by quantizing the texel buffer." For
   Metal, the equivalent pattern is to copy descriptor contents into an
   argument buffer (or into the recorded command's per-draw resource list) at
   record time.
   [VK_EXT_descriptor_buffer proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_descriptor_buffer.html)
   [VK_EXT_descriptor_buffer in Vulkan-Docs](https://github.com/KhronosGroup/Vulkan-Docs/blob/main/proposals/VK_EXT_descriptor_buffer.adoc)

### ExecuteIndirect / UAV clear / indirect command emulation

9. **vkd3d-proton 3.0.1 (May 2026) shipped a dedicated batched ExecuteIndirect
   system** — release notes: "Implement a batched system for complex
   ExecuteIndirect where existing split command list optimization did not work.
   Improves GPU bound performance in various games that spam ExecuteIndirect
   with state updates like Crimson Desert, Starfield and Halo Infinite." This
   is the single most relevant prior-art finding for M12: complex
   ExecuteIndirect (root-constant/argument-buffer updates per draw) does *not*
   work with naive per-draw splits and needs a dedicated batching pass.
   [vkd3d-proton Releases](https://github.com/HansKristian-Work/vkd3d-proton/releases)
   [vkd3d-proton 3.0.1 coverage](https://www.gamingonlinux.com/2026/05/vkd3d-proton-3-0-1-brings-many-linux-gaming-enhancements-for-direct3d-12-via-vulkan/)

10. **vkd3d-proton 2.10 mapped ExecuteIndirect to
    `VK_NV_device_generated_commands_compute`** — release notes: "With
    NV_device_generated_commands_compute we can efficiently implement
    Starfield's use of ExecuteIndirect which hammers multi-dispatch COMPUTE
    + ..." Lesson: when the target API has a DGC extension, prefer it; when it
    does not (Metal), fall back to a CPU/GPU-side batching layer.
    [vkd3d-proton 2.10 release coverage](https://www.linuxcompatible.org/story/vkd3dproton-210/)

11. **Metal has no API-level multi-draw indirect; it uses Indirect Command
    Buffers (ICBs)** — Amélie Heinrich's RHI writeup: "Metal does not support
    API level multi draw indirect. It has its own system: indirect command
    buffers, that can be written inside an MSL shader. Executing that ICB then
    properly emulates D3D12 style ExecuteIndirect." Caveat: real-world
    benchmarks have shown ICBs *slower* than a loop of simple indirect draws
    (GravityMark measured this on Metal). So M12 needs to A/B both paths and
    not assume ICBs win.
    [Making a modern Metal/D3D12/Vulkan RHI](https://amelieheinrich.com/post.html?id=rhi)
    [GravityMark forum discussion on Metal ICB perf](https://forums.macrumors.com/threads/gravitymark-gpu-benchmark.2301185/)

12. **Chromium/Dawn emulates WebGPU multiDrawIndirect on Metal via ICB** — the
    Chromium issue for MultiDrawIndirect states: "The Metal backend has to
    emulate the multi draws commands. It converts and encodes the multi draws
    into an indirect command buffer." This is a production-grade reference
    implementation worth studying for the encoding/recording shape of the ICB
    approach.
    [Chromium MultiDrawIndirect issue](https://issues.chromium.org/issues/356461286)

13. **D3D12 command signatures are a superset of Metal/Vulkan indirect** —
    gpuweb Issue #31: "In D3D12 executing commands indirectly is done through
    'command signature' that are a super-set of what's available in Metal and
    Vulkan." Implication: some command signature argument types (root constant
    updates, vertex/index buffer binds per draw) have no direct Metal
    equivalent and must be expanded into the per-draw state stored alongside
    the ICB.
    [gpuweb Issue #31](https://github.com/gpuweb/gpuweb/issues/31)

14. **UAV barriers in legacy D3D12 must be re-emitted even for non-state
    changes** — vkd3d-proton's `resource.c` has an explicit comment: "We need
    aggressive decay and promotion into anything," and elsewhere "This was
    fixed in enhanced barriers to also require discards on UAV [...]." Lesson:
    legacy barriers force broad UAV barriers (NULL UAV = "all UAVs") and the
    translator must handle decay/promotion explicitly. Enhanced Barriers
    removes most of this.
    [vkd3d-proton libs/vkd3d/resource.c](https://github.com/HansKristian-Work/vkd3d-proton/blob/master/libs/vkd3d/resource.c)

### Metal hazard tracking and the tracked/untracked split

15. **Metal auto-tracks hazards for individual tracked resources, not for
    heap-allocated untracked ones** — Apple's docs: "The default tracking mode
    for an MTLResource is `MTLHazardTrackingMode.tracked` because individual
    resources benefit from automatic hazard tracking," and "The default
    tracking mode for an MTLHeap is `MTLHazardTrackingMode.untracked` because
    heaps typically contain many resources that you manage manually." This is
    the central Metal design split that every D3D12-to-Metal layer must
    understand: tracked resources give you implicit ordering for free; for
    placed/reserved resources (D3D12 heaps) and aliasing, you must use
    `MTLFence`/`MTLEvent` manually.
    [hazardTrackingMode docs](https://developer.apple.com/documentation/metal/mtlresource/hazardtrackingmode)
    [MTLHazardTrackingMode.default](https://developer.apple.com/documentation/metal/mtlhazardtrackingmode/default)
    [Metal Resource synchronization docs](https://developer.apple.com/documentation/metal/resource-synchronization)

16. **D3DMetal leans on Metal's auto hazard tracking to avoid doubling work**
    — Apple Developer Forums comment (about CrossOver/D3DMetal vs DXMT): "DXMT
    handles it. Instead of doubling the work, it allows Metal to
    single-handedly track resource dependencies internally. This is in part
    due to the [Metal hazard tracking system]." Strategy implication: a
    Metal-backed D3D12 implementation that *trusts* Metal's per-resource
    tracking can skip a lot of the explicit barrier bookkeeping that
    vkd3d-proton/DXVK need on Vulkan.
    [Apple Developer Forums: Game Porting Toolkit tag](https://developer.apple.com/forums/tags/game-porting-toolkit)

17. **DXMT built synchronization on MTLFence + intrapass barriers** — DXMT
    release notes (the Metal D3D11/D3D10 layer used by this project's M9/M10/M11
    routes) state: "Synchronization based on MTLFence and intrapass barriers."
    This is the same primitive set D3DMetal uses, and it is the lowest-level
    escape hatch for cases Metal's auto-tracker cannot express.
    [3Shain/dxmt Releases](https://github.com/3Shain/dxmt/releases)

18. **D3DMetal shares sync code across D3D11 and D3D12** — DXMT discussion:
    "D3DMetal shares a lot of code between their D3D12 and D3D11
    implementation, including the synchronization. So DXMT could very well end
    up quite a bit faster [by specializing]." Implication: the M12 (D3D12) and
    M9/M10/M11 (D3D11/10, DXMT) code in this repo should be designed to share
    a synchronization/fence-completion layer; D3DMetal does exactly that.
    [3Shain/dxmt discussion #15](https://github.com/3Shain/dxmt/discussions/15)

19. **MoltenVK hits Metal auto-tracking limits with compute-recorded indirect
    draws** — MoltenVK Issue #2312: encountered when "using vkDrawIndirect(),
    where a compute shader in the same command buffer recorded draw calls in
    a [...]." Caveat: even on Metal, intra-command-buffer indirect draws
    produced by a compute pass need explicit encoder-level fences; the
    auto-tracker does not always see the dependency.
    [KhronosGroup/MoltenVK Issue #2312](https://github.com/KhronosGroup/MoltenVK/issues/2312)

### Command buffer completion modeling

20. **Metal and WebGPU use transient command buffers** — Sebastian Aaltonen's
    "No Graphics API" piece: "Metal and WebGPU feature transient command
    buffers, which are created just before recording and disappear after GPU
    has finished rendering." This shapes the completion model: there is no
    long-lived `VkCommandBuffer` to reset and re-record, so the completion
    callback is the only place to free per-recording state.
    [No Graphics API — Sebastian Aaltonen](https://www.sebastianaaltonen.com/blog/no-graphics-api)

21. **IREE Metal HAL uses a linked-list of deferred command segments** — design
    doc: "to implement IREE HAL command buffers using Metal, we perform two
    steps using a linked list of command segments: First we create segments to
    keep [...] A linked list gives us the flexibility [...] and a deferred
    recording gives us the complete picture of the command buffer when really
    started recording." This is a concrete reference design for a deferred
    command recorder that produces a Metal `MTLCommandBuffer` only at submit
    time, allowing resource lifetime bookkeeping to be finalized before commit.
    [IREE Metal HAL driver design](https://iree.dev/developers/design-docs/metal-hal-driver/)

22. **Metal queues without tracked resources are not Vulkan/D3D12 queues** —
    gpuweb Issue #1065: "It sounds like, without untracked resources, Metal
    queues bear no relation to Vulkan and D3D12 queues and are purely a
    CPU-multithreading primitive?" Implication: queue-level ordering on Metal
    must be implemented via shared events/fences; the MTLCommandQueue itself
    does not provide cross-queue synchronization semantics for untracked
    resources.
    [gpuweb Issue #1065](https://github.com/gpuweb/gpuweb/issues/1065)

23. **Mesa timeline-point refcounting for in-flight submissions** — Mesa 25.3
    changelog: "Hold a reference to pending vk_sync_timeline_points." This is
    the canonical pattern for keeping a sync point alive while a submission is
    pending and dropping it once the GPU signals. Mirror this for the Metal
    side: hold a ref to the MTLSharedEvent + value pair until addCompletedHandler
    fires.
    [Mesa 25.3.0 relnotes](https://docs.mesa3d.org/relnotes/25.3.0.html)

### Promotion / decay (legacy D3D12 barrier model)

24. **Promotion and decay are widely cited as the worst part of D3D12** — The
    Danger Zone retrospective: "By far the worst part was the concept of
    resource state 'promotion' and 'decay'." Enhanced Barriers explicitly
    eliminate them. Any D3D12-to-Metal layer must either (a) faithfully model
    decay (a resource decays to COMMON after a GPU signal is queued) and
    promotion (implicit COMMON→specific state on first use), or (b) prefer the
    enhanced-barrier API path when present.
    [Ten Years of D3D12 — The Danger Zone](https://therealmjp.github.io/posts/ten-years-of-d3d12/)
    [Enhanced Barriers spec](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html)
    [DirectX CPUEfficiency.md on UAV barriers](https://github.com/microsoft/DirectX-Specs/blob/master/d3d/CPUEfficiency.md)

### Cautionary tales

25. **DXVK 2.6's sparse buffer usage had to be removed for causing hangs** —
    release notes: "Removed sparse buffer usage that was introduced in DXVK
    2.6. This caused all sorts of driver issues that would cause hangs and
    instability." Lesson: features that interact with residency/lifetime
    tracking (sparse binding especially) need careful driver-specific
    validation before enabling; do not enable wide by default.
    [DXVK Releases](https://github.com/doitsujin/dxvk/releases)

26. **vkd3d-proton: long-standing fence rewind bugs** — recent changelog:
    "Fixes some long-standing issues with how we deal with fence rewinds,"
    plus "placed MSAA resources and alignment" fixes. Fence value reuse and
    placed-resource alignment are sharp edges even in mature implementations.
    [vkd3d-proton Releases](https://github.com/HansKristian-Work/vkd3d-proton/releases)

## Sources

### Kept

- **DXVK Developer guidelines wiki** (https://github.com/doitsujin/dxvk/wiki/Developer-guidelines) — primary; explicit statement about deferred-context replay pessimization.
- **wgpu CHANGELOG** (https://github.com/gfx-rs/wgpu/blob/trunk/CHANGELOG.md) — primary; canonical description of per-command-buffer tracking and the projects that share the model.
- **vkd3d-proton Releases / CHANGELOG** (https://github.com/HansKristian-Work/vkd3d-proton/releases, https://github.com/HansKristian-Work/vkd3d-proton/blob/master/CHANGELOG.md) — primary; the batched ExecuteIndirect system, queue submission rewrite, fence rewind fixes.
- **vkd3d-proton libs/vkd3d/resource.c** (https://github.com/HansKristian-Work/vkd3d-proton/blob/master/libs/vkd3d/resource.c) — primary source; inline comments confirm aggressive decay/promotion handling.
- **Maister DXIL part 3** (https://themaister.net/blog/2021/11/07/my-personal-hell-of-translating-dxil-to-spir-v-part-3/) — primary commentary; D3D12 view lifetime is tied to the descriptor heap.
- **VK_EXT_descriptor_buffer proposal** (https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_descriptor_buffer.html, https://github.com/KhronosGroup/Vulkan-Docs/blob/main/proposals/VK_EXT_descriptor_buffer.adoc) — primary spec; explains why descriptor capture-at-record is necessary and the vkd3d-proton texel-buffer quantization workaround.
- **Apple MTLResource hazardTrackingMode docs** (https://developer.apple.com/documentation/metal/mtlresource/hazardtrackingmode, https://developer.apple.com/documentation/metal/mtlhazardtrackingmode/default, https://developer.apple.com/documentation/metal/resource-synchronization) — primary; defines tracked/untracked split and per-resource auto-tracking.
- **3Shain/dxmt Releases & discussion** (https://github.com/3Shain/dxmt/releases, https://github.com/3Shain/dxmt/discussions/15) — primary; MTLFence + intrapass barriers synchronization, and the D3DMetal code-sharing observation.
- **Amélie Heinrich RHI post** (https://amelieheinrich.com/post.html?id=rhi) — primary practitioner writeup; ICB-based ExecuteIndirect emulation.
- **Chromium MultiDrawIndirect issue** (https://issues.chromium.org/issues/356461286) — primary; Dawn's production Metal ICB emulation path.
- **gpuweb Issue #31** (https://github.com/gpuweb/gpuweb/issues/31) — primary spec discussion; D3D12 command signature superset vs Metal/Vulkan.
- **gpuweb Issue #1065** (https://github.com/gpuweb/gpuweb/issues/1065) — primary; Metal queues vs Vulkan/D3D12 queue semantics for untracked resources.
- **IREE Metal HAL driver design doc** (https://iree.dev/developers/design-docs/metal-hal-driver/) — primary; linked-list deferred command segment design.
- **MoltenVK Issue #2312** (https://github.com/KhronosGroup/MoltenVK/issues/2312) — primary bug report; auto-tracker gap for compute-recorded indirect draws.
- **Sebastian Aaltonen "No Graphics API"** (https://www.sebastianaaltonen.com/blog/no-graphics-api) — primary; transient command buffer model.
- **Microsoft Enhanced Barriers spec** (https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html) and **CPUEfficiency.md** (https://github.com/microsoft/DirectX-Specs/blob/master/d3d/CPUEfficiency.md) — primary spec; UAV barrier semantics and decay/promotion removal.
- **Ten Years of D3D12 — The Danger Zone** (https://therealmjp.github.io/posts/ten-years-of-d3d12/) — primary practitioner retrospective; decay/promotion pain.
- **Mesa 22.2.0 / 25.3.0 / 26.0.0 release notes** — primary; d3d12 residency-list locking, timeline-point refcounting, "move all resource tracking to the residency set."
- **DXVK Releases (sparse removal)** (https://github.com/doitsujin/dxvk/releases) — primary; cautionary tale about sparse buffer usage causing hangs.
- **vkd3d-proton 2.10 / 2.11 / 3.0.1 coverage** (gamingonlinux.com, linuxcompatible.org, reddit) — secondary but reliable; ExecuteIndirect history (NV DGC compute, mesh shader ExecuteIndirect, batched system).

### Dropped

- **"A driver on the GPU" — Bas Nieuwenhuizen** (https://www.basnieuwenhuizen.nl/a-driver-on-the-gpu/) — referenced indirectly; the relevant DGC content is covered more directly by the vkd3d-proton release notes.
- **homebrew-core formula listing** — irrelevant to graphics; surfaced due to keyword match.
- **Vulkan is miserable / Why Linux is not ready / "Point of WebGPU on Native" Hacker News threads** — opinion pieces, no implementation detail.
- **Various Apple-Silicon-Guide / CrossOver user-guide marketing pages** — no implementation specifics.
- **Apple Developer Forums tag pages (general)** — kept only the specific quote about D3DMetal hazard tracking.

## Gaps

- **Direct D3D12 source in DXMT**: DXMT currently targets D3D11/10 only; the
  project's own M12 surface (D3D12-to-Metal) is not yet open-sourced by 3Shain.
  The DXMT releases and discussions only confirm the *approach* (MTLFence +
  intrapass barriers, Metal hazard tracking), not the D3D12-specific code.
- **D3DMetal source**: Apple's D3DMetal binary is closed. The only public
  signal about its design is the DXMT maintainer's commentary about shared
  D3D11/D3D12 sync code and the Apple Forums quote about hazard tracking.
- **Detailed vkd3d-proton `d3d12_command_queue` submit path internals**:
  search snippets confirm the design ("rewrite to fewer dummy wait/signal")
  but the per-resource retention list implementation in `queue.c` /
  `command_list.c` was not retrieved in full; a follow-up pass could fetch
  `libs/vkd3d/command_queue.c` and `command_list.c` directly for line-level
  evidence on submission-graph construction.
- **wgpu `hal::metal` resource-retention implementation**: confirmed via
  CHANGELOG that the pattern exists; specific `wgpu-hal/src/metal/` source was
  not retrieved. Would be a useful secondary reference.
- **Performance numbers** for ICB vs. looped-indirect on Apple Silicon (M-series)
  for D3D12 workloads specifically — GravityMark is one data point on
  older hardware; more recent M3/M4 numbers with D3D12-style argument-buffer
  updates per draw were not found.

### Suggested next steps

1. Fetch `vkd3d-proton/libs/vkd3d/command_queue.c`, `command_list.c`, and
   `device.c` to extract the exact submission-graph + fence-value retirement
   pattern (the most direct prior art for M12's submission layer).
2. Fetch `wgpu-hal/src/metal/device.rs` and `encoder.rs` for the
   per-command-buffer tracking list implementation.
3. Fetch `3Shain/dxmt` repo tree (currently D3D11/10) to confirm the
   MTLFence-based synchronization structure that the M12 D3D12 path would
   inherit.
4. Pull Chromium Dawn `src/dawn/native/metal/*` for the production-grade ICB
   emulation of MultiDrawIndirect.

## Practical design implications for MetalSharp M12

Synthesizing the above into concrete guidance for the project's D3D12-to-Metal
engine (`src/d3d/d3d12/` + `metal/` + the `dxmt-m12` runtime surface):

1. **Adopt per-command-list/per-submission resource tracking as the primary
   retention mechanism.** This is the pattern DXVK, vkd3d-proton, wgpu, MoltenVK,
   Mesa-d3d12, and IREE all converge on. Concretely: each recorded D3D12 command
   appends touched resources to a list keyed by the next fence value; on
   `addCompletedHandler`, walk that list and release. Do *not* rely on D3D12
   app-side refcounts alone (D3D12 apps routinely destroy resources before the
   GPU finishes using them, expecting the runtime to hold them).

2. **Lean on Metal's per-resource `MTLHazardTrackingMode.tracked` for the
   common case** (committed resources). For the subset that requires manual
   ordering — placed/reserved heap resources (D3D12 heaps), aliasing, UAV
   clears across encoders, and compute-recorded indirect draws — insert
   `MTLFence` / `MTLSharedEvent` explicitly. DXMT already does exactly this
   ("MTLFence + intrapass barriers"), and D3DMetal is widely reported to lean
   on Metal's auto-tracker. This is a major opportunity to skip much of the
   barrier bookkeeping vkd3d-proton/DXVK must do on Vulkan.

3. **Capture descriptor contents at record time, not at submit time.** D3D12
   descriptor views have no lifetime of their own (Maister); a stale descriptor
   heap slot read at submit time is a use-after-free. Mirror `VK_EXT_descriptor_buffer`'s
   mental model: snapshot the descriptor's resolved resource + format + view
   parameters into the command list's per-draw state (or into a Metal argument
   buffer slot captured at record time). The `dxmt.conf`/DXMT argument-buffer
   pipeline already follows this pattern; M12 should too.

4. **Plan for a dedicated ExecuteIndirect batching layer.** vkd3d-proton 3.0.1
   just shipped one because naive split-command-list optimization did not work
   for complex (root-state-changing) signatures; Dawn/Chromium emulates
   MultiDrawIndirect by encoding into ICBs; Amélie Heinrich's RHI confirms
   the ICB-based approach emulates D3D12 ExecuteIndirect. For M12: implement
   both paths (ICB + looped-indirect), A/B them per workload (GravityMark
   data shows ICBs can be slower), and pick based on per-draw argument count
   and signature complexity.

5. **Model command buffer completion as: pending set + timeline event value.**
   Mirror Mesa's "Hold a reference to pending vk_sync_timeline_points" pattern
   on the Metal side: hold a strong ref to the `MTLSharedEvent` + signaled
   value until `addCompletedHandler` fires, then drop it and retire the
   pending resource list. Use a timeline-style value (monotonically
   increasing) so the same event can service many submissions.

6. **Use a deferred, segment-list command recorder.** IREE's linked-list
   approach lets the recorder see the whole command buffer before committing
   to a `MTLCommandBuffer`. This is valuable for: batching ExecuteIndirect,
   coalescing redundant state changes, deciding when to flush an encoder
   (cheap on Metal — `endEncoding` is a real cost), and finalizing the
   resource tracking set. Worth adopting for M12's `MCommandQueue` /
   `MCommandList` translation.

7. **Implement decay/promotion carefully — or push for Enhanced Barriers.**
   Legacy D3D12 ResourceBarrier requires faithful decay (state → COMMON after
   a GPU signal is queued) and promotion (implicit COMMON → specific state on
   first use). vkd3d-proton does this explicitly ("aggressive decay and
   promotion into anything"). On Metal, decay maps naturally to Metal's
   automatic layout transitions for tracked resources; promotion is mostly a
   no-op because Metal doesn't have explicit layouts. But UAV barriers
   (especially NULL UAV = "all UAVs") must still be translated to explicit
   `MTLFence` waits, since Metal has no global UAV barrier primitive.

8. **Treat sparse/residency features with caution.** DXVK 2.6 had to remove
   sparse buffer usage for causing driver hangs; Mesa is mid-migration to
   residency-set-based tracking. For M12, defer sparse/tiling support until
   the basic per-command-buffer tracking is solid, and gate any sparse paths
   behind per-driver validation.

9. **Share the synchronization layer between M9/M10/M11 (DXMT) and M12.**
   D3DMetal does this between D3D11 and D3D12; the project already shares
   `~/.metalsharp/runtime/wine/lib/dxmt*/` and the Wine runtime, so a shared
   `MTLFence`/`MTLSharedEvent` completion module is natural and reduces
   divergence risk.

10. **Minimize dummy signal/wait pairs at submission.** vkd3d-proton's
    submission rewrite ("fewer 'dummy' wait/signal submissions") is directly
    applicable: only insert a Metal shared-event wait when a real cross-queue
    dependency exists (a `D3D12_RESOURCE_BARRIER` cross-queue transition, a
    `Wait` on a fence value the current queue hasn't reached). Pro-forma
    signal/wait on every submit is the over-retention/over-synchronization
    trap.
