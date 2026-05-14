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
│   ├── dxmt/
│   │   ├── x86_64-unix/    winemetal.so (47MB, Mach-O x86_64)
│   │   │                       Metal command buffer bridge
│   │   │                       Exports __wine_unix_call_wow64_funcs for 32-bit
│   │   └── x86_64-windows/ 64-bit DXMT PE DLLs
│   │       d3d11.dll (72MB), dxgi.dll (20MB), d3d10core.dll (14MB)
│   │       d3d12.dll (36MB), winemetal.dll (269KB)
│   └── dxvk/
│       └── i386-windows/    DXVK 1.10.3 32-bit DLLs
│           d3d9.dll, d3d11.dll, d3d10core.dll, dxgi.dll
├── etc/
│   ├── dxmt.conf           DXMT config (MetalFX upscaling, framerate cap)
│   └── vulkan/
│       └── icd.d/
│           └── MoltenVK_icd.json  MoltenVK ICD manifest
└── share/
    └── wine/               Wine data files (fonts, inf, nls)
```

Also present outside the wine tree:

```
~/.metalsharp/runtime/
├── wine/                   (see above)
├── dxvk-1.10.3/            DXVK archive (x32/ and x64/ dirs, used by installer)
├── mono-x86/               x86 Mono runtime for legacy FNA games
├── mono-arm64/             ARM64 Mono runtime (or Homebrew mono)
└── shims/                  Pre-built shims (SDL3, FNA3D, FMOD stubs, CSteamworks)
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

### DXVK d3d9.dll Injection for 32-bit Games

For DxvkMetal32 games (Portal 2, Goat Simulator), the Rust backend copies DXVK's i386 d3d9.dll into the game's binary directory:

```rust
let dxvk_i386 = ms_root.join("lib").join("dxvk").join("i386-windows");
std::fs::copy(dxvk_i386.join("d3d9.dll"), game.join("bin").join("d3d9.dll"));
```

With `WINEDLLOVERRIDES="d3d9=n,b"` and `VK_ICD_FILENAMES` pointing to the bundled MoltenVK ICD, D3D9 calls go through DXVK → MoltenVK → Metal.

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

### DYLD_FALLBACK_LIBRARY_PATH

Critical for DXMT — `winemetal.so` lives outside Wine's standard search paths. Without it:

```
DYLD_FALLBACK_LIBRARY_PATH="$MS_LIB/wine/x86_64-unix:$MS_LIB/dxmt/x86_64-unix:..."
```

The PE→unix bridge can't locate `winemetal.so` and D3D11 device creation fails.

For SteamD3DMetalPerf games, DYLD is also augmented with GPTK paths so the Steam child process loads Apple's d3d11.dll.

### Vulkan / MoltenVK

For DxvkMetal32 games, the MoltenVK ICD is bundled in the wine runtime:

```
VK_ICD_FILENAMES=~/.metalsharp/runtime/wine/etc/vulkan/icd.d/MoltenVK_icd.json
```

This replaces the previous Homebrew-only path (`/opt/homebrew/share/vulkan/icd.d/MoltenVK_icd.json`).

## Wine Prefix

Single shared prefix at `~/.metalsharp/prefix-steam/`:
- Steam installed inside it (`drive_c/Program Files (x86)/Steam/`)
- External SSD Steam library mapped as F: drive
- `steamwebhelper.exe` wrapper deployed for CEF rendering (re-deploy after Steam auto-updates)

Some games get their own prefix (appid-specific) via `prepare_metalsharp_game()` in `setup.rs` — e.g., `~/.metalsharp/prefix-{appid}/`.

## Building Wine 11.5

From upstream source with 7 custom patches (A-G). Key build details:
- MinGW GCC for PE cross-compilation
- `-fno-function-sections` (MinGW 15.2 linker drops symbols with `-ffunction-sections`)
- `__attribute__((used))` on specific exports
- Bison 3.8+ required (Homebrew, not macOS system bison 2.3)
- freetype 2.13.3, gnutls, MoltenVK linked

## GPTK (Game Porting Toolkit)

GPTK is used for `SteamD3DMetalPerf` games only. It's installed at `/Applications/Game Porting Toolkit.app/` and provides:
- `x86_64-windows/d3d11.dll` — Apple's D3DMetal implementation
- `x86_64-unix/` unix support libs

The Rust backend prepends GPTK's paths to `WINEDLLPATH` and `DYLD_FALLBACK_LIBRARY_PATH` so Steam child processes load Apple's D3DMetal. GPTK is **not** needed for DxmtMetal (64-bit D3D11) or DxvkMetal32 (32-bit D3D9) games.
