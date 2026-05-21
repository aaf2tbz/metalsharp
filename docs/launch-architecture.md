# Launch Architecture

MetalSharp launches games through the Rust backend and the current MTSP pipeline resolver.

## Flow

```text
Play clicked
  -> renderer calls backend
  -> backend resolves a pipeline
  -> backend syncs/preflights the runtime bottle when one applies
  -> backend builds a LaunchRecipe
  -> backend preflights runtime assets
  -> backend prepares DLLs/env/cache beside the selected executable
  -> direct Wine/MTSP, Steam, MacOS Steam, or Native macOS starts the game
```

The launch recipe is the backend contract for click-to-play. It records the appid, selected pipeline, game directory,
selected executable, launch arguments, environment, DLL placement, runtime asset status, anti-cheat markers, and warnings.
Manual launch methods still work; they force the pipeline before the recipe is built.

Runtime bottles add the user-facing readiness contract. Sharp Library installer/app bottles own their own prefixes under
`~/.metalsharp/bottles/<id>/prefix`. Steam game bottles use ids like `steam_620` and are launch-authoritative preflight
records over the shared Wine Steam prefix, so Steam remains the running launcher/session owner while MetalSharp checks
runtime assets, redistributables, component state, and launch health.

Steam game bottle sync scans `_CommonRedist`, `CommonRedist`, and `installscript.vdf` payloads, then infers repairable
components such as VC runtime, DirectX June 2010, .NET 4.8, WebView2, OpenAL, XNA, and PhysX. Repair actions resolve
legal local assets from Steamworks Shared or `~/.metalsharp/runtime/redist/` and write per-bottle component logs.

For env-dependent Steam routes, MetalSharp keeps Wine Steam running as the background client, then launches the game
executable directly through the selected MTSP pipeline with the bottle prefix, route env, cache paths, and
`SteamAppId`/`SteamGameId`. Plain **Steam** mode still uses Wine Steam itself.

## Current Pipelines

| Pipeline | Backend | Launch path |
|---|---|---|
| **M11** | DXMT | Direct Wine launch with D3D11/DXGI DXMT DLLs |
| **M12** | DXMT | Direct Wine launch with D3D12/D3D11/DXGI DXMT DLLs |
| **M10** | DXMT | Direct Wine launch with D3D10/D3D11/DXGI DXMT DLLs |
| **M9** | DXMT launch family | Direct Wine launch with bundled `d3d9.dll` and DXMT-family cache/env |
| **M32** | Wine32 | 32-bit Wine fallback |
| **Native macOS** | Mono/FNA | Native FNA/XNA/Mono runtime |
| **Steam** | Wine Steam | Launch through Windows Steam after Steam bottle preflight |
| **MacOS Steam** | Native Steam | Launch through native macOS Steam |
| **Wine** | Wine | Plain Wine launch for custom library apps |

## Resolution

The resolver checks, in order:

1. `configs/mtsp-rules.toml`
2. Managed .NET/FNA eligibility
3. PE header analysis
4. Installed game directory markers
5. M12 fallback

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

Runtime prep is recipe-driven. DXMT/Wine DLL overrides are deployed next to the selected executable rather than blindly
into the game root, which keeps nested layouts such as `Binaries/Win64` and launcher-heavy games from loading the wrong
binary or missing local overrides.

M11/M10 copy:

- `d3d11.dll`
- `dxgi.dll`
- `d3d10core.dll`
- `winemetal.dll` in the active Wine prefix `C:\windows\system32`

M10 is selected by 64-bit `d3d10.dll`, `d3d10_1.dll`, or `d3d10core.dll` imports. It deploys Wine's public `d3d10.dll` and `d3d10_1.dll` entrypoints plus DXMT's `d3d10core.dll`, so public D3D10 imports and the DXMT core handoff are both owned by the x86_64 M10 runtime contract.

M12 also copies:

- `d3d12.dll`

M9 copies:

- `d3d9.dll`

M9 no longer accepts the legacy `dxvk_metal32`, `m9_gl`, or `m32_vk` aliases. D3D9 imports resolve to `[m9]`, and `[m9]` stays on the DXMT-family launch path instead of selecting DXVK/MoltenVK.

Native macOS does not use Wine. Steam and MacOS Steam are separate paths.

## Bottles

| Bottle type | Prefix behavior | Used for |
|---|---|---|
| Installer / Sharp Library | Dedicated bottle prefix | Windows installers, launchers, demos, imported apps |
| Steam game | Shared `~/.metalsharp/prefix-steam` | Steam game preflight, runtime assets, component repair, launch health |

Steam game bottles do not replace Steam. They prepare the runtime state the game will use and keep Wine Steam alive as
the background Steamworks client/session owner. Env-dependent pipeline launches run the game executable directly with
Steam identity env; client-only **Steam** launches still route through Wine Steam.

## Process Lifecycle

- Running games are tracked by the backend.
- Stop/kill actions terminate the registered process and child processes.
- Steam process management lives in `steam.rs`.
- Launching a Steam game keeps Wine Steam alive for Steam connectivity. Env-dependent routes apply route-specific
  environment to the spawned game process rather than trying to make an already-running Steam client inherit it.
- Wine Steam readiness checks fail clearly if Steam never becomes detectable, keeping launch requests below the renderer
  backend timeout instead of silently proceeding without a Steam client.
- Shader cache paths are per appid under `~/.metalsharp/shader-cache/`.
- Wine-backed launch logs include the host ABI version, host runtime path, Wine runtime path, Steam bridge port, and
  compatdata manifest path when the launch is tied to a Steam appid.
- Launch recipes classify detected anti-cheat markers into statuses such as `blocked_pending_vendor_support`,
  `unsupported_kernel_driver`, `vendor_supported_on_proton_assets_present`, `unknown`, and `user_mode_possible`.
