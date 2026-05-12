# MetalSharp Wine Architecture

MetalSharp Wine is a custom Wine 11.5 runtime that runs Windows applications on macOS via Rosetta 2 (x86_64). Single self-contained tree at `~/.metalsharp/runtime/wine/`.

## Runtime Layout

```
~/.metalsharp/runtime/wine/
├── bin/
│   ├── metalsharp-wine     Main entry point — env setup + backend dispatch
│   ├── wine                Symlink to wineloader
│   ├── wineloader          Wine binary (x86_64, runs under Rosetta 2)
│   └── wineserver          Wine server process
├── lib/
│   ├── wine/
│   │   ├── x86_64-unix/    Unix .so modules (ntdll.so, opengl32.so, etc.)
│   │   │                       Includes mscompatdb.so.disabled (dead rules engine)
│   │   ├── x86_64-windows/ 64-bit PE DLLs (Wine builtins)
│   │   └── i386-windows/   32-bit PE DLLs
│   │                           DXMT builtins for d3d11/dxgi/d3d10core/winemetal
│   │                           Originals backed up as .bak
│   └── dxmt/
│       ├── x86_64-unix/    winemetal.so (47MB, Mach-O x86_64)
│       │                       Metal command buffer bridge
│       │                       Exports __wine_unix_call_wow64_funcs for 32-bit
│       └── x86_64-windows/ 64-bit DXMT PE DLLs
│           d3d11.dll (72MB), dxgi.dll (20MB), d3d10core.dll (14MB)
│           d3d12.dll (36MB), winemetal.dll (269KB)
├── etc/
│   └── dxmt.conf           DXMT config (MetalFX upscaling, framerate cap)
└── share/
    └── wine/               Wine data files (fonts, inf, nls)
```

## How It Works

### WoW64 Architecture

MetalSharp Wine uses WoW64 to run 32-bit Windows PE binaries on the 64-bit Wine server:

```
32-bit PE game (e.g., Nidhogg 2)
  → loads i386-windows/d3d11.dll (DXMT, PE32)
    → DXMT DLL calls into winemetal.so (x86_64-unix, Mach-O)
      → Wine's WoW64 handles 32→64 bit transition
        → winemetal.so creates Metal command buffers
          → Metal framework renders to screen
```

There is **no i386-unix directory**. All unix .so modules are x86_64 only. `winemetal.so` exports `__wine_unix_call_wow64_funcs` with full `thunk32_*` implementations for 32-bit PE clients.

### DXMT DLL Injection for 64-bit Games

For DxmtMetal games (Rain World, Schedule I, Subnautica BZ), the Rust backend copies DXMT PE DLLs directly into the game directory on every launch:

```rust
std::fs::copy(dxmt_x64.join("d3d11.dll"), game.join("d3d11.dll"));
std::fs::copy(dxmt_x64.join("dxgi.dll"), game.join("dxgi.dll"));
std::fs::copy(dxmt_x64.join("d3d10core.dll"), game.join("d3d10core.dll"));
std::fs::copy(dxmt_x64.join("winemetal.dll"), game.join("winemetal.dll"));
```

With `WINEDLLOVERRIDES="dxgi,d3d11,d3d10core,winemetal=n,b"`, Wine loads these from the game dir instead of its builtins. The `winebuild --builtin` post-processing ensures they're loaded as proper Wine builtins, not native overrides.

### Why mscompatdb Is Dead

The old `mscompatdb.so` hooked ntdll syscalls for per-game overrides. Fragile — broke when Wine's syscall patterns changed. Now disabled (`.so.disabled`).

Game routing is handled entirely by the Rust backend in `launch.rs`.

## Environment Variables

The `metalsharp-wine` wrapper sets:

| Variable | Value | Purpose |
|----------|-------|---------|
| `MS_ROOT` | `~/.metalsharp/runtime/wine` | Base of Wine tree |
| `CX_ROOT` | Same as `MS_ROOT` | Back-compat for Wine patches |
| `WINEPREFIX` | `~/.metalsharp/prefix-steam` | Default prefix |
| `WINEDLLPATH` | `lib/wine/x86_64-windows:lib/wine/i386-windows` | PE builtin search |
| `WINELOADER` | `bin/wineloader` | Wine binary |
| `WINESERVER` | `bin/wineserver` | Wine server |
| `DYLD_FALLBACK_LIBRARY_PATH` | Includes `lib/wine/x86_64-unix/`, `lib/dxmt/x86_64-unix/` | Unix .so resolution |

## DYLD_FALLBACK_LIBRARY_PATH

Critical for DXMT — `winemetal.so` lives outside Wine's standard search paths. Without it:

```
DYLD_FALLBACK_LIBRARY_PATH="$MS_LIB/wine/x86_64-unix:$MS_LIB/dxmt/x86_64-unix:..."
```

The PE→unix bridge can't locate `winemetal.so` and D3D11 device creation fails.

## Wine Prefix

Single shared prefix at `~/.metalsharp/prefix-steam/`:
- Steam installed inside it (`drive_c/Program Files (x86)/Steam/`)
- External SSD Steam library mapped as F: drive
- `steamwebhelper.exe` wrapper deployed for CEF rendering (re-deploy after Steam auto-updates)

## Building Wine 11.5

From upstream source with 7 custom patches (A-G). Key build details:
- MinGW GCC for PE cross-compilation
- `-fno-function-sections` (MinGW 15.2 linker drops symbols with `-ffunction-sections`)
- `__attribute__((used))` on specific exports
- Bison 3.8+ required (Homebrew, not macOS system bison 2.3)
- freetype 2.13.3, gnutls, MoltenVK linked

## What Came From CrossOver

MetalSharp Wine is built from Wine 11.5 upstream source with 7 custom patches. Contributions include:
- gnutls TLS support (Steam login)
- freetype font rendering
- macOS-specific fixes (macdrv, DIEM, CEF compatibility)
- WoW64 implementation for 32-bit game support

No CrossOver proprietary code. `cxcompatdb.so` and CrossOver's bottle system are not used.
