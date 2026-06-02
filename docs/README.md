# MetalSharp Docs

Use this page as the repo map before changing launch/runtime code.

## Operational Docs

- [Runtime Bundles and Steam Routing](runtime-bundles-and-steam-routing.md) - bundle provenance, wrapper deployment, and the correct Wine Steam route.
- [Launch Architecture](launch-architecture.md) - pipeline selection and launch ownership.
- [Game Compatibility](game-compat.md) - short entrypoint to the current compatibility ledger.
- [Supported Games](GAMES-SUPPORTED.md) - current working/blocked game evidence and recommended public routes.
- [Mono Runtime Lanes](mono-runtime-lanes.md) - Mono/FNA/XNA route boundaries and native Mono lane details.
- [Wine Architecture](wine-architecture.md) - Wine prefix/runtime layout and wrapper behavior.
- [Launcher Runtime](launcher-runtime.md) - Sharp Library launcher and CEF/WebView runtime handling.
- [D3D12 Pipeline Map](m12-pipeline-map.md) - current M12 D3D12/DXMT path.
- [DXMT and Vulkan Architecture](dxmt-vulkan-architecture.md) - DXMT/M9-M12 boundaries and Vulkan fallback boundaries.
- [D3D12 Developer Runtime Package](../tools/d3d12-metal-sdk/docs/developer-runtime.md) - self-contained developer SDK tarball layout and publish flow.

## Historical Roadmaps

These files are planning history and should not be treated as the current implementation contract without checking the code and runtime manifests first.

- [MetalSharp Final Roadmap](MetalSharp%20Final%20Roadmap.md)
- [DX12 Pipeline Complete Roadmap](DX12%20Pipeline%20Complete%20Roadmap.md)
- [Beta7 DXMT Cohesion Roadmap](beta7-dxmt-cohesion-roadmap.md)
- [DXMT Proton-Parity Roadmap](dxmt-proton-parity-roadmap.md)
- [Installer Runtime Roadmap](installer-runtime-roadmap.md)
- [Anti-Cheat Hard Route Roadmap](anticheat-hard-route-roadmap.md)

## Bundle Truth Sources

- Release assets live on the [`bundles` GitHub release](https://github.com/aaf2tbz/metalsharp/releases/tag/bundles).
- Manifest-tracked hashes live in [tools/bundles/asset-manifest.tsv](../tools/bundles/asset-manifest.tsv).
- Verify local and remote bundle state with:

```bash
tools/bundles/verify-bundles.sh --release
tools/bundles/verify-bundles.sh --require mac
```
