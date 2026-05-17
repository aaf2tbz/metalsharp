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

M10 is the D3D10 DXMT path. It deploys Wine's public `d3d10.dll` and `d3d10_1.dll` entrypoints for imported D3D10 APIs, then routes the core handoff through DXMT's `d3d10core.dll` and the shared D3D11/DXGI/winemetal stack.

DXMT DLLs:

| DLL | Used by |
|---|---|
| `d3d12.dll` | M12 |
| `d3d11.dll` | M12, M11, M10 |
| `dxgi.dll` | M12, M11, M10 |
| `d3d10core.dll` | M12, M11, M10 |
| `d3d10.dll`, `d3d10_1.dll` | M10 public Wine D3D10 entrypoints |
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
~/.metalsharp/shader-cache/m10/<appid>/
~/.metalsharp/shader-cache/m11/<appid>/
~/.metalsharp/shader-cache/m12/<appid>/
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
- M10 and M11 share the `dxmt-metal` preset fallback family.
- M9 has an extra Vulkan/MoltenVK hop.
- M12 is still marked experimental in source.
- M32 is the fallback for 32-bit Wine cases.
