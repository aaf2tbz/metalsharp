# Games Supported

Updated: 2026-06-09

Tested and working games organized by pipeline. Only games confirmed playable are listed.

## Test System

Games were tested from an external 1TB M.2 SSD (~5000 MB/s over USB-C 3.1) on an M4 MacBook Air with 16GB RAM.

## Pipelines

| Pipeline | Backend | Use |
|---|---|---|
| **D3DMetal** | Apple GPTK 4.0 | D3D11/D3D12 via Apple's D3DMetal framework |
| **M12** | DXMT | D3D12 to Metal |
| **M11** | DXMT | D3D11 to Metal |
| **M10** | DXMT | D3D10 to Metal |
| **M9** | DXMT | D3D9 to Metal |
| **Mono/FNA** | MonoKickstart + FNA | XNA/FNA/MonoGame via native Mono runtime |

Internal routes (`dxmt` auto-detect, Wine Steam, macOS Steam, `wine_bare`) remain backend machinery and are not shown in bottle selectors.

---

## D3DMetal

Games running through Apple's Game Porting Toolkit 4.0 D3DMetal pipeline.

*No games currently confirmed working through D3DMetal. This pipeline is available for investigation and GPTK-specific testing.*

---

## M12 — D3D12 to Metal

| Game | AppID | Notes |
|---|---:|---|
| PEAK | 3527290 | Medium settings. |
| Hollow Knight: Silksong | 1030300 | |
| Schedule I | 3164500 | Also works on M11. |

---

## M11 — D3D11 to Metal

| Game | AppID | Notes |
|---|---:|---|
| Repo | 3241660 | Online play through Steam. |
| Skul: The Hero Slayer | 1147560 | |
| The Witcher 3: Wild Hunt | 292030 | |
| Stumble Guys | 16677740 | Online Steam session. |
| The Wilds | 1028590 | |
| Totally Accurate Battle Simulator | 508440 | |
| Subnautica | 264710 | |
| Subnautica: Below Zero | 848450 | |
| The Long Dark | 305620 | Ultra settings verified. |
| Thronefall | 2239150 | |
| Sons of the Forest | 1326470 | |
| Schedule I | 3164500 | |
| Rain World | 312520 | |
| Among Us | 945360 | |
| Hollow Knight | 367520 | |
| Valheim | 892970 | Fallback M9. |
| Combat Master | 2281730 | Saved M11 bottle config required. |
| Ghostrunner | 1139900 | |

---

## M10 — D3D10 to Metal

*No games currently confirmed working through M10. Available for D3D10-specific titles.*

---

## M9 — D3D9 to Metal

| Game | AppID | Notes |
|---|---:|---|
| Nidhogg 2 | 535520 | |
| Undertale | 391540 | |
| Dave the Diver | 1868140 | 32-bit. Requires vcrun2019 + DX Jun2010. |
| Blasphemous | 774361 | 32-bit. Sync-loading mitigation active. |

---

## Mono/FNA — XNA/FNA/MonoGame

| Game | AppID | Notes |
|---|---:|---|
| Celeste | 504230 | FNA/XNA assets, FMOD shims, Steamworks shim. x86_64 Mono. |
| Terraria | 105600 | TerrariaLauncher/patcher support, x86_64 Mono, XNA/FNA assemblies. |
| DREDGE | 1562430 | Auto-detected as FNA flavor. Generic FNA config. |

---

## Notes

- Game cards can be tested through the route dropdown in each game's bottle workspace.
- Shader caches are per-appid and can be cleared from Settings.
- Wine Steam remains the background Steam client for installed Windows Steam games.
- Installed Wine Steam games create `steam_<appid>` bottle records for runtime asset/component preflight before launch.
- Env-dependent Steam routes keep Wine Steam alive as the background client, then launch the game executable directly with the selected pipeline, bottle prefix, route env, and Steam identity variables.
