# Games Supported

Updated: 2026-06-02

These notes reflect the current MetalSharp pipeline names and current local playtesting evidence.

## Test System

Games were tested from an external 1TB M.2 SSD, roughly 5000 MB/s read/write over USB-C 3.1, on an M4 MacBook Air with 16GB RAM.

## Pipelines

| Pipeline | Use |
|---|---|
| **M11** | D3D11 to Metal via DXMT |
| **M12** | D3D12 to Metal via DXMT |
| **M10** | D3D10 to Metal via DXMT |
| **M9** | D3D9 via DXMT launch family |
| **M32** | 32-bit Wine fallback |
| **Native macOS** | FNA/XNA/Mono ARM64 native runtime |
| **Steam** | Wine Steam, preflighted by Steam game bottles |
| **MacOS Steam** | Native macOS Steam |
| **Wine** | Plain Wine custom-library fallback |

## Current Compatibility Summary

| Game | AppID / Bottle | Status | Recommended |
|---|---:|---|---|
| Repo | `steam_3241660` | Ran and played online through Steam. M11 was automatically selected. No issues from install through gameplay. | **M11** |
| Skul: The Hero Slayer | `steam_1147560` | Ran perfectly. M11 was automatically selected on install. | **M11** |
| The Witcher 3 | `steam_292030` | Ran through M11 with no issues. | **M11** |
| Stumble Guys | `steam_16677740` | Ran through M11 with online Steam session. No issues. | **M11** |
| The Wilds | `steam_1028590` | Auto-routed to M11 and ran without issues. | **M11** |
| Totally Accurate Battle Simulator | `steam_508440` | Auto-routed to M11 and ran without issues. | **M11** |
| Subnautica | `steam_264710` | Auto-selected M11 and ran without issues. | **M11** |
| The Long Dark | `steam_305620` | Auto-selected M11 and ran without issues on ultra settings. | **M11**, Ultra verified |
| Thronefall | `steam_2239150` | Auto-routed to M11 and ran without issues. | **M11** |
| Nidhogg 2 | `steam_535520` | Still working. Auto-routes to M9 and runs correctly. | **M9** |
| Subnautica: Below Zero | `steam_848450` | Still working. Auto-routes to M11 and runs correctly. | **M11** |
| Sons of the Forest | `steam_1326470` | Still working. Auto-routes to M11 and runs correctly. | **M11** |
| Schedule 1 | `steam_3164500` | Still working. Auto-routes to M11 and runs correctly. Also launches with M12 bottle config. | **M11**, M12 also works |
| Valheim | Not recorded | Still working. Auto-routes to M11 and runs correctly. Also works with M9. | **M11**, fallback **M9** |
| Rain World | Not recorded | Still working. Auto-routes to M11 and runs correctly. | **M11** |
| Undertale | Not recorded | Still working. Auto-routes to M9 and runs correctly. | **M9** |
| Among Us | `steam_945360` | Still working. Routes to M11 and runs correctly. | **M11** |
| Hollow Knight | Not recorded | Still working. Routes to M11 and runs correctly. | **M11** |
| Hollow Knight: Silksong | `steam_1030300` | Still working. Routes to M12 and runs correctly. | **M12** |
| Ghostrunner | `steam_1139900` | Auto-routes to M11 and runs correctly. M12 saved config produced unexpected Wine Mono installer prompt behavior. | **M11**, M12 if possible |
| Combat Master | `steam_2281730` | Auto-routed to DX12 but hit Agility error. Saved M11 bottle config ran gameplay with no issues. | **M11** |
| Blasphemous | `steam_774361` | Auto-routed to M11, but the game is 32-bit. Saved to M9 bottle and ran perfectly. | **M9** |
| Dave the Diver | `steam_1868140` | Auto-routed to M11, but the game is 32-bit. Saved to M9 and ran beautifully after deploying D3D9, vcrun2019, and DirectX June 2010. | **M9** |
| Peak | `steam_3527290` | DX12 game. With M12 applied to the bottle, it launched and played perfectly on medium settings. Should auto-route to M12 instead of M11. | **M12**, Medium settings |
| Dark Deception | `steam_332950` | Routed to M12 and hit Agility SDK not found. Saved config to M11 launched with no input. Launching through Steam installed needed runtime assets, then the game launched and worked normally. | **Steam**, then **M11** |

