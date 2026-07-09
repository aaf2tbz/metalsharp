# MetalSharp Redistributable Runtime
**Updated:** 2026-07-08


Status: Phase 4 foundation

Redistributables are modeled as bottle components. MetalSharp looks for legal local assets in Steam Common Redistributables, Sharp Library installer bottles, and `~/.metalsharp/runtime/redist/`, then records missing, installed, or repair-needed state in the bottle.

## Component IDs

The current repairable component surface includes:

- `vcrun2019`
- `directx_jun2010`
- `dotnet48`
- `webview2`
- `openal`
- `xna`
- `physx`
- `wine-mono`
- `gecko`
- `corefonts`

Graphics route components such as `d3d9`, `d3d10`, `d3d11`, `d3d12`, and `dxgi` are verified from the MetalSharp runtime instead of downloaded as redistributables.

## Asset Discovery

Steam game bottles scan game installs for `_CommonRedist`, `CommonRedist`, and `installscript.vdf` assets. Detected assets infer required bottle components:

- VC redist payloads -> `vcrun2019`
- DirectX payloads or install scripts -> `directx_jun2010`
- .NET payloads -> `dotnet48`
- WebView payloads -> `webview2`
- OpenAL payloads -> `openal`
- XNA payloads or install scripts -> `xna`
- PhysX payloads or install scripts -> `physx`

The install script parser is intentionally conservative: it only infers known redistributable families from obvious names such as `DXSETUP.exe`, `xnafx40_redist.msi`, `oalinst.exe`, or PhysX installers.

## Repair Sources

The repair resolver searches:

```text
~/.metalsharp/prefix-steam/drive_c/Program Files (x86)/Steam/steamapps/common/Steamworks Shared/_CommonRedist/
~/.metalsharp/bottles/*/installers/
~/.metalsharp/runtime/redist/
```

MSI redistributables are launched through `msiexec /i`; EXE redistributables are launched directly with silent arguments where known. XNA Framework 4.0 repairs also reuse a matching Sharp Library installer-bottle payload when the user has already installed or staged `xnafx40_redist.msi` through Sharp Library. Every repair writes a per-bottle component log.

## Remaining Work

- Persist redist install receipts separately from heuristic file checks.
- Parse more Steam install script actions and conditions.
- Add legal asset download/install UX around the existing redist source guide.
- Add end-to-end verification against Steamworks Common Redistributables on a real Wine Steam install.
