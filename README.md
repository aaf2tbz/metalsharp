# MetalSharp

**Direct3D → Metal translation layer. Run Windows games natively on macOS.**

No VM. No Vulkan middleman. Single-hop D3D → Metal.

```
Current:  Game → D3D → DXVK → Vulkan → MoltenVK → Metal → GPU  (4 hops)
MetalSharp: Game → D3D → MetalSharp → Metal → GPU              (2 hops)
```

---

## What's New

**Native PE Loader** — MetalSharp now loads and executes Windows x86_64 executables directly on macOS via Rosetta 2, no Wine required. The loader maps PE sections, processes 20K+ relocations, resolves 339 imports across 14 DLLs, handles structured exception handling, and patches Control Flow Guard dispatch tables — all natively.

**steam.exe runs.** Windows Steam's CRT initialization completes cleanly through the native PE loader. The process reaches main initialization, allocates TLS/FLS slots, and exits with a clean status code. This is the foundation for running Steam and its games without Wine.

**CFG Initialization** — The loader reads the PE's LoadConfig directory, finds GuardCFCheckFunctionPointer and GuardCFDispatchFunctionPointer entries, and patches them with an allow-all stub (`mov eax, 1; ret`). This resolves the crash that prevented statically-linked VC++ CRT binaries from initializing.

**Crash Diagnostics** — SIGSEGV/SIGBUS handler prints full x86_64 register dump (RIP, RSP, RAX, RBX, RCX, RDX), faulting address, crash RVA within the PE image, and a 16-entry stack trace with PE RVA resolution.

---

## Current Status

### Rendering Pipeline — Complete

| Component | Status |
|-----------|--------|
| Metal device init (MTLDevice + MTLCommandQueue) | **Done** |
| D3D11 device + immediate context (full COM) | **Done** |
| D3D12 device + command queue + command lists | **Done** |
| DXGI swap chain → CAMetalLayer | **Done** |
| DXBC bytecode parser + DXBC→MSL translator | **Done** |
| Vertex/index/constant buffers → MTLBuffer | **Done** |
| Texture 1D/2D/3D with mipmaps, MSAA, initial data | **Done** |
| State objects (blend, rasterizer, depth stencil, sampler) | **Done** |
| 8 render targets with per-RT blend | **Done** |
| Input layouts → MTLVertexDescriptor | **Done** |
| Draw calls: Draw, DrawIndexed, DrawInstanced, DrawIndexedInstanced | **Done** |
| Map/Unmap, CopyResource, GenerateMips | **Done** |
| DXGI format → MTLPixelFormat (40+ formats) | **Done** |
| Deferred context (multithreaded rendering) | **Done** |

### Native PE Loader — Working

| Feature | Status |
|---------|--------|
| PE32+ (AMD64) parsing + section mapping | **Done** |
| 20K+ base relocations (DIR64) | **Done** |
| Import resolution across 14 DLLs (339/339) | **Done** |
| Dynamic DLL loading (LoadLibrary/GetProcAddress) | **Done** |
| 51 api-ms-win-* API-set redirects | **Done** |
| Structured Exception Handling (SEH chain) | **Done** |
| Control Flow Guard (CFG) initialization | **Done** |
| CRT init (security cookies, TLS, constructors) | **Done** |
| x86_64 + Rosetta 2 (MSABI on all shims) | **Done** |
| Crash handler with register dump + RVA resolution | **Done** |

### Win32 Shims — 339 Functions Registered

| DLL | Count | Key Functions |
|-----|-------|---------------|
| KERNEL32 | 202 | Memory (VirtualAlloc, HeapAlloc), Threading (CreateThread, pthreads), Sync (CriticalSection, SRWLock), Time, TLS, Module loading |
| USER32 | 57 | Window management (stub), Message pump (stub), Input (stub) |
| GDI32 | 19 | Device contexts, fonts, bitmaps (stubs) |
| ADVAPI32 | 14 | Registry (stub — returns FILE_NOT_FOUND), Security |
| WS2_32 | 28 | **Real networking** — socket, connect, send, recv, bind, listen, accept, getaddrinfo |
| SHELL32 | 4 | Known folders (stubs) |
| ole32 | 1 | CoTaskMemFree |
| OLEAUT32 | 1 | SysAllocString |
| CRYPT32 | 7 | Certificate functions (stubs) |
| PSAPI | 3 | Process info (stubs) |
| VERSION | 1 | Version queries (stub) |
| COMCTL32 | 1 | Common controls (stub) |
| WS2_32 | 28 | Winsock with real POSIX delegation |
| WSOCK32 | 1 | Legacy winsock |
| bcrypt | 1 | BCryptGenRandom |
| ntdll | 15 | Heap, CRC32, critical sections, memory ops |