## Half Working / Needs Investigation

| Game | AppID / Bottle | Observed Behavior | Current Best Path |
|---|---:|---|---|
| Subnautica 2 | `steam_1962700` | Launches with M12 pipeline but does not render. | **M12**, render-path investigation |
| Minecraft Legends | `steam_1928870` | Only launches with M12 pipeline but does not render. | **M12**, render-path investigation |
| Stardew Valley | `steam_413150` | Launches from Steam, then crashes with Wine debug output. Bottle config could not be updated away from Native macOS. | Bottle config save bug |
| Palworld | `steam_1623730` | DX12/DX11 game. M11 and M12 both produce a black loading window with no visible output. | M11/M12 render-path investigation |
| Mirror's Edge | `steam_17410` | Auto-routed to M11 and launched, but this is a DX9 game and the routing does not make sense. Loaded oddly. | Should route to **M9** |
| Resident Evil Village | `steam_1196590` | DX12 game. M12 stages DLLs but does not launch because Agility SDK x64 payload is not found for version `default`. With DLLs staged, it reached a black launch before Wine debug crash. | **M12**, Agility payload fix |
| Borderlands 2 | `steam_49520` | Only launches on M9 without immediate crash, but never makes it past the loading screen. | **M9**, loading investigation |
| InZOI | `steam_2456740` | DX12-exclusive game. Requires feature level 2. Auto-routes to M12 and launches with M12, but does not enter game or output color. Useful DX12-specific test target. | **M12**, feature/render investigation |
| Black Myth Wukong | `steam_2358720` | DX12/DX11 game. Routed to M12 and hit Agility SDK not found. Changed to M11 bottle config with no error but no launch. Launched with Steam using `-d3d11` and compatibility mode. | Needs to launch through D3D11/D3D12 pipelines from the app |

## Not Working

| Game | AppID / Bottle | Failure | Notes |
|---|---:|---|---|
| Necesse | Not recorded | Does not load with M11 bottle config or any tested config. | Supposedly supports DX11/OpenGL. |
| Dredge | Not recorded | Does not launch with any tested pipeline. | Mono/FNA/XNA title. |
| Celeste | Not recorded | Does not launch with any tested pipeline, and pipeline config could not be changed during testing. | Mono/FNA/XNA title plus bottle config save issue. |
| Elden Ring | Not recorded | Launches with M11, but anti-cheat blocks the path. Offline play is not solved yet. | Anti-cheat/offline route needed. |
| No Man's Sky | Not recorded | Not a Direct3D-first game; primarily Vulkan. | Needs Vulkan backend or bottle option such as MoltenVK/VKD3D-style route. |

## Backend And Bottle Usability Notes

- Bottle saves appear to work in at least some cases: Peak did not launch before M12 routing, but launched correctly after the bottle was moved to M12.
- Game cards can still report the old graphics routing method after a saved bottle pipeline change. If a bottle is moved to M12, the card should reflect `DXMT (M12)`.
- Some bottles could not be changed after routing had already been saved. Celeste and Stardew Valley exposed this most clearly, alongside Dredge potentially.
- Pipeline override persistence should be treated as a backend/UI correctness issue, not only a game-compatibility issue.
- Route detection needs better 32-bit and API-aware correction. Blasphemous and Dave the Diver should not stay on M11 when M9 is the working route, and Mirror's Edge should not auto-route to M11 as a DX9 title.
- Peak should be added to the routing rules as an M12 target.
- Dark Deception shows that launching through Steam can install required runtime assets before the saved route works correctly. The runtime should try a Steam launch first on a fresh installation, then proceed with graphics routing once Steam installs redistributables and queues the game for launch.

## Notes

- Game cards can be tested through the pipeline dropdown.
- Shader caches are per appid and can be cleared from Settings.
- Steam and MacOS Steam are separate launch paths. The current target is Wine Steam for installed Steam games.
- Installed Wine Steam games create `steam_<appid>` bottle records for runtime asset/component preflight before launch.
- Env-dependent Steam game routes keep Wine Steam alive as the background client, then launch the game executable directly with the selected MTSP pipeline, bottle prefix, route env, and Steam identity variables.
