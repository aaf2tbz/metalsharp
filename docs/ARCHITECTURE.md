# Architecture Overview

MetalSharp is a Direct3D-to-Metal translation layer that runs Windows games natively on macOS. No VM, no Vulkan middleman.

## Translation Chain

```
Current:    Game → D3D → DXVK → Vulkan → MoltenVK → Metal → GPU  (4 hops)
MetalSharp: Game → D3D → metalsharp → Metal → GPU                (2 hops)
```

## System Layers

### 1. PE Loader (`tools/launcher/NativeLauncher.cpp`, `src/loader/PELoader.cpp`)

Loads Windows x86_64 PE executables directly on macOS via Rosetta 2:

- Parses DOS/COFF/Optional headers and section tables
- Maps PE sections into mmap'd memory with correct virtual addresses
- Processes base relocations (DIR64 fixups) for load address delta
- Resolves imports by matching DLL names to registered shim libraries
- Handles delay-load imports, TLS callbacks, export forwarding
- Initializes Control Flow Guard (CFG) with allow-all stubs
- Sets up fake TEB/PEB for Windows CRT compatibility (`%gs:0x30`)
- Registers crash handler (SIGSEGV/SIGBUS) with x86_64 register dumps

### 2. Win32 Shim Layer (`src/win32/`)

400+ functions across 14+ DLLs that translate Win32 API calls to POSIX/macOS equivalents:

| DLL | Delegates to |
|-----|-------------|
| KERNEL32 | POSIX file I/O, pthreads, mmap, getenv |
| USER32 | NSWindow, NSEvent, NSPasteboard |
| GDI32 | Software rendering (BitBlt, DrawTextW) |
| WS2_32 | POSIX sockets + Security.framework TLS |
| ADVAPI32 | In-memory registry with JSON persistence |
| OLE32/OLEAUT32 | COM initialization stubs, SafeArray, Variant |
| VERSION | Fake version resource with VS_FIXEDFILEINFO |
| ntdll | Heap, CRC32, critical sections, context capture |

All shim functions use `__attribute__((ms_abi))` to match the Windows x86_64 calling convention.

### 3. D3D/DXGI Shim Layer (`src/d3d/`, `src/dxgi/`)

D3D11 and D3D12 COM interfaces backed by Metal:

- `D3D11Device` / `D3D11DeviceContext` → MTLDevice + MTLCommandQueue
- `DXGISwapChain` → CAMetalLayer, Present → drawable present + event pump
- Resources → MTLBuffer, MTLTexture, MTLSamplerState
- State objects → Metal pipeline state descriptors
- Shader translation → DXBC bytecode parsed → MSL generated → Metal library compiled

### 4. Metal Backend (`src/metal/`)

RAII wrappers around Metal API objects. All Objective-C/Metal calls isolated in `.mm` files:

- Device, CommandQueue, CommandBuffer, CommandEncoder wrappers
- Pipeline state creation (render + compute)
- DXBC parser + DXBC→MSL translator
- Apple IRConverter bridge (DXIL→Metal, runtime dlopen)
- Argument buffer manager (root constant/descriptor/table encoding)
- Shader cache (FNV-1a hash → metallib on disk, LRU eviction)
- Pipeline cache (descriptor hash → binary index on disk)

### 5. Performance Pipeline (`src/perf/`)

- `ShaderCache` — metallib binary storage, LRU eviction (configurable max MB), auto entry point discovery
- `PipelineCache` — serialized descriptors, LRU deque, binary index persistence
- `RenderThreadPool` — configurable worker pool, command buffer pooling
- `FramePacer` — PresentMode (VSync/Immediate/HalfRate/Adaptive), triple buffering, frame time percentiles
- `MetalFXUpscaler` — runtime dlopen for MetalFX, spatial upscaling with sharpness
- `GPUProfiler` — Metal GPU timing (GPUStartTime/GPUEndTime), per-pass CPU+GPU timing
- `CommandBatcher` — command buffer batching
- `BufferPool` — MTLBuffer recycling

### 6. Audio Pipeline (`src/audio/`)

- `CoreAudioBackend` — real AudioUnit render callback, buffer queue, hardware volume
- `XAudio2Engine` — source voice with buffer submission
- `X3DAudioEngine` — positional audio (distance attenuation, stereo panning, Doppler)
- `DirectSoundBackend` — legacy audio fallback