### Electron App — Built

| Feature | Status |
|---------|--------|
| Game library grid with Play/Stop | **Done** |
| Rust HTTP backend (scan, launch, kill, steam status) | **Done** |
| Steam game download via SteamCMD | **Done** |
| Settings panel (Steam config, launch defaults) | **Done** |

### Tests — 12/12 Passing

---

## What's In Progress

See [ROADMAP.md](ROADMAP.md) for the full phased plan (phases 8–16).

| Phase | Description | Effort |
|-------|-------------|--------|
| **8** | Real File I/O, Registry, Environment Variables | 2-3 days |
| **9** | Window Management (NSWindow-backed HWND, message pump, input) | 3-4 days |
| **10** | Networking (async Winsock, SSL, named pipes) | 3-4 days |
| **11** | Real Threading & Synchronization | 2-3 days |
| **12** | PE Loader Hardening (TLS callbacks, delay imports, resources) | 3-4 days |
| **13** | D3D ↔ PE Loader Integration | 2-3 days |
| **14** | First Game Validation | 2-3 days |
| **15** | Steam Full Run (login, UI, game library) | 3-4 days |
| **16** | Download & Launch a Game | 3-4 days |

**Goal:** Launch Steam from the Electron app, login, download a game, play it.

---

## Architecture

```
Windows Game (.exe)
        │
        ├──────────────────────────────────────┐
        ▼                                      ▼
   Native PE Loader                    Wine (legacy mode)
   (Rosetta 2, x86_64)                (DLL injection)
        │                                      │
        ▼                                      ▼
   MetalSharp Win32 Shims             MetalSharp DLL Shims
   (kernel32, user32, ws2_32,         (d3d11, d3d12, dxgi,
    advapi32, gdi32, ntdll,            xaudio2_9, xinput1_4)
    + 51 api-ms-win-* sets)
        │                                      │
        └──────────────────┬───────────────────┘
                           ▼
                   MetalSharp Core
                   ├── Device      → MTLDevice
                   ├── Command     → MTLCommandQueue / MTLCommandBuffer
                   ├── Pipeline    → MTLRenderPipelineState
                   ├── Shader      → DXBC → MSL → Metal library
                   ├── Resources   → MTLBuffer, MTLTexture, MTLSamplerState
                   └── Framebuffer → MTLRenderPassDescriptor
                           │
                           ▼
                   Apple Metal → GPU
```

---

## Build

### Quick Install

```bash
git clone https://github.com/aaf2tbz/metalsharp.git && cd metalsharp && ./install.sh
```

Checks for dependencies (Homebrew, CMake, Wine, SteamCMD), builds, runs tests, sets up the Wine prefix. Interactive with confirmation prompts.

### Manual Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build build -j$(sysctl -n hw.ncpu)
cd build && ctest --output-on-failure
```

### Build Targets

| Target | Output | Purpose |
|--------|--------|---------|
| `metalsharp_core` | static lib | Metal backend, DXBC parser, MSL translator, perf subsystems |
| `metalsharp_d3d11` | `d3d11.dylib` | D3D11 API shim |
| `metalsharp_d3d12` | `d3d12.dylib` | D3D12 API shim |
| `metalsharp_dxgi` | `dxgi.dylib` | DXGI swap chain, adapter, output enumeration |
| `metalsharp_audio` | `xaudio2_9.dylib` | XAudio2 → CoreAudio bridge |
| `metalsharp_input` | `xinput1_4.dylib` | XInput → GameController bridge |
| `metalsharp_loader` | static lib | Native PE loader + Win32 shims |
| `metalsharp` | executable | Native PE launcher (runs .exe directly, no Wine) |
| `metalsharp_launcher` | executable | Wine-based launcher, SteamCMD integration |

### Launch a Game

```bash
# Native PE loader (no Wine)
./build/metalsharp path/to/game.exe

