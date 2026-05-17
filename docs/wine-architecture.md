# Wine Architecture

MetalSharp ships a self-contained Wine runtime at:

```text
~/.metalsharp/runtime/wine/
```

Set `METALSHARP_HOME` to run the same runtime layout from another volume. For
example, `METALSHARP_HOME=/Volumes/AverySSD/metalsharp` makes the backend use:

```text
/Volumes/AverySSD/metalsharp/runtime/wine/
/Volumes/AverySSD/metalsharp/prefix-steam/
/Volumes/AverySSD/metalsharp/shader-cache/
/Volumes/AverySSD/metalsharp/pipeline-cache/
```

The launch-critical backend paths now resolve from `METALSHARP_HOME`: MTSP
recipe/runtime roots, M12 verification/deploy/title-readiness reports, Wine
Steam prefix helpers, Wine Steam library scanning, local MetalSharp game
scanning, setup state, shader/pipeline caches, Sharp Library state, and
Electron's MetalSharp directory helper. Installer and updater runtime/cache
paths also resolve from `METALSHARP_HOME`, so runtime bundle extraction, DXMT
deployment, Steam setup, downloaded bundles, and update DMG caches stay on the
selected volume. Migration remains a legacy repair path for `~/.metalsharp` and
should be audited separately before claiming complete external-drive parity for
old installs.

It is used by M11, M12, M10, M9, M32, Steam, and Wine. Native macOS and MacOS Steam do not use this Wine runtime.

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
├── goldberg/
├── mono-arm64/
├── shims/
└── wine/
```

## Pipeline Use

| Pipeline | Wine use |
|---|---|
| M11 | Wine + DXMT D3D11/DXGI |
| M12 | Wine + DXMT D3D12/D3D11/DXGI |
| M10 | Wine + DXMT D3D10/D3D11/DXGI |
| M9 | Wine + D3D9 Metal under the DXMT launch family |
| M32 | Wine 32-bit fallback |
| Steam | Wine Steam prefix |
| Wine | Plain Wine |

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
d3d11.dll
dxgi.dll
d3d10core.dll
winemetal.dll
```

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

Some prepared games can also use app-specific prefixes:

```text
~/.metalsharp/prefix-<appid>/
```

## Important Environment

| Variable | Purpose |
|---|---|
| `WINEPREFIX` | Prefix location |
| `WINEDLLPATH` | Wine PE DLL lookup |
| `DYLD_FALLBACK_LIBRARY_PATH` | Unix library lookup for Wine and DXMT |
| `WINEDLLOVERRIDES` | Selects injected/native DLL behavior |
| `DXMT_SHADER_CACHE_PATH` | DXMT shader cache |
| `DXMT_CONFIG_FILE` | DXMT config file |

## Steam Wrapper

Wine Steam uses the bundled `steamwebhelper.exe` wrapper. Steam updates may replace it, so MetalSharp redeploys it when preparing or launching Steam.
