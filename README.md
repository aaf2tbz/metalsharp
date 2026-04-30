# MetalSharp

**Direct3D → Metal translation layer. Run Windows games natively on macOS.**

No VM. No Vulkan middleman. Single-hop D3D → Metal.

```
Current:  Game → D3D → DXVK → Vulkan → MoltenVK → Metal → GPU  (4 hops)
MetalSharp: Game → D3D → MetalSharp → Metal → GPU              (2 hops)
```

---

## What's New

**All 24 phases complete.** MetalSharp loads Windows x86_64 executables via native PE loader on Rosetta 2, translates D3D11/D3D12 calls to Metal, manages Win32 APIs through comprehensive shims, and ships with a full Electron + Rust frontend for game library management, Steam integration, and download/launch workflows.

**Production Ready** — Auto-detects games from Steam, Epic, and GOG. Automatic crash reporting with log collection. Update checker via GitHub Releases. Full settings management (resolution, upscaling, shader cache). DMG installer for macOS. CI/CD for ARM64 and x86_64. Complete documentation suite.

**Native PE Loader** — Loads Windows x86_64 PE files directly on macOS. Maps sections, processes relocations, resolves imports across 14+ DLLs (339 functions), handles TLS callbacks, delay-load imports, export forwarding, structured exception handling, Control Flow Guard, and resource loading — all natively, no Wine required.

**D3D → Metal Rendering** — Full D3D11 and D3D12 device/context implementations backed by Metal. DXBC bytecode parsed and translated to MSL. Shader and pipeline caches persisted to disk for fast subsequent launches.

**Electron App** — Game library grid, Steam download with live progress, Rust HTTP backend, launch mode selector (Native PE / Wine), log viewer, error reporting.

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
| Import resolution across 14+ DLLs (339/339) | **Done** |
| Dynamic DLL loading (LoadLibrary/GetProcAddress) | **Done** |
| 51 api-ms-win-* API-set redirects | **Done** |
| TLS callbacks + delay-load imports | **Done** |
| Export forwarding + resource loading | **Done** |
| Structured Exception Handling (SEH chain) | **Done** |
| Control Flow Guard (CFG) initialization | **Done** |
| CRT init (security cookies, TLS, constructors) | **Done** |
| Fake TEB + PEB for Windows CRT (%gs:0x30) | **Done** |
| Crash handler with register dump + RVA resolution | **Done** |

### Win32 Shims — 400+ Functions

| DLL | Count | Key Functions |
|-----|-------|---------------|
| KERNEL32 | 220+ | Memory, threading (pthreads), sync, file I/O, time, TLS, modules, CreateProcessW |
| USER32 | 60+ | Window management (NSWindow), message pump, input translation, wsprintfA |
| GDI32 | 30+ | BitBlt, StretchBlt, DrawTextW, CreateFontIndirectW, bitmaps, brushes |
| ADVAPI32 | 14+ | Registry (real in-memory store), security |
| WS2_32 | 28+ | Real networking — socket, connect, send, recv, SSL/TLS |
| OLE32 | 12 | CoInitialize, CoCreateInstance, CoTaskMemAlloc |
| OLEAUT32 | 14 | SysAllocString, VariantInit, SafeArray |
| VERSION | 3+ | GetFileVersionInfoW, VerQueryValueW |
| ntdll | 15+ | Heap, CRC32, critical sections, RtlCaptureContext |
| bcrypt | 1 | BCryptGenRandom |
| + 51 api-ms-win-* sets | — | CRT runtime, heap, stdio, string, math, locale, time, environment |

### Electron App

| Feature | Status |
|---------|--------|
| Game library grid with Play/Stop + cover art | **Done** |
| Rust HTTP backend (scan, launch, kill, steam, config) | **Done** |
| Steam game download with live progress | **Done** |
| Steam login state detection | **Done** |
| Launch mode selector (PE Loader / Wine) | **Done** |
| Log viewer with monospace display | **Done** |
| Error dialogs for crashes/failures | **Done** |
| Settings panel with graphics/shader cache/updates | **Done** |
| Auto-detect games from Steam/Epic/GOG | **Done** |
| Crash reporter with automatic log collection | **Done** |
| Update checker via GitHub Releases | **Done** |