# Wine-based launcher (DLL injection mode)
./build/metalsharp_launcher game.exe

# Steam game download + launch
./build/metalsharp_launcher --steam 730

# List your Steam library
./build/metalsharp_launcher --list-games
```

---

## Project Structure

```
src/
├── d3d/
│   ├── d3d11/          D3D11 device, context, resources, state objects
│   └── d3d12/          D3D12 device, command queue, command lists
├── dxgi/               DXGI factory, adapter, swap chain, output enumeration
├── metal/
│   ├── device/         MTLDevice wrapper
│   ├── command/        MTLCommandQueue, MTLCommandBuffer
│   ├── pipeline/       Pipeline state creation
│   ├── shader/         DXBC parser, DXBC→MSL translator
│   ├── Buffer.mm       MTLBuffer wrapper
│   ├── Texture.mm      MTLTexture 1D/2D/3D
│   ├── Sampler.mm      MTLSamplerState + format translation
│   └── Framebuffer.mm  MTLRenderPassDescriptor
├── loader/             Native PE loader (parse, map, reloc, imports, CFG)
├── win32/              Win32 API shims (kernel32, user32, ws2_32, advapi32, etc.)
├── runtime/            Logger, PEHook
├── audio/              XAudio2 → CoreAudio
├── input/              XInput → GameController
└── perf/               ShaderCache, PipelineCache, BufferPool, FramePacer,
                       GPUProfiler, CommandBatcher, MetalFXUpscaler
app/
├── src/main/           Electron main process + Rust bridge
├── src/renderer/       Game library UI, settings, store
└── src-rust/           Rust HTTP backend (launch, scan, steam integration)
tools/launcher/         CLI launchers (NativeLauncher + WineLauncher)
tests/                  12 test suites
```

---

## How It Works

### Native PE Loader

The `metalsharp` executable loads Windows x86_64 PE files directly on macOS:

1. **Parse PE headers** — DOS header, COFF header, optional header, section table
2. **Map sections** — `mmap` a region of `SizeOfImage` bytes, copy each section from the raw PE data to its virtual address
3. **Process relocations** — Walk the `.reloc` section, apply `IMAGE_REL_BASED_DIR64` fixups for the delta between preferred and actual load address
4. **Resolve imports** — Walk import descriptors, match DLL names to registered shims, resolve function names/ordinals to shim addresses, write into the IAT
5. **Initialize CFG** — Read LoadConfig, patch GuardCF function pointers with allow-all stub
6. **Handle exceptions** — `RtlCaptureContext` captures PE context, `RtlLookupFunctionEntry` searches `.pdata`, `UnhandledExceptionFilter` logs and continues
7. **Call entry point** — Jump to the PE's entry point via ms_abi trampoline

### D3D Translation

Shaders take two paths:

1. **MSL source** — compiled with `newLibraryWithSource`
2. **DXBC bytecode** — parsed (SHDR/SHEX chunks, input/output signatures, SM5.0 tokens) → MSL generated → compiled to Metal library

The `D3D11DeviceContext` caches a `MTLRenderPipelineState` from current state (shaders, blend, rasterizer, depth). Draw calls encode directly into a `MTLCommandBuffer`. Pipeline state rebuilds automatically when state changes.

---

## Requirements

- macOS 13+ (Ventura or later)
- Apple Silicon Mac (M1/M2/M3/M4) or Intel Mac with Metal support
- Xcode Command Line Tools
- CMake 3.24+
- C++20 compiler (Apple Clang 15+)
- Wine (optional, for legacy DLL injection mode)
- SteamCMD (optional, for Steam game downloads)

## License

MIT

## References

- [DXVK](https://github.com/doitsujin/dxvk) — D3D → Vulkan reference
- [MoltenVK](https://github.com/KhronosGroup/MoltenVK) — Vulkan → Metal reference
- [Apple Game Porting Toolkit](https://developer.apple.com/games/game-porting-toolkit/)
- [Wine](https://gitlab.winehq.org/wine/wine) — Win32 API translation
- [D3D11 API](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/)
- [D3D12 API](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/)
- [Metal API](https://developer.apple.com/documentation/metal)
