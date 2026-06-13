# Wine Architecture

MetalSharp ships a self-contained Wine runtime at:

```text
~/.metalsharp/runtime/wine/
```

It is used by the public Wine-backed routes M12, M11, M10, and M9. Internal fallback/diagnostic routes such as M32, Steam handoff, and plain Wine also use this runtime. Mono/FNA does not use the Wine runtime.

## Layout

```text
~/.metalsharp/runtime/wine/
|-- bin/
|   |-- metalsharp-wine
|   |-- wine
|   |-- wineloader
|   `-- wineserver
|-- lib/
|   |-- wine/
|   |   |-- x86_64-unix/
|   |   |-- x86_64-windows/
|   |   `-- i386-windows/
|   |-- dxmt/
|   |   |-- x86_64-unix/
|   |   `-- x86_64-windows/
`-- etc/
    |-- dxmt.conf
    `-- vulkan/icd.d/MoltenVK_icd.json
```

Other runtime pieces live beside it:

```text
~/.metalsharp/runtime/
|-- redist/
`-- wine/
```

User/runtime state lives beside the runtime root:

```text
~/.metalsharp/
|-- prefix-steam/
|-- bottles/
|-- sharp-library/
|-- games/
|-- shader-cache/
|-- cache/
`-- logs/
```

## Route Use

| Route | Wine use |
|---|---|
| M12 | Wine + DXMT D3D12/D3D11/DXGI |
| M11 | Wine + DXMT D3D11/DXGI |
| M10 | Wine + DXMT D3D10/D3D11/DXGI |
| M9 | Wine + D3D9 Metal under the DXMT launch family |

M32, Steam handoff, and plain Wine remain internal Wine-backed routes for diagnostics, bootstrap cases, legacy records, and installer/custom-app internals.

## DLL Deployment

The backend copies graphics DLLs into the game directory before launch.

M11/M10:

```text
d3d11.dll
dxgi.dll
d3d10core.dll
winemetal.dll
```

M10 deploys Wine's public `d3d10.dll` and `d3d10_1.dll` entrypoints for D3D10 imports, then uses DXMT's `d3d10core.dll` as the D3D10 handoff and shares the D3D11/DXGI/winemetal runtime with M11.

M12:

```text
d3d12.dll
dxgi_dxmt.dll
d3d11.dll
dxgi.dll
d3d10core.dll
winemetal.dll
nvapi64.dll
nvngx.dll
```

M12 deliberately deploys the DXMT D3D11 and D3D10 core fallback surface because
some D3D12 titles and launchers still touch D3D11/DXGI/D3D10 components during
startup.

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
`SteamAppId`/`SteamGameId`; client-only Steam handoff remains internal for diagnostics/bootstrap cases.

## Prefix Runtime Surface

Install and migration keep a current runtime surface inside
`~/.metalsharp/prefix-steam`. This is separate from game-local DLL injection:
it is the prefix copy used during Wine init, Steam startup, and prefix-routed
M12 launches.

Windows files are staged to:

```text
~/.metalsharp/prefix-steam/drive_c/windows/system32/
```

Unix sidecars are staged to:

```text
~/.metalsharp/prefix-steam/.metalsharp/unix/
```

The file list is owned by `app/src-rust/src/prefix_runtime.rs`. Install,
migration, and tests all use that shared list. The important M12 files are:

```text
d3d12.dll
dxgi.dll
dxgi_dxmt.dll
d3d11.dll
d3d10core.dll
winemetal.dll
winemetal.so
metalsharp_ntdll_hook.dll
```

Validation is content-based. A prefix copy that exists but does not match the
installed runtime is treated as stale and must be restaged.

Prefix init uses a bounded probe:

```text
metalsharp-wine cmd /c exit
```

Migration repairs reported prefix runtime failures without reinstalling Steam.
The repair pass fixes prefix directories, backs up an invalid `dosdevices` path,
recreates `c:`, clears only staged runtime files, restages the current runtime,
reruns the bounded probe, and validates again.

## Important Environment

| Variable | Purpose |
|---|---|
| `WINEPREFIX` | Prefix location |
| `WINEDLLPATH` | Wine PE DLL lookup |
| `DYLD_FALLBACK_LIBRARY_PATH` | Unix library lookup for Wine and DXMT |
| `WINEDLLOVERRIDES` | Selects injected/native DLL behavior |
| `DXMT_SHADER_CACHE_PATH` | DXMT shader cache |
| `DXMT_CONFIG_FILE` | DXMT config file |
| `DXMT_WINEMETAL_UNIXLIB` | WineMetal Unix sidecar loaded by `winemetal.dll`; M12 game-local launches force `winemetal.so` by basename |
| `SteamAppId` / `SteamGameId` | Steam identity for direct Steam-bottle game launches |
| `DXMT_LOG_PATH` / `DXMT_LOG_FILE` | M12 diagnostics root and shared `m12.log` file |

## Steam Wrapper

Wine Steam uses the bundled `steamwebhelper.exe` wrapper. Steam updates may replace it, so MetalSharp redeploys it when preparing or launching Steam.