### 7. Runtime Services (`src/runtime/`)

- `GameDetector` — auto-detect games from Steam (VDF/ACF), Epic (.item), GOG, local dirs
- `CrashReporter` — session tracking, automatic log collection, crash dump persistence
- `UpdateChecker` — semantic version comparison, GitHub Releases API
- `SettingsManager` — JSON-persisted render/upscaling/cache/launch configuration
- `CompatDatabase` — game compatibility DB (Platinum/Gold/Silver/Bronze/Broken)
- `ImportReporter` — structured import resolution tracking
- `CrashDiagnostics` — crash dump to file with register state + RVA resolution
- `DRMDetector` — binary signature scanner (30 anti-cheat/DRM/engine signatures)
- `GameValidator` — validation protocol orchestrator

### 8. Electron App (`app/`)

TypeScript frontend + Rust HTTP backend:

- **Rust backend** (`app/src-rust/`): tiny_http server on port 9274
  - `/scan` — detect games in Steam prefix + local dirs
  - `/launch` — launch game via NativeLauncher or Wine
  - `/config` — persist launch mode (native/wine) to `~/.metalsharp/config.json`
  - `/steam/status` — detect Steam install + login state (reads `loginusers.vdf`)
  - `/steam/download-game` — SteamCMD with live progress reporting
  - `/logs` — serve runtime log files
- **Electron frontend** (`app/src/renderer/`): game library grid with cover art, settings (graphics/shader cache/updates), store, log viewer, crash reports
- **Rust bridge** (`app/src/main/rust-bridge.ts`): spawns Rust binary, HTTP request wrapper

## Data Flow

```
Windows .exe
    │
    ▼
NativeLauncher
    ├── PELoader::load() → mmap, reloc, imports
    ├── Register all shims (kernel32, user32, d3d11, dxgi, ...)
    ├── Set up fake TEB (%gs:0x30)
    └── Call PE entry point
         │
         ▼
    PE code runs (x86_64 via Rosetta 2)
         │
         ├── Calls CreateFileW → shim → POSIX open()
         ├── Calls CreateWindowExW → shim → NSWindow
         ├── Calls D3D11CreateDevice → shim → MTLDevice
         ├── Calls DrawIndexed → shim → MTLCommandBuffer encoding
         └── Calls Present → MTLDrawable present + NSEvent pump
              │
              ▼
         Metal renders to screen
```

## File Layout Conventions

| Extension | Purpose |
|-----------|---------|
| `.cpp` | Portable C++ logic |
| `.mm` | Objective-C++ — Metal/ObjC API calls only |
| `.h` | C++ headers |
| `.rs` | Rust backend (app/src-rust/) |
| `.ts` | Electron frontend (app/src/) |

## Key Design Decisions

1. **MSABI everywhere** — All shim functions use `__attribute__((ms_abi))` because PE code uses the Windows x86_64 calling convention. Native macOS code uses SystemV ABI. The trampoline layer handles the translation.

2. **No runtime dependencies** — The native PE loader doesn't need Wine, DXVK, MoltenVK, or Vulkan. It loads PE files directly and translates D3D calls to Metal in a single hop.

3. **Static linking for PE loader** — `metalsharp_loader` is a static library containing the PE loader + all Win32 shims + D3D/DXGI shims. The `metalsharp` executable links against it. This means D3D shim resolution happens through the same import table as kernel32.

4. **Shader cache keyed by FNV-1a hash** — DXBC bytecode is hashed, and the resulting MSL is cached to `~/.metalsharp/cache/shader_cache/<hash>.msl`. On subsequent loads, the cache is checked before recompiling.

5. **Pipeline cache keyed by descriptor hash** — Pipeline state descriptors are hashed, and the resulting MTLRenderPipelineState is cached. An LRU eviction policy keeps the cache bounded. A binary index file persists across sessions.

6. **IRConverter runtime loading** — `libmetalirconverter` is loaded via `dlopen` at runtime. If not installed, DXBC→MSL fallback handles shader translation. This avoids a hard dependency on Apple's Metal Shader Converter.

7. **JSON for persistence** — Compatibility database, settings, and crash reports use simple JSON/text formats. No SQLite or other database dependencies. Consistent with the project's zero-runtime-dependency philosophy.
