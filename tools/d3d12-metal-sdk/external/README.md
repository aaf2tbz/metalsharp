# External Tool Checkouts

This directory is intentionally ignored except for this README and `.gitignore`.
Use it for shallow clones of large DirectX and graphics-debugging tools needed
for PR119 validation.

Bootstrap or refresh the local tools with:

```bash
tools/d3d12-metal-sdk/scripts/setup-external-tools.sh
```

The script currently tracks:

- AMD Smoldr for tiny script-driven D3D12/DXIL tests.
- LunarG GFXReconstruct for D3D12 capture/replay workflows.
- RenderDoc for reference D3D12 frame inspection.
- Microsoft DirectX-Headers for canonical D3D12 ABI/header reference.
- Microsoft DirectXTK12 for hard D3D12 asset/rendering probes.

Do not commit the cloned repositories into the MetalSharp PR. Keep the bootstrap
script and any small integration harnesses in this SDK instead.
