# MetalSharp .NET Translation Layer Plan

## Problem
Wine-mono crashes on most .NET games (Terraria, etc.). System mono runs .NET perfectly on macOS (proven with Celeste). Every .NET game needs this fixed.

## Architecture
Custom `mscoree.dll` PE shim that:
1. Takes the .NET IL from the PE binary
2. Runs it via **system mono** (native ARM64, fast, stable)
3. Routes Win32 P/Invoke calls through **Wine's existing Win32 layer**

```
Current (broken):
  Game.exe -> Wine -> wine-mono (crashes) -> native DLLs

Proposed:
  Game.exe -> Wine -> mscoree_shim -> system mono -> Win32 P/Invoke -> Wine
```

## Implementation Steps

### Step 1: Build mscoree.dll PE shim
- Custom PE DLL built with MinGW (like existing D3D11 shim pattern in `src/wine/`)
- Intercepts `CorBindToRuntimeEx` / `CLRCreateInstance` — the entry points Wine calls when loading a .NET assembly
- Instead of loading wine-mono, launches system mono with the game's exe

### Step 2: IL extraction
- Parse PE headers to find the .NET metadata and IL sections
- Mono can load PE files directly, so we likely just pass the exe path
- May need to handle mixed-mode (C++/CLI) assemblies differently

### Step 3: Launch system mono from within Wine
- From the shim (running inside Wine's x86_64 process), fork/exec `/opt/homebrew/bin/mono`
- Pass the game exe path and working directory
- Set environment variables: `DYLD_LIBRARY_PATH`, `MONO_PATH`, etc.
- Mono runs the managed code natively on ARM64

### Step 4: P/Invoke routing
- When mono encounters P/Invoke to Windows native DLLs (kernel32, ReLogic.Native.dll), it uses `dlopen`/`dlsym`
- Under Wine, `dlopen`/`dlsym` can resolve through Wine's PE loader
- May need to set `MONO_PATH` or configure assembly resolution so mono finds the game's DLLs
- May need a custom `DllImport` resolver for edge cases
- This is the hardest part — bridging mono's native calls back into Wine's Win32 translation

### Step 5: Replace wine-mono in prefix
- Disable wine-mono via registry: `HKLM\Software\Wine\Mono\Enabled = no`
- Install our custom `mscoree.dll` into the Wine prefix (`drive_c/windows/system32/`)
- Ensure prefix init still works without wine-mono
- Non-.NET games should be unaffected (they never call mscoree)

### Step 6: Testing
- **Terraria**: .NET 4.x game with native Win32 DLLs (ReLogic.Native.dll, nfd.dll) — primary test case
- Verify pure FNA games (Celeste) still work via their mono path
- Verify non-.NET games still work through Wine normally
- Test other .NET games as they come

## File locations
- Shim source: `src/wine/mscoree_pe.cpp` (following `d3d11_pe.cpp` pattern)
- Unix bridge: possibly `src/wine/mscoree_unix.mm` if macOS-specific IPC needed
- Build: add to `src/wine/Makefile`

## Key references
- mscoree.dll hosting API: https://learn.microsoft.com/en-us/dotnet/framework/unmanaged-api/hosting/
- Wine's mscoree implementation: `wine/dlls/mscoree/`
- Mono embedding API: https://www.mono-project.com/docs/advanced/embedding/
- Existing shim pattern: `src/wine/d3d11_pe.cpp` + `metalsharp_unix.mm`
