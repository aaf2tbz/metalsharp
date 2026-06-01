# Developer Runtime

This directory is populated when `metalsharp-d3d12-developer-sdk.tar.zst` is
built. It is intentionally lightweight in git.

The packaged SDK contains:

- `runtime/wine/` - the Wine runtime used by MetalSharp developer probes.
- `runtime/dxmt/` - staged DXMT D3D10/D3D11/D3D12 DLLs and Winemetal bridge
  files copied from `metalsharp-graphics-dll.tar.zst`.
- `runtime/host/` - host runtime ABI metadata copied from
  `metalsharp-runtime.tar.zst`.
- `runtime/metalsharp-backend` - the backend binary from the runtime bundle.
- `runtime/manifest.json` - deterministic provenance for the runtime and
  graphics assets used to assemble the SDK.

Do not check the generated runtime payload into this folder. Rebuild the SDK
tarball instead.
