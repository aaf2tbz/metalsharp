# M10 Pipeline Map

M10 is the stable D3D10 engine path. It launches Windows D3D10 titles through Wine and DXMT, then hands rendering to Metal through DXMT's winemetal bridge.

## Runtime Shape

```text
D3D10 game
  -> Wine
  -> Wine d3d10.dll / d3d10_1.dll public entrypoints
  -> DXMT d3d10core.dll
  -> DXMT d3d11.dll + dxgi.dll
  -> winemetal.dll / winemetal.so
  -> Metal command buffers
  -> Apple GPU
```

M10 deploys Wine's public D3D10 entrypoint DLLs for games that import `d3d10.dll` or `d3d10_1.dll`, then routes the core handoff through DXMT's `d3d10core.dll` plus the same DXMT D3D11, DXGI, and winemetal runtime used by M11.

M10 deploys these public D3D10 entrypoints from `~/.metalsharp/runtime/wine/lib/wine/x86_64-windows/`:

- `d3d10.dll`
- `d3d10_1.dll`

M10 deploys these DXMT handoff DLLs from `~/.metalsharp/runtime/wine/lib/dxmt/x86_64-windows/`:

- `d3d11.dll`
- `dxgi.dll`
- `d3d10core.dll`
- `winemetal.dll`

M10 deliberately does not deploy `d3d12.dll`.

## Engine Contract

| Field | Value |
|---|---|
| Pipeline | `M10` |
| Backend | `dxmt` |
| Launch args | `-dx10` |
| Wine overrides | `d3d10,d3d10_1,dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d` |
| Shader cache subdir | `m10` |
| Preset fallback family | `m10`, then `dxmt-metal` |

M10 uses the same DXMT Unix library search path as M11:

```text
lib/wine/x86_64-unix
lib/dxmt/x86_64-unix
```

## Selection Rules

The backend resolves M10 from PE imports before broad directory heuristics. That keeps D3D10 games from being demoted to M11 just because their folder also includes common engine or Steam markers.

Recognized D3D10 imports:

- `d3d10.dll`
- `d3d10_1.dll`
- `d3d10core.dll`

If a game imports both D3D12 and D3D10 compatibility DLLs, D3D12 still wins and maps to M12 for 64-bit executables.