### Performance Pipeline — Complete

| Component | Status |
|-----------|--------|
| Shader cache (metallib binary, LRU eviction, 2GB default) | **Done** |
| Pipeline state cache (serialized descriptors, binary index) | **Done** |
| Render thread pool + command buffer pooling | **Done** |
| Frame pacing (VSync/Immediate/Adaptive, triple buffering, percentiles) | **Done** |
| MetalFX spatial upscaler integration | **Done** |
| GPU profiler with Metal GPU timing | **Done** |

### Audio Pipeline — Complete

| Component | Status |
|-----------|--------|
| CoreAudio AudioUnit with real render callback | **Done** |
| XAudio2 source voice with buffer submission | **Done** |
| X3DAudio positional audio (distance, panning, Doppler) | **Done** |
| DirectSound backend for legacy games | **Done** |

### Anti-Cheat & Game Validation — Complete

| Component | Status |
|-----------|--------|
| DRM/anti-cheat binary signature scanner (30 signatures) | **Done** |
| Compatibility database (Platinum/Gold/Silver/Bronze/Broken) | **Done** |
| Import resolution reporter | **Done** |
| Crash diagnostics with dump-to-file | **Done** |
| Game validator orchestrator | **Done** |
| Game auto-detection (Steam/Epic/GOG/local) | **Done** |

### Tests — 21/21 Passing

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
    ole32, oleaut32, version,
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

### Electron App

```bash
cd app
npm install
npm run rust:build          # Build Rust backend
npm run build               # Build TypeScript + copy HTML
npm run start               # Launch Electron app
```

### Build Targets

| Target | Output | Purpose |
|--------|--------|---------|
| `metalsharp_core` | static lib | Metal backend, DXBC parser, MSL translator, perf, runtime subsystems |
| `metalsharp_d3d11` | `d3d11.dylib` | D3D11 API shim (DLL injection mode) |
| `metalsharp_d3d12` | `d3d12.dylib` | D3D12 API shim (DLL injection mode) |
| `metalsharp_dxgi` | `dxgi.dylib` | DXGI swap chain, adapter, output enumeration |
| `metalsharp_audio` | `xaudio2_9.dylib` | XAudio2 → CoreAudio bridge |
| `metalsharp_input` | `xinput1_4.dylib` | XInput → GameController bridge |
| `metalsharp_loader` | static lib | Native PE loader + all Win32 shims + D3D/DXGI shims |
| `metalsharp` | executable | Native PE launcher (runs .exe directly) |
| `metalsharp_launcher` | executable | Wine-based launcher, SteamCMD integration |

### Launch a Game

```bash
# Native PE loader (no Wine)
./build/metalsharp path/to/game.exe

# Wine-based launcher (DLL injection mode)
./build/metalsharp_launcher game.exe

# Steam game download + launch (via Electron app)
cd app && npm run start
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
│   ├── shader/         DXBC parser, DXBC→MSL, IRConverter bridge, argument buffers
│   ├── Buffer.mm       MTLBuffer wrapper
│   ├── Texture.mm      MTLTexture 1D/2D/3D
│   ├── Sampler.mm      MTLSamplerState + format translation
│   └── Framebuffer.mm  MTLRenderPassDescriptor
├── loader/             Native PE loader (parse, map, reloc, imports, CFG)
├── win32/              Win32 API shims
│   ├── kernel32/       KERNEL32, ntdll, VirtualFileSystem, Registry, networking, sync
│   ├── user32/         USER32, WindowManager (NSWindow-backed HWND)
│   └── extra/          GDI32, ADVAPI32, SHELL32, OLE32, OLEAUT32, VERSION, etc.
├── runtime/            Logger, PEHook, GameDetector, CrashReporter, UpdateChecker,
│                       SettingsManager, CompatDatabase, ImportReporter, CrashDiagnostics,
│                       DRMDetector, GameValidator
├── audio/              XAudio2 → CoreAudio, X3DAudio, DirectSound
├── input/              XInput → GameController
└── perf/               ShaderCache, PipelineCache, BufferPool, FramePacer,
                       GPUProfiler, CommandBatcher, MetalFXUpscaler, RenderThreadPool
app/
├── src/main/           Electron main process + Rust bridge
├── src/renderer/       Game library UI, settings, store, logs
├── src/shared/         TypeScript types
└── src-rust/           Rust HTTP backend (launch, scan, steam, config, logs)
tools/
├── launcher/           CLI launchers (NativeLauncher + WineLauncher)
└── dmg/                DMG installer + dependency checker
tests/                  21 test suites
docs/                   Architecture, user guide, compat guide, dev guide, troubleshooting
```

