# DXMT and Vulkan Architecture

MetalSharp has two graphics translation families:

- **DXMT launch family**: M9/M10/M11/M12 to Metal
- **DXVK + MoltenVK**: Legacy fallback assets only; no longer selected by M9

## Pipeline Map

| Pipeline | Translation |
|---|---|
| **M12** | D3D12 -> DXMT -> Metal |
| **M11** | D3D11 -> DXMT -> Metal |
| **M10** | D3D10 -> DXMT -> Metal |
| **M9** | D3D9 -> MetalSharp D3D9 -> DXMT launch family -> Metal |
| **M32** | 32-bit Wine fallback |

## DXMT

The DXMT launch family is used by M12, M11, M10, and M9.

DXMT-family DLLs:

| DLL | Used by |
|---|---|
| `d3d12.dll` | M12 |
| `d3d11.dll` | M12, M11, M10 |
| `dxgi.dll` | M12, M11, M10 |
| `d3d10core.dll` | M12, M11, M10 |
| `winemetal.dll` | M12, M11, M10 |
| `d3d9.dll` | M9 |
| `winemetal.so` | Unix Metal bridge |

Basic flow:

```text
Game
  -> DXMT PE DLL
  -> winemetal.so
  -> Metal command buffers
  -> Apple GPU
```

DXMT uses per-game shader caches under:

```text
~/.metalsharp/shader-cache/dxmt-metal/<appid>/
~/.metalsharp/shader-cache/dxmt-metal12/<appid>/
```

## M9 D3D9

M9 does not select DXVK/MoltenVK. The pipeline deploys the D3D9 DLL from the bundled Wine runtime and uses the same DXMT launch/cache environment family as the other Metal pipelines.

Basic flow:

```text
Game
  -> d3d9.dll
  -> Wine / MetalSharp D3D9 handoff
  -> Metal
```

M9 cache path:

```text
~/.metalsharp/shader-cache/m9/<appid>/
```

## Current Game Notes

| Game | Best/current pipeline |
|---|---|
| Schedule 1 | M11 recommended, M12 works |
| Subnautica | M11 |
| Subnautica: Below Zero | M12 recommended, M11 optimized |
| Rain World | M11, M9 also works |
| Undertale | M32, fallback M9 |
| Portal 2 | M9, audio still needs work |
| Nidhogg 2 | M32 |
| Ghostrunner | M12 |
| High on Life | Steam with `-dx11` |
| Borderlands 3 | Steam |
| Stardew Valley | MacOS Steam |

## Notes

- DXMT is the direct Metal path.
- M9 no longer has a Vulkan/MoltenVK hop in MTSP selection.
- M12 is still marked experimental in source.
- M32 is the fallback for 32-bit Wine cases.
