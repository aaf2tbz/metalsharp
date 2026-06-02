# Games Supported

Updated: 2026-06-02

These notes reflect the current MetalSharp route names, current local playtesting evidence, and the June 2 route-selector cleanup. The visible bottle options are now **M12**, **M11**, **M10**, **M9**, and **Mono/FNA**. Raw `dxmt`, Wine, macOS Steam, M32, and Steam handoff routes remain internal backend machinery.

## Test System

Games were tested from an external 1TB M.2 SSD, roughly 5000 MB/s read/write over USB-C 3.1, on an M4 MacBook Air with 16GB RAM.

## Pipelines

| Visible Route | Use |
|---|---|
| **M12** | D3D12 to Metal via DXMT |
| **M11** | D3D11 to Metal via DXMT |
| **M10** | D3D10 to Metal via DXMT |
| **M9** | D3D9 through the DXMT launch/cache family |
| **Mono/FNA** | Windows XNA/FNA games through MetalSharp's native Mono runtime, staged FNA/XNA assemblies, FMOD/FAudio/FNA3D/native-library shims, and Steamworks shim support |

Internal routes still exist for diagnostics and compatibility records:

| Internal Route | Current Role |
|---|---|
| `dxmt` | Auto-router that chooses M12/M11/M10/M9 from rules and PE evidence |
| Wine Steam | Background account/session/download owner for Steam games |
| macOS Steam | Native Steam handoff for diagnostics or special cases, not a normal Windows-game route |
| Plain Wine / M32 / M13 | Backend fallback and investigation routes, hidden from normal bottle selectors |

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
| Dark Deception | `steam_332950` | Routed to M12 and hit Agility SDK not found. Saved config to M11 launched with no input. A one-time internal Steam handoff installed needed runtime assets, then the saved route launched and worked normally. | **M11** after runtime bootstrap |

## Recent Runtime Hardening

| Area | Current State |
|---|---|
| Public route selector | Only **M12**, **M11**, **M10**, **M9**, and **Mono/FNA** are shown. DXMT auto-routing, Wine, macOS Steam, M32, and M13/GPTK stay internal. |
| Mono/FNA/XNA | First-class route with ARM64 and x86_64 Mono lanes under the hood. The launcher stages FNA/XNA assemblies, native libraries, FMOD/FAudio/FNA3D shims, `steam_appid.txt`, and Steamworks compatibility shims. |
| Celeste/Terraria path | Hardened for future FNA/XNA targets, but not marked working from this pass. Celeste still hit Steamworks initialization failure during testing. Terraria was downloaded for testing but not promoted to working evidence. |
| Goat Simulator | Defined as an M9/D3D9 UE3 title requiring native .NET 4.0 CLR, VC++ 2010, and DirectX June 2010. Current blocker is native `.NET 4.0` install failure in Wine: registry repair clears false install markers, but `clr.dll` still does not land. |
| DREDGE | Reclassified away from FNA/XNA. It is a 32-bit Unity title with embedded MonoBleedingEdge and currently crashes in embedded Mono before reaching graphics routing. |

## Half Working / Needs Investigation

| Game | AppID / Bottle | Observed Behavior | Current Best Path |
|---|---:|---|---|
| Subnautica 2 | `steam_1962700` | Launches with M12 pipeline but does not render. | **M12**, render-path investigation |
| Minecraft Legends | `steam_1928870` | Only launches with M12 pipeline but does not render. | **M12**, render-path investigation |
| Stardew Valley | `steam_413150` | Launches from Steam, then crashes with Wine debug output. Earlier testing was stuck on an old native-macOS handoff label that is no longer a public route option. | Needs fresh public-route test |
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
| Dredge | Not recorded | Does not launch with any tested pipeline. Current evidence points to a 32-bit Unity embedded-Mono crash, not the Mono/FNA/XNA route. | Embedded Unity Mono investigation |
| Celeste | `steam_504230` | Windows install is routed through Mono/FNA x86_64 with FNA/XNA/shim staging. Launch still fails around Steamworks initialization / native Mono behavior. | **Mono/FNA**, not working yet |
| Terraria | `steam_105600` | Mono/FNA route has Terraria-specific runtime staging and x86_64 Mono handling, but current pass did not produce a new working proof. | **Mono/FNA**, needs fresh launch proof |
| Goat Simulator | `steam_265930` | M9 route launches then dies before graphics with native Mono/CLR crash. Runtime doctor correctly reports missing `dotnet40`; .NET 4.0 redist extracts but no native `clr.dll` lands. | **M9**, blocked on native .NET 4.0 |
| Elden Ring | Not recorded | Launches with M11, but anti-cheat blocks the path. Offline play is not solved yet. | Anti-cheat/offline route needed. |
| No Man's Sky | Not recorded | Not a Direct3D-first game; primarily Vulkan. | Needs a future Vulkan backend, not a current public route |

## Backend And Bottle Usability Notes

- Bottle saves appear to work in at least some cases: Peak did not launch before M12 routing, but launched correctly after the bottle was moved to M12.
- Game cards and bottle selectors now use the public route vocabulary: **M12**, **M11**, **M10**, **M9**, and **Mono/FNA**.
- Old saved route values such as `dxmt`, `wine_bare`, `mac_steam`, and `m32` are still parsed by the backend for compatibility, but the renderer filters them out of normal route controls.
- Pipeline override persistence should be treated as a backend/UI correctness issue, not only a game-compatibility issue.
- Route detection needs better 32-bit and API-aware correction. Blasphemous and Dave the Diver should not stay on M11 when M9 is the working route, and Mirror's Edge should not auto-route to M11 as a DX9 title.
- Peak should be added to the routing rules as an M12 target.
- Dark Deception shows that an internal Steam handoff can install required runtime assets before the saved public route works correctly. The runtime should try that bootstrap only when needed, then proceed with graphics routing once Steam installs redistributables and queues the game for launch.

## Notes

- Game cards can be tested through the route dropdown.
- Shader caches are per appid and can be cleared from Settings.
- Wine Steam remains the normal background Steam client for installed Windows Steam games.
- Installed Wine Steam games create `steam_<appid>` bottle records for runtime asset/component preflight before launch.
- Env-dependent Steam game routes keep Wine Steam alive as the background client, then launch the game executable directly with the selected MTSP pipeline, bottle prefix, route env, and Steam identity variables.