---

## Documentation

- [Architecture Overview](docs/ARCHITECTURE.md) — system design, data flow, component relationships
- [User Guide](docs/USER-GUIDE.md) — installation, configuration, launching games
- [Compatibility Guide](docs/COMPATIBILITY.md) — game compatibility status, DRM detection
- [Developer Guide](docs/DEVELOPER-GUIDE.md) — contributing, adding shims, extending D3D
- [Troubleshooting](docs/TROUBLESHOOTING.md) — common issues and solutions
- [PE Loader](docs/PE-LOADER.md) — how the native PE loader works internally
- [Win32 Shims](docs/WIN32-SHIMS.md) — Win32 API shim implementation guide
- [Electron App](docs/ELECTRON-APP.md) — frontend architecture, Rust backend API
- [Roadmap](ROADMAP.md) — full phased plan (phases 1–24, all complete)

---

## How It Works

### Native PE Loader

The `metalsharp` executable loads Windows x86_64 PE files directly on macOS:

1. **Parse PE headers** — DOS header, COFF header, optional header, section table
2. **Map sections** — `mmap` a region of `SizeOfImage` bytes, copy each section to its virtual address
3. **Process relocations** — Walk `.reloc`, apply `IMAGE_REL_BASED_DIR64` fixups for load address delta
4. **Resolve imports** — Walk import descriptors, match DLL names to registered shims, resolve to shim addresses
5. **Process delay imports** — Lazy-resolve delay-load imports on first call
6. **Handle TLS callbacks** — Walk TLS directory, fire callbacks with `DLL_PROCESS_ATTACH`
7. **Initialize CFG** — Read LoadConfig, patch GuardCF function pointers with allow-all stub
8. **Apply section protections** — mprotect sections based on characteristics (RX, R, RW)
9. **Set up TEB/PEB** — Allocate fake TEB with stack limits, PEB pointer, set `%gs:0x30`
10. **Call entry point** — Jump to PE entry via ms_abi trampoline

### D3D Translation

Shaders take two paths:

1. **Apple IRConverter** — DXIL bytecode compiled to metallib via `libmetalirconverter` (primary path when installed)
2. **DXBC→MSL** — DXBC bytecode parsed (SHDR/SHEX chunks, input/output signatures, SM5.0 tokens) → MSL generated → compiled to Metal library (fallback for SM ≤ 5.0)

The `D3D11DeviceContext` caches a `MTLRenderPipelineState` from current state (shaders, blend, rasterizer, depth). Draw calls encode directly into a `MTLCommandBuffer`. Pipeline state rebuilds automatically when state changes. Shader and pipeline caches persist to `~/.metalsharp/cache/` for fast subsequent launches.

### Win32 API Shims

Games call Win32 APIs through import tables. The PE loader resolves these to shim functions that delegate to macOS APIs:

- **kernel32** → POSIX file I/O, pthreads, mmap, getenv, clock_gettime
- **user32** → NSWindow, NSEvent, NSPasteboard, message queue
- **gdi32** → Basic software rendering (BitBlt, DrawTextW)
- **ws2_32** → POSIX sockets, Security.framework for TLS
- **advapi32** → In-memory registry with JSON persistence
- **ole32/oleaut32** → COM initialization stubs, SafeArray, Variant

All shim functions use `__attribute__((ms_abi))` to match the Windows x86_64 calling convention that PE code expects.

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
