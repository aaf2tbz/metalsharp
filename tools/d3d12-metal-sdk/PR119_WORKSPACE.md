# PR119 D3D12 Metal Workspace

Canonical PR119 workspace:

```text
/Volumes/AverySSD/metalsharp-pr119-sdk
```

Use this checkout for all PR119 D3D12/DXMT/Metal work going forward. The old
internal-drive worktree was copied here so the active shader translator fixes,
probe sources, captured stress probes, and generated results stay together.

Runtime state still lives in the normal MetalSharp locations:

```text
~/.metalsharp/runtime/wine/
~/.metalsharp/compatdata/1962700/
~/.metalsharp/shader-cache/
/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2/
```

## Evidence Chain

- Mini D3D12 probes prove the route can create devices, queues, swapchains,
  root signatures, descriptor tables, RTV clears, compute dispatches, graphics
  PSOs, geometry PSOs, and texture sampling without launching a game.
- Fullscreen diagnostic rendering proved nonzero pixels can reach the
  swapchain attachment and present path.
- Normal UE5/Subnautica rendering still produces black output, which points at
  D3D12 color-output state, render target binding, shader output semantics,
  movie/loading-window handoff, or UE render-graph resource transitions.
- Recent compiler fixes removed known DXIL-to-MSL blockers around select
  scalarization, embedded vector load scalarization, vertex buffer access
  mutability, and false MetalShaderConverter stage-in requirements.

## Local Tooling Lanes

- `scripts/setup-external-tools.sh` keeps DirectX/graphics debugging tools
  cloned under `tools/d3d12-metal-sdk/external/`.
- `scripts/index-subnautica-failures.py` summarizes the latest Subnautica logs
  and shader failure sidecars without launching the game.
- `scripts/preflight-before-game.sh` is the no-game gate before a live launch.
- `scripts/offline-pso-factory.py` replays captured Metal PSO creation offline.
- `scripts/replay-shader-corpus.py` replays dumped `.dxbc` through
  MetalShaderConverter.

## Immediate Renderer Target

Attack the color-output path with proof:

1. Compare normal UE render PSO manifests against the diagnostic fullscreen PSO.
2. Verify D3D12 RTV formats, write masks, blend state, sample count, and render
   target array sizes map into the Metal render pipeline descriptor.
3. Verify fragment functions expose color outputs that match the active Metal
   color attachments.
4. Verify D3D12 resource states and barriers put the active color target into a
   renderable state before draw and into present/copy state before readback.
5. Verify the final UE render target is the swapchain backbuffer or is copied /
   resolved into it before `Present`.
