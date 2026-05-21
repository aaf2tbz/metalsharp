# Wine Architecture

MetalSharp ships a self-contained Wine runtime at:

```text
~/.metalsharp/runtime/wine/
```

The current primary macOS runtime is a from-source Wine 11.9 build with x86_64 host binaries, i386 and x86_64 Windows
lanes, MoltenVK, DXMT/DXVK payloads, gnutls/freetype host dependencies, and the MetalSharp `mscompatdb` hook contract
exported from Unix `ntdll.so`. It is used by M11, M12, M10, M9, M32, Steam, installer bottles, protected-launch probes,
and plain Wine. Native macOS and macOS Steam do not use this Wine runtime.

## Layout

```text
~/.metalsharp/runtime/wine/
├── bin/
│   ├── metalsharp-wine
│   ├── wine
│   ├── wineloader
│   └── wineserver
├── lib/
│   ├── wine/
│   │   ├── x86_64-unix/
│   │   ├── x86_64-windows/
│   │   └── i386-windows/
│   ├── dxmt/
│   │   ├── x86_64-unix/
│   │   └── x86_64-windows/
└── etc/
    ├── dxmt.conf
    └── vulkan/icd.d/MoltenVK_icd.json
```

Other runtime pieces live beside it:

```text
~/.metalsharp/runtime/
├── redists/
├── redist/
├── mono-arm64/
├── mono-x86/
├── shims/
├── host/
└── wine/
```

## Current Primary Runtime

The Wine 11.9 runtime was promoted as the primary local direction for the WTMKT/protected-launch work. The live runtime
must satisfy these basic checks before installer or anti-cheat behavior is interpreted:

```bash
DYLD_LIBRARY_PATH="$HOME/.metalsharp/runtime/wine/lib/wine/x86_64-unix" \
  "$HOME/.metalsharp/runtime/wine/bin/wine" --version

nm -gU "$HOME/.metalsharp/runtime/wine/lib/wine/x86_64-unix/ntdll.so" \
  | rg 'MetalSharpGetMscompatdbHookContract|NtCreateUserProcess|NtCreateFile'

curl -sS -X POST http://127.0.0.1:9274/steam/mscompatdb-probe \
  -H 'Content-Type: application/json' \
  -d '{}' | jq '{status, hooked:.probe.hooked, hookContractReady:.probe.ntdllSymbols.hookContractReady}'
```

Expected baseline:

- `wine-11.9` from `wine --version`.
- `wine` and `wineserver` are Mach-O x86_64 host executables.
- `ntdll.dll` exists in both `x86_64-windows/` and `i386-windows/`.
- Unix `ntdll.so` exports `MetalSharpGetMscompatdbHookContract` and `MetalSharpGetMscompatdbHookContractVersion`.
- `mscompatdb.so` autoloads from Unix `ntdll.so`, uses the explicit hook contract, and reports `hook_surface_ready`.
- `mscompatdb.so` and `mscompatdb.dylib` are both Mach-O x86_64 and ad-hoc signed.
- Host dependency install names use bundled or `@rpath` paths, not temporary build paths.

## mscompatdb Contract Shim

The current Darwin shim is source-controlled at:

```text
include/metalsharp/MscompatdbHookContract.h
src/wine/ntdll_mscompatdb_contract.c
src/wine/ntdll_mscompatdb_loader.c
src/wine/mscompatdb/mscompatdb_contract_shim.c
tools/wine/build-mscompatdb-shim.sh
```

The Wine-side contract export is compiled into Unix `ntdll.so`. The loader template restores the runtime `mscompatdb.so`
autoload step and uses `RTLD_GLOBAL` so the shim can resolve `MetalSharpGetMscompatdbHookContract`. The shim no longer
scrapes the private/local `KeServiceDescriptorTable` symbol; it asks `ntdll.so` for the service table and key NT entrypoint
pointers through the explicit contract, then patches `NtCreateUserProcess` through the table.

Build the shim payload with:

```bash
tools/wine/build-mscompatdb-shim.sh /tmp/metalsharp-mscompatdb
```

For runtime packaging, copy `/tmp/metalsharp-mscompatdb/mscompatdb.so` and
`/tmp/metalsharp-mscompatdb/mscompatdb.dylib` into `wine/lib/wine/x86_64-unix/` after relinking the patched `ntdll.so`.

User/runtime state lives beside the runtime root:

```text
~/.metalsharp/
├── prefix-steam/
├── bottles/
├── sharp-library/
├── games/
├── shader-cache/
├── cache/
└── logs/
```

## Pipeline Use

| Pipeline | Wine use |
|---|---|
| M11 | Wine + DXMT D3D11/DXGI |
| M12 | Wine + DXMT D3D12/D3D11/DXGI |
| M10 | Wine + DXMT D3D10/D3D11/DXGI |
| M9 | Wine + D3D9 Metal under the DXMT launch family |
| M32 | Wine 32-bit fallback |
| Steam | Wine Steam prefix, preflighted by `steam_<appid>` bottle records |
| Wine | Plain Wine |

## DLL Deployment

The backend copies graphics DLLs into the game directory before launch.

M11/M10:

```text
d3d11.dll
dxgi.dll
d3d10core.dll
```

M10 deploys Wine's public `d3d10.dll` and `d3d10_1.dll` entrypoints for D3D10 imports, then uses DXMT's `d3d10core.dll` as the D3D10 handoff and shares the D3D11/DXGI/winemetal runtime with M11.

M12:

```text
d3d12.dll
d3d11.dll
dxgi.dll
d3d10core.dll
```

DXMT's `winemetal.dll` is not staged beside the game executable. MetalSharp
binds it into the active Wine prefix under `C:\windows\system32` and keeps the
paired `winemetal.so` under Wine's Unix library directory so Wine can attach the
PE stub to the Unix Metal bridge correctly. Stale game-local `winemetal` copies
are removed during preparation.

M9:

```text
d3d9.dll
```

## Prefixes

The shared Steam prefix is:

```text
~/.metalsharp/prefix-steam/
```

Steam is installed inside that prefix. External Steam libraries may be mounted into the prefix by drive letter.

Sharp Library installer/app bottles use dedicated prefixes:

```text
~/.metalsharp/bottles/<id>/prefix/
```

Steam game bottles are different: they are launch-authoritative readiness records, but their `prefix_path` currently
points at `~/.metalsharp/prefix-steam/` so Runtime Doctor and repair actions affect the prefix Wine Steam actually uses.
Wine Steam remains the live background client that stays connected for Steam games. Env-dependent Steam game launches
run the game executable directly through the selected MTSP pipeline with this prefix, route env, cache paths, and
`SteamAppId`/`SteamGameId`; plain **Steam** launches still route through Wine Steam.

## Important Environment

| Variable | Purpose |
|---|---|
| `WINEPREFIX` | Prefix location |
| `WINEDLLPATH` | Wine PE DLL lookup |
| `DYLD_FALLBACK_LIBRARY_PATH` | Unix library lookup for Wine and DXMT |
| `WINEDLLOVERRIDES` | Selects injected/native DLL behavior |
| `DXMT_SHADER_CACHE_PATH` | DXMT shader cache |
| `DXMT_CONFIG_FILE` | DXMT config file |
| `SteamAppId` / `SteamGameId` | Steam identity for direct Steam-bottle game launches |

## Steam Wrapper

Wine Steam uses the bundled `steamwebhelper.exe` wrapper. Steam updates may replace it, so MetalSharp redeploys it when preparing or launching Steam.
