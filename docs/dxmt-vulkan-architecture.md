# DXMT and Vulkan Architecture

MetalSharp has two graphics translation families:

- **DXMT**: D3D10/D3D11/D3D12 to Metal
- **DXVK + MoltenVK**: D3D9 to Vulkan to Metal

## Pipeline Map

| Pipeline | Translation |
|---|---|
| **M12** | D3D12 -> DXMT -> Metal |
| **M11** | D3D11 -> DXMT -> Metal |
| **M10** | D3D10 -> DXMT -> Metal |
| **M9** | D3D9 -> DXVK -> MoltenVK -> Metal |
| **M32** | 32-bit Wine fallback |

## DXMT

DXMT is used by M12, M11, and M10.

DXMT DLLs:

| DLL | Used by |
|---|---|
| `d3d12.dll` | M12 |
| `d3d11.dll` | M12, M11, M10 |
| `dxgi.dll` | M12, M11, M10 |
| `d3d10core.dll` | M12, M11, M10 |
| `winemetal.dll` | M12, M11, M10 |
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

## DXVK + MoltenVK

M9 uses DXVK 1.10.3 for D3D9.

Basic flow:

```text
Game
  -> d3d9.dll from DXVK
  -> Vulkan
  -> MoltenVK
  -> Metal
```

M9 cache path:

```text
~/.metalsharp/shader-cache/dxvk-metal9/<appid>/
```

MoltenVK ICD:

```text
~/.metalsharp/runtime/wine/etc/vulkan/icd.d/MoltenVK_icd.json
```

## Current Game Notes

| Game | Best/current pipeline |
|---|---|
| Schedule 1 | M12 recommended |
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
- M9 has an extra Vulkan/MoltenVK hop.
- M12 is the primary stable DXMT D3D engine method in source.
- M32 is the fallback for 32-bit Wine cases.
