# Roadmap: Post-Rosetta MetalSharp (macOS 28+)

**Created:** 2026-07-09
**Context:** Apple announced at WWDC 2025 that Rosetta 2 will be restricted to
"older games" in macOS 28 (Fall 2027), then likely removed entirely. MetalSharp's
D3DMetal/GPTK pipeline (`PipelineId::D3DMetal` / `M13`) requires Rosetta 2 to run
x86_64 Windows game executables. The DXMT pipelines (M9/M10/M11/M12) run native
ARM64 code for the graphics translation but still need x86_64 emulation for the
game process itself.

## Problem Statement

MetalSharp's architecture today has three layers that touch x86:

| Layer | What runs x86? | Why? |
|-------|----------------|------|
| **Game executable** | x86_64 Windows PE | Virtually all Windows games compiled for x86_64 |
| **Wine loader** | x86_64 Linux/Win32 | Wine PE loader is per-arch; cross-arch needs emulation |
| **D3DMetal framework** | Native ARM64 | Apple's framework is ARM64. But it loads INTO the x86 process |
| **DXMT (M9–M12)** | Native ARM64 | DXMT dylibs are natively ARM64. The game process is x86 |

When Rosetta goes away, the entire x86→ARM64 userspace binary translation
layer vanishes. No existing open-source project (Box64, FEX-Emu, QEMU-user)
supports macOS ARM64 as of mid-2026. FEX-Emu explicitly declined macOS
support (issue #5046, "Not planned", Nov 2025).

This roadmap outlines six parallel tracks, ordered from safest/most pragmatic
to most ambitious/novel. **No single track is sufficient alone.** A resilient
MetalSharp needs at least Tracks A + B, with C as the long-term replacement
for Rosetta.

---

## Track A: The Apple Games Exemption (Pragmatic Short-Term)

### What Apple Actually Said

From the Ars Technica article (June 9, 2025):

> "Rosetta will continue to work as a general-purpose app translation tool in
> both macOS 26 and macOS 27. But after that, Rosetta will be pared back and
> will only be available to a limited subset of apps — specifically, older
> games that rely on Intel-specific libraries but are no longer being actively
> maintained by their developers."

### Why This Matters

"Older games that rely on Intel-specific libraries but are no longer being
actively maintained" is literally the entire MetalSharp use case. Every game
launched through MetalSharp is:
- An x86_64 Windows binary
- Not maintained for macOS (otherwise it'd have a native port)
- Reliant on Intel-specific libraries (D3D, the Windows PE)

### Strategy

1. **Engage Apple's Developer Relations** — Make the case that MetalSharp games
   are exactly the "limited subset" Apple intends to preserve. MetalSharp is not
   a general-purpose app; it's a game compatibility layer.

2. **Determine the Exemption Mechanism** — How does Apple actually implement
   the restriction? Possibilities:
   - A per-app entitlement (`.entitlements` plist with `com.apple.rosetta.games`)
   - A launch services check (app bundle contains game content → Rosetta allowed)
   - A dynamic linker gate (Rosetta runtime only loads for apps signed with a
     specific provisioning profile)

3. **Working Group** — Coordinate with CodeWeavers/CrossOver and other affected
   projects. CrossOver has publicly stated they'll handle it; shared lobbying
   is stronger.

4. **Fallback: User-installed Rosetta** — If Apple ships the Rosetta runtime
   on-disk but restricts the launch path, MetalSharp could potentially load
   Rosetta's `/usr/libexec/rosetta/runtime` directly as a process launcher.

### Risks

- Apple could ignore the use case entirely.
- "Older games" might not include games released after a certain date.
- Apple could ship macOS 28 without the Rosetta runtime binary at all,
  making any workaround impossible.

### Timeline

- **Now–WWDC 2027 (macOS 28 beta):** Engage Apple, track WWDC sessions
- **WWDC 2027:** Determine if exemption is viable
- **If yes:** Qualify MetalSharp under it
- **If no:** Tracks B–F become critical

---

## Track B: DXMT Everywhere + VKD3D-Metal Bridge (Most Viable Alternative)

### The Core Insight

**DXMT is already native ARM64.** It translates D3D9/10/11 → Metal with
zero x86 code. The ONLY x86 dependency is the game process itself running
as an x86_64 PE binary inside Wine.

But we don't need Rosetta for the entire Wine process. Wine can be compiled
**natively for ARM64 macOS** (Wine has supported `aarch64-darwin` as a host
since Wine 9.0). The challenge is that the Windows game binary inside Wine
is x86_64, and Wine needs to run it somehow.

### The VKD3D-Metal Stack

For D3D12 games (currently using D3DMetal/GPTK), a fully-native chain exists:

```
Windows x86_64 game.exe
  → D3D12 API calls
    → VKD3D (vkd3d-proton by Hans-Kristian)
      → Translates D3D12 → Vulkan
        → MoltenVK (Khronos Group)
          → Translates Vulkan → Metal (native ARM64)
            → Apple GPU
```

**Every link in this chain except the game binary is ARM64-native.**

- VKD3D compiles for ARM64 (used in Asahi Linux, Android)
- MoltenVK is ARM64-native on macOS since M1
- Wine ARM64 native has been in development since 2023

### DXMT Extensions (M11/M12 Already)

The existing M11/M12 pipelines already use DXMT for the D3D11/D3D12
translation. The MetalSharp engine, shader cache, and pipeline routing
are all native. What's missing is **running the game process** without
Rosetta.

### Strategy

1. **Port VKD3D-Proton to macOS ARM64** — Build the vkd3d-proton tree
   as a macOS dylib. This gives a clean D3D12→Vulkan translation that
   doesn't need any Apple proprietary framework.

2. **Bundle MoltenVK as a MetalSharp dependency** — Ship MoltenVK's
   `libMoltenVK.dylib` inside the MetalSharp app bundle. This gives
   the Vulkan→Metal half of the stack without requiring the user to
   install anything.

3. **Wire VKD3D through the existing shader cache** — Reuse the M12
   shader cache family (`m12`, `dxmt-metal12`) for VKD3D-based D3D12
   games. The cache infrastructure is already shader-family-aware.

4. **Add `M14` pipeline** — A new `PipelineId::M14` that routes D3D12
   games through VKD3D+MoltenVK instead of D3DMetal/GPTK.

### What Still Needs Emulation

Even with a fully-native graphics stack, the game process binary is x86_64.
This needs Track C or a Wine WOW64 solution (see Track D).

### Timeline

- **0–3 months:** Build vkd3d-proton + MoltenVK proof-of-concept on macOS ARM64
- **3–6 months:** Wire M14 pipeline into the engine, add shader cache family
- **6–12 months:** Test against the existing D3D12 game corpus

---

## Track C: Embedded QEMU User-Mode as a Rosetta Replacement (Ambitious)

### The "Build Our Own Rosetta" Approach

Rosetta 2 is a userspace x86_64→ARM64 binary translator. If Apple removes
it, we can build our own. This is not as crazy as it sounds:

- **QEMU user-mode** (`qemu-x86_64`) already does x86_64→ARM64 translation
  on Linux with JIT (TCG) and reasonable performance
- **Hangover** (Wine + emulator DLLs on ARM64 Linux) proves the concept works
- The emulation surface is limited: only the game process binary needs x86;
  the vast majority of code (Wine, DXMT, Metal, kernel) runs native ARM64

### Why This Is Different From FEX/Box64

Both FEX-Emu and Box64 target **Linux**. They use Linux-specific mechanisms:
- `mmap()` at fixed addresses (MAP_FIXED)
- Linux `/proc/self/maps` for memory layout
- Linux syscall conventions
- Linux ELF loader

A macOS port would need:
- Mach-O binary loader instead of ELF
- Darwin syscall thunking instead of Linux syscalls
- `mach_vm_map()` instead of `mmap()`
- No `/proc` filesystem — use `mach_vm_region()` instead
- 16KB page size handling (Apple Silicon uses 16KB pages, x86 expects 4KB)

### The Key Insight: Emulate the Windows PE, Not the macOS Process

Instead of emulating a full macOS x86_64 process (which requires translating
Darwin syscalls, handling Mach ports, etc.), emulate the **Windows PE binary
inside Wine**. This is exactly what Hangover does on Linux:

```
┌──────────────────────────────────────────────────┐
│  Native ARM64 Wine (aarch64-darwin)              │
│  ┌────────────────────────────────────────────┐  │
│  │  Emulator DLL (arm64ecfex / qemu-x86_64)   │  │
│  │  - Loads the Windows PE binary             │  │
│  │  - JIT translates x86_64→ARM64             │  │
│  │  - Forwards Win32 syscalls to native Wine  │  │
│  │  - Graphics calls go through native stack  │  │
│  └────────────────────────────────────────────┘  │
│  Native ARM64 DXMT / VKD3D / MoltenVK            │
│  Native ARM64 Metal framework                     │
└──────────────────────────────────────────────────┘
```

The emulator DLL is loaded by Wine's WOW64 layer. When the game makes a
Win32 call (NtCreateFile, NtUserCreateWindowEx, etc.), the call exits the
emulator and runs natively in Wine. When the game makes a D3D call, it
goes through native DXMT/VKD3D.

### What Needs to Be Built

1. **x86_64 PE loader as a macOS dylib** — Load a Windows PE binary
   from disk, map its sections, resolve imports. This code exists in
   Wine's `ntdll` but is x86-specific. Needs to be adapted to run in
   the emulator context.

2. **QEMU TCG JIT for macOS** — Port QEMU's tiny code generator
   (TCG) x86_64→ARM64 backend to build as a macOS dylib. QEMU TCG
   already supports aarch64-darwin as a host. The frontend
   (`target/i386/translate.c`) is architecture-independent.

3. **Wine WOW64 integration** — Wire the emulator into Wine's
   ARM64EC-compatible WOW64 path. Wine's `wow64cpu.dll` needs a
   new backend that calls into the emulator instead of executing
   native x86 code.

4. **16KB page alignment** — Apple Silicon uses 16KB pages. x86 PE
   binaries assume 4KB. The emulator must handle sub-page
   alignment (this is the hardest part).

### Timeline

- **0–6 months:** Research phase. Port QEMU TCG x86_64→ARM64 to
  build as a standalone macOS dylib. Prove the JIT works.
- **6–12 months:** Build the PE loader + Wine WOW64 integration.
  Get a simple Windows "hello world" running.
- **12–18 months:** Performance optimization. Add DynaRec layer.
  Test against real games.
- **18–24 months:** Ship as the default emulation layer for all
  pipelines, replacing Rosetta.

---

## Track D: Wine WOW64 + Windows PE ARM64EC (Architecture-Agnostic Wine)

### The Windows ARM64EC Insight

Microsoft's ARM64EC ("Emulation Compatible") is an ABI that lets x86_64
code and ARM64 code coexist in the same process. On Windows ARM64, this
is how x86_64 apps gradually migrate to ARM64: the app binary is x86_64
but individual DLLs can be ARM64EC, and the two can call each other.

**Wine has implemented ARM64EC support since Wine 9.0** (January 2024).
This means Wine on ARM64 can load an x86_64 Windows executable and use
ARM64EC thunks to call native ARM64 Wine DLLs for system services.

### How This Helps MetalSharp

If Wine's ARM64EC layer is robust enough:
- Wine runs natively on ARM64 macOS
- The x86_64 game executable is loaded through the ARM64EC emulation path
- Win32 system calls are executed natively (no emulation)
- Graphics calls (D3D11, D3D12) go through native DXMT/VKD3D
- ONLY the game's own x86_64 code (game logic, physics, etc.) runs emulated

This is the same architecture as Hangover (Track C) but uses Wine's
built-in `xtajit` support rather than an external emulator. On Windows
ARM64, the JIT is provided by the OS. On macOS, we'd need to provide it.

### Strategy

1. **Build Wine ARM64 macOS with WOW64 support** — Wine's configure
   has `--enable-archs=i386,x86_64,aarch64`. Build with just
   `aarch64` as host and `x86_64` as guest.

2. **Stub or port xtajit for macOS** — Wine's `xtajit64.dll` is the
   DLL that does the x86_64→ARM64 translation. On Windows this is
   provided by the OS. We'd need to link it to QEMU TCG (Track C)
   or to a stripped-down emulator.

3. **Use ARM64EC calling convention** — Wine's `libarm64ecfex` in
   Hangover shows the pattern: FEX-Emu embedded as a DLL that Wine
   loads for emulation.

### Timeline

- **0–3 months:** Build Wine aarch64-darwin with `--enable-wow64`
- **3–6 months:** Get a simple x86_64 Windows program running
  (even at interpreter speed)
- **6–12 months:** Integrate with the DXMT/VKD3D stack

---

## Track E: Virtualized Linux ARM64 + GPU Acceleration (Fallback)

### The "If All Else Fails" Approach

If macOS 28 removes Rosetta entirely and Tracks B/C/D aren't ready,
the most reliable fallback is to run the x86_64 Windows game inside a
Linux ARM64 VM with full GPU acceleration.

### The Architecture

```
┌─────────────────────────────────────────────┐
│  macOS 28 (Apple Silicon, no Rosetta)       │
│  ┌───────────────────────────────────────┐  │
│  │  Linux ARM64 VM (UTM / Hypervisor.fw)  │  │
│  │  ┌─────────────────────────────────┐  │  │
│  │  │  FEX-Emu / Box64                │  │  │
│  │  │  Wine ARM64 + WOW64 + emulation  │  │  │
│  │  │  DXVK / VKD3D → Vulkan           │  │  │
│  │  │  VirtIO GPU (paravirtualized)    │  │  │
│  │  └─────────────────────────────────┘  │  │
│  │  Metal backend (GPU rendering)        │  │
│  └───────────────────────────────────────┘  │
│  MetalSharp frontend (Electron, native UI)  │
└─────────────────────────────────────────────┘
```

### Why This Actually Works Today

- **FEX-Emu on Linux ARM64** runs x86_64 Windows games through Wine with
  good performance
- **Box64 on Linux ARM64** has a dedicated Wine compatibility mode
- **UTM** for macOS provides QEMU-based VM management with Hypervisor.framework
  acceleration
- **VirtIO GPU** provides paravirtualized 3D acceleration
- **MoltenVK** inside the Linux guest can use the VirtIO GPU transport

### The GPU Problem (And Solution)

QEMU's VirtIO GPU (`virtio-gpu-gl`, `virtio-gpu-rutabaga`) supports:
- **Venus:** Vulkan passthrough (guest Vulkan → host Vulkan → MoltenVK → Metal)
- **VirGL:** OpenGL passthrough

This three-hop chain (guest Vulkan → host Vulkan → Metal) has overhead but
works. The Venus protocol is actively maintained and used by ChromeOS for
GPU acceleration in Linux VMs.

### Strategy

1. **Prototype:** Boot Asahi Linux ARM64 VM on macOS via UTM, install
   Wine+FEX, run a DX11 game. Measure performance.
2. **Integrate:** Package the VM + MetalSharp launcher as a single app
   bundle. MetalSharp's Electron UI launches the VM transparently.
3. **Optimize:** Use shared memory for the Venus transport. Map the
   VM's Vulkan surface directly to Metal textures.

### Timeline

- **0–3 months:** Prototype VM + GPU acceleration
- **3–6 months:** Measure performance ceiling
- **6–12 months:** Productize as optional fallback pipeline

---

## Track F: Native x86 Emulation via Apple Silicon Hardware Features (Exploratory)

### The TSO Mode Hypothesis

Apple Silicon M-series chips have a hardware feature that enables x86
Total Store Ordering (TSO) memory model. This is documented in the ARM
architecture reference manual as `ACTLR_EL1.TSO` or `FEAT_XNX`. Rosetta
2 uses this to avoid expensive memory barrier instructions when emulating
x86 strong memory ordering.

If we can enable TSO mode on a per-process basis (or per-thread), an
emulator becomes dramatically faster because it doesn't need to insert
`dmb` barriers after every store.

### What We Know

- Darwin kernel exposes `thread_set_tsd_base()` and similar APIs
- Rosetta's runtime (`/usr/libexec/rosetta/runtime`) is a Mach-O dylib
  that Apple ships on every macOS version since 11.0
- The Rosetta runtime has known entry points: `_rosetta_convert_main`,
  `_rosetta_get_process_info`, etc.
- A process launched under Rosetta has a special flag in its `proc` structure

### What We DON'T Know

- Whether TSO mode is accessible without Apple's private entitlements
- Whether the Rosetta runtime dylib can be loaded from a non-system path
- Whether Apple will leave the Rosetta runtime binary on disk in macOS 28

### Strategy

1. **Reverse engineering (defensive research only)** — Understand the
   Rosetta runtime's interface. Document entry points, thread setup,
   and syscall thunking mechanism.

2. **Monitor WWDC 2026 and 2027** — Apple might announce a public API
   for x86 emulation (similar to how they exposed Rosetta to Linux VMs
   in macOS Ventura).

3. **TSO mode research** — Investigate whether `pthread_attr_setarch()`
   or similar APIs exist to set per-thread memory ordering.

### Timeline

- **Ongoing:** Monitor Apple developer announcements
- **If Apple provides a public API:** Implement within 3 months
- **If not:** This track is dead; fall back to Tracks B–E

---

## Master Timeline

```
2026 Q3 (Jul–Sep):  ─┬─ Start Track B (vkd3d-proton POC)
                     ├─ Start Track D (Wine ARM64 macOS build)
                     ├─ Start Track E (Linux VM prototype)
                     └─ Engage Apple re: Track A

2026 Q4 (Oct–Dec):   ─┬─ Track B: M14 pipeline integrated
                     ├─ Track D: Simple x86_64 binary running on Wine ARM64
                     └─ Track E: Performance benchmarks

2027 Q1 (Jan–Mar):   ─┬─ Track C: QEMU TCG port to macOS dylib
                     ├─ Track B: Test against D3D12 game corpus
                     └─ Track E: Productize VM fallback

2027 Q2 (Apr–Jun):   ─┬─ WWDC 2027 — macOS 28 beta ships
                     ├─ Track A: Determine if exemption is viable
                     ├─ Track C: PE loader + Wine WOW64 integration
                     └─ Critical decision point

2027 Q3 (Jul–Sep):   ─┬─ IF Track A viable → qualify MetalSharp, relax
                     ├─ IF Track A not viable → full steam on C+D
                     ├─ Track C: First game running without Rosetta
                     └─ macOS 28 public beta

2027 Q4 (Oct–Dec):   ─┬─ macOS 28 ships (Rosetta restricted)
                     ├─ Production release with at least one
                     │  non-Rosetta pipeline
                     └─ IF D3DMetal still works under exemption,
                        ship hybrid approach
```

## Decision Matrix

When should each track be chosen over others?

| Scenario | Tracks to Prioritize |
|----------|---------------------|
| Apple exempts MetalSharp games | A (monitor) + B (for new D3D12 games) |
| Apple removes Rosetta entirely | C (QEMU emulator) + E (VM fallback) |
| Apple keeps Rosetta binary but restricts access | C (use Rosetta runtime directly) + D |
| Wine ARM64EC + WOW64 matures rapidly | D (preferred) + B |
| User performance expectations are low | E (VM fallback works at lower perf) |
| User performance expectations are high | C (QEMU JIT with TSO mode) |

## Resource Allocation

For a solo developer working 2026–2027:

| Track | Hours/Week | Why |
|-------|-----------|-----|
| A (Apple exemption) | 1–2 | Mostly monitoring and engagement, not engineering |
| B (VKD3D + MoltenVK) | 5–10 | Highest ROI; unblocks all D3D12 games |
| C (QEMU emulator) | 10–15 | The "real" Rosetta replacement; hardest but most valuable |
| D (Wine WOW64) | 3–5 | Lower priority if C succeeds; useful intermediate step |
| E (VM fallback) | 2–3 | Safety net; mostly packaging/integration |
| F (Hardware research) | 1 | Passive monitoring |

## Risks and Unknowns

1. **Apple could remove the Rosetta runtime binary entirely** — If
   macOS 28 ships without `/usr/libexec/rosetta/runtime`, Track F
   and any Rosetta-reuse approach dies.

2. **QEMU TCG on macOS 16KB pages** — The 16KB page size is the
   single hardest technical problem. x86 code assumes 4KB pages.
   QEMU TCG handles this with software page tables, but at a
   significant performance cost.

3. **Wine ARM64 maturity** — Wine on aarch64-darwin is still
   experimental. Wine's main development focus is x86_64 Linux and
   ARM64 Linux (Asahi). macOS ARM64 gets less attention.

4. **D3DMetal is not open source** — If the Apple exemption doesn't
   apply, D3DMetal as a framework may become inaccessible. VKD3D +
   MoltenVK is the only fully open-source alternative for D3D12.

5. **GPU driver stability** — MoltenVK on macOS ARM64 is stable
   but doesn't support all Vulkan extensions that VKD3D needs.
   Some D3D12 features may not translate.

6. **One developer, six tracks** — The most realistic outcome is
   shipping at least one non-Rosetta pipeline (likely Track B for
   D3D12 games through VKD3D+MoltenVK, and DXMT continuing to work
   for D3D9/10/11) combined with either the Apple exemption (Track A)
   or the QEMU emulator (Track C) for running the game binary.

---

## Appendix: Research References

- **Ars Technica (June 9, 2025):** "Apple details the end of Intel Mac
  support and a phaseout for Rosetta 2"
  https://arstechnica.com/gadgets/2025/06/apple-details-the-end-of-intel-mac-support-and-a-phaseout-for-rosetta-2/

- **FEX-Emu issue #5046 ("Mac Support"):** Closed as "Not planned" (Nov 2025).
  https://github.com/FEX-Emu/FEX/issues/5046

- **Hangover (Wine + emulator DLLs):** Runs x86_64 Windows apps on ARM64
  Linux using FEX/Box64 DLL backends. Linux only.
  https://github.com/AndreRH/hangover

- **Box64:** Linux userspace x86_64 emulator for ARM64 Linux. No macOS
  support. Uses Linux syscall forwarding + DynaRec.
  https://github.com/ptitSeb/box64

- **vkd3d-proton:** D3D12→Vulkan for Wine/Proton. Cross-compilable for
  any aarch64 target. Used by CrossOver on macOS.
  https://github.com/HansKristian-Work/vkd3d-proton

- **MoltenVK:** Vulkan→Metal portability layer. Native ARM64 on macOS
  since M1. SDK ships with universal binaries.
  https://github.com/KhronosGroup/MoltenVK

- **Wine ARM64:** Wine can be compiled for aarch64-darwin with
  `--enable-archs=i386,x86_64,aarch64`. WOW64 support for ARM64EC
  introduced in Wine 9.0 (January 2024).

- **UTM:** QEMU-based VM manager for macOS. Supports x86_64 emulation
  via QEMU TCG on Apple Silicon. Uses Hypervisor.framework for ARM VMs.
  https://github.com/utmapp/UTM

- **QEMU TCG:** QEMU's Tiny Code Generator. x86_64→ARM64 backend exists
  and is actively maintained. User-mode (`qemu-x86_64`) supports Linux
  guest on ARM64 host. No macOS user-mode support.

- **Apple ARM64EC / TSO mode:** Apple Silicon supports TSO memory model
  (`FEAT_XNX`). Rosetta 2 uses this. No public API.
