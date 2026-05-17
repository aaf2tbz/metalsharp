# Launch Architecture

MetalSharp launches games through the Rust backend and the current MTSP pipeline resolver.

## Flow

```text
Play clicked
  -> renderer calls backend
  -> backend resolves a pipeline
  -> backend prepares DLLs/env/cache
  -> Wine, Steam, MacOS Steam, or Native macOS starts the game
```

## Current Pipelines

| Pipeline | Backend | Launch path |
|---|---|---|
| **M11** | DXMT | Direct Wine launch with D3D11/DXGI DXMT DLLs |
| **M12** | DXMT | Direct Wine launch with D3D12/D3D11/DXGI DXMT DLLs |
| **M10** | DXMT | Direct Wine launch with D3D10/D3D11/DXGI DXMT DLLs |
| **M9** | DXMT launch family | Direct Wine launch with bundled `d3d9.dll` and DXMT-family cache/env |
| **M32** | Wine32 | 32-bit Wine fallback |
| **Native macOS** | Mono/FNA | Native FNA/XNA/Mono runtime |
| **Steam** | Wine Steam | Launch through Windows Steam in the Wine prefix |
| **MacOS Steam** | Native Steam | Launch through native macOS Steam |
| **Wine** | Wine | Plain Wine launch for custom library apps |

## Resolution

The resolver checks, in order:

1. `configs/mtsp-rules.toml`
2. Managed .NET/FNA eligibility
3. PE header analysis
4. Installed game directory markers
5. M11 fallback

Common marker behavior:

| Marker | Pipeline |
|---|---|
| `.NET` managed game without native PE dependencies | Native macOS |
| Unity, Unreal, Source, RE Engine, or `steam_api*.dll` markers | M11 |
| `d3dx9_43.dll` | Wine |
| PE imports D3D12 | M12 for 64-bit games, M11 otherwise |
| PE imports D3D11 | M11 |
| 64-bit PE imports D3D10 | M10 |
| PE imports D3D9 | M9 |

D3D10 PE imports are checked before broad Unity, Unreal, Source, RE Engine, and Steam marker heuristics so D3D10 games stay on `[m10]`.

## Runtime Prep

M11/M10 copy:

- `d3d11.dll`
- `dxgi.dll`
- `d3d10core.dll`
- `winemetal.dll`

M10 is selected by 64-bit `d3d10.dll`, `d3d10_1.dll`, or `d3d10core.dll` imports. It deploys Wine's public `d3d10.dll` and `d3d10_1.dll` entrypoints plus DXMT's `d3d10core.dll`, so public D3D10 imports and the DXMT core handoff are both owned by the x86_64 M10 runtime contract.

M12 also copies:

- `d3d12.dll`

M9 copies:

- `d3d9.dll`

M9 no longer accepts the legacy `dxvk_metal32`, `m9_gl`, or `m32_vk` aliases. D3D9 imports resolve to `[m9]`, and `[m9]` stays on the DXMT-family launch path instead of selecting DXVK/MoltenVK.

Native macOS does not use Wine. Steam and MacOS Steam are separate paths.

## Process Lifecycle

- Running games are tracked by the backend.
- Stop/kill actions terminate the registered process and child processes.
- Steam process management lives in `steam.rs`.
- Shader cache paths are per appid under `~/.metalsharp/shader-cache/`.
