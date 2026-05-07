# MetalSharp Wine Architecture

MetalSharp Wine is a custom Wine 11.0 runtime that runs Windows applications on macOS via Rosetta 2 (x86_64). It replaces the old multi-runtime assembly (Wine Devel + GPTK overlays + CrossOver) with a single self-contained tree at `~/.metalsharp/runtime/wine/`.

## Runtime layout

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
│   │   ├── x86_64-windows/ 64-bit PE DLLs (Wine builtins — untouched)
│   │   │                       d3d11.dll = Wine's D3DMetal-backed builtin (4.5MB)
│   │   └── i386-windows/   32-bit PE DLLs (DXMT builtins replace originals)
│   │                           d3d11.dll = DXMT (5.4MB, replaces Wine builtin)
│   │                           dxgi.dll  = DXMT (1.8MB)
│   │                           d3d10core.dll = DXMT (1.4MB)
│   │                           winemetal.dll = DXMT bridge (68KB)
│   │                           Originals backed up as .bak
│   └── dxmt/
│       ├── x86_64-unix/    winemetal.so (31MB, Mach-O x86_64)
│       │                       Metal command buffer bridge — the only .so
│       │                       DXMT produces. No i386-unix version exists.
│       ├── x86_64-windows/ 64-bit DXMT PE DLLs (d3d11, dxgi, nvapi64, nvngx)
│       └── i386-windows/   32-bit DXMT PE DLLs (d3d11, dxgi, d3d10core, winemetal)
└── share/
    └── wine/               Wine data files (fonts, inf, nls)
```

## How it works

### WoW64 architecture

MetalSharp Wine uses WoW64 (Windows on Windows 64) to run 32-bit Windows PE binaries on the 64-bit Wine server:

```
32-bit PE game (e.g., Nidhogg 2)
  → loads i386-windows/d3d11.dll (DXMT, PE32)
    → DXMT DLL calls into winemetal.so (x86_64-unix, Mach-O)
      → winemetal.so creates Metal command buffers
        → Metal framework renders to screen
```

There is **no i386-unix directory**. All unix .so modules are x86_64 only. When a 32-bit PE DLL needs to call into a unix .so, Wine's WoW64 layer handles the 32→64 bit transition. This is why `winemetal.so` is the only DXMT unix binary — it's always x86_64 regardless of whether the game is 32 or 64-bit.

### The CrossOver WoW64 bug and the builtin fix

Wine's `WINEDLLOVERRIDES=d3d11=native` tells Wine to load a DLL from the game directory instead of its builtins. For 64-bit processes this works fine. For 32-bit processes on macOS, Wine's `load_path` resolves as `(null)` when searching for native overrides — a WoW64 bug inherited from CrossOver's patches. This means `status c0000135` (DLL not found) for any native override in a 32-bit process.

**The fix:** Replace Wine's builtin DLLs in `i386-windows/` directly with DXMT's versions. Wine loads builtins from its own directory without the override mechanism, so the bug is bypassed entirely. The original Wine builtins are preserved as `.bak` files.

The 64-bit builtins are left untouched — `WINEDLLOVERRIDES` works correctly for 64-bit processes, and Steam itself is 64-bit and needs Wine's original D3DMetal builtins.

### DYLD_FALLBACK_LIBRARY_PATH

The `winemetal.so` unix library lives outside Wine's standard search paths. The wrapper exports:

```
DYLD_FALLBACK_LIBRARY_PATH="$MS_LIB:$MS_LIB/wine/x86_64-unix:$MS_LIB/dxmt/x86_64-unix:..."
```

Without this, the PE→unix bridge can't locate `winemetal.so` at runtime and D3D11 device creation fails.

## Environment variables

The `metalsharp-wine` wrapper sets these before dispatching to the backend:

| Variable | Value | Purpose |
|----------|-------|---------|
| `MS_ROOT` | `~/.metalsharp/runtime/wine` | Base of the Wine tree |
| `CX_ROOT` | Same as `MS_ROOT` | Back-compat — some Wine patches check this |
| `WINEPREFIX` | `~/.metalsharp/prefix-steam` | Default prefix (overridable) |
| `WINEDLLPATH` | `lib/wine/x86_64-windows:lib/wine/i386-windows` | Where Wine finds PE builtins |
| `WINELOADER` | `bin/wineloader` | Which Wine binary to use |
| `WINESERVER` | `bin/wineserver` | Which wineserver to use |
| `DYLD_FALLBACK_LIBRARY_PATH` | Includes `lib/`, `lib/wine/x86_64-unix/`, `lib/dxmt/x86_64-unix/` | Unix .so resolution |
| `MS_BACKEND` | User-set | Selects graphics backend |

## Why mscompatdb is dead

The old `mscompatdb.so` was a rules engine that hooked `ntdll` syscalls to apply per-game DLL overrides, env vars, and command line patches. It was fragile — the ntdll hooking approach broke when Wine's internal syscall patterns changed between versions. It's now disabled (`.so.disabled`).

Game routing is handled by:
1. **`launch.rs`** — Rust code that maps app IDs to engine types and sets the right env vars
2. **`metalsharp-wine` wrapper** — the `MS_BACKEND` env var selects the graphics pipeline
3. **DXMT i386 builtins** — no DLL override configuration needed for 32-bit games

## What came from CrossOver

MetalSharp Wine is built from CrossOver's open-source Wine patches (Wine 11.0). The contributions include:
- gnutls TLS support (Steam login)
- freetype font rendering
- macOS-specific fixes (macdrv, DIEM, CEF compatibility)
- The WoW64 implementation used for 32-bit game support

No CrossOver proprietary code is included. `cxcompatdb.so` and CrossOver's bottle system are not used.
