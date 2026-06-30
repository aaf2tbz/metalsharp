# Games Supported

Updated: 2026-06-30

Tested and working games organized by pipeline. Only games confirmed playable are listed.

## Test System

Games were tested from an external 1TB M.2 SSD (~5000 MB/s over USB-C 3.1) on an M4 MacBook Air with 16GB RAM.

## Pipelines

| Pipeline | Backend | Use |
|---|---|---|
| **D3DMetal** | Homebrew GPTK / Apple D3DMetal | D3D11/D3D12 via Apple's D3DMetal framework. GPTK is installed through Homebrew and is not bundled by MetalSharp. |
| **M12** | DXMT | D3D12 to Metal |
| **M11** | DXMT | D3D11 to Metal |
| **M10** | DXMT | D3D10 to Metal |
| **M9** | DXMT | D3D9 to Metal |
| **Mono/FNA** | MonoKickstart + FNA | XNA/FNA/MonoGame via native Mono runtime |

Internal routes (`dxmt` auto-detect, Wine Steam, macOS Steam, `wine_bare`) remain backend machinery and are not shown in bottle selectors.

---

## D3DMetal

Games running through Homebrew GPTK and Apple's D3DMetal pipeline. D3DMetal bottles use the explicit Save → Repair Redist → Seed Prefix → Play D3DMetal flow, with route DLLs copied from `/Applications/Game Porting Toolkit.app` into the shared GPTK prefix.

| Game | AppID | Notes |
|---|---:|---|
| Elden Ring | 1245620 | Offline play. |
| ARMORED CORE VI FIRES OF RUBICON | 1888160 | Offline play. |
| High On Life | 1583230 | Also works on M12. |

---

## M12 — D3D12 to Metal

| Game | AppID | Notes |
|---|---:|---|
| PEAK | 3527290 | Medium settings. |
| Hollow Knight: Silksong | 1030300 | |
| Schedule I | 3164500 | |
| Ghostrunner | 1139900 | |
| Yu-Gi-Oh! Master Duel | 1449850 | |
| Dark Deception | 332950 | Runtime bootstrap required on first launch. |

---

## M11 — D3D11 to Metal

| Game | AppID | Notes |
|---|---:|---|
| Repo | 3241660 | |
| Cult of the Lamb | 1313140 | |
| The Witcher 3: Wild Hunt | 292030 | |
| The Wilds | 1028590 | |
| The Long Dark | 305620 | Ultra settings verified. |
| Subnautica | 264710 | |
| Rain World | 312520 | |
| Hollow Knight | 367520 | |
| Party Animals | 1823720 | Steam online play. |

---

## M10 — D3D10 to Metal

*No games currently confirmed working through M10. Available for D3D10-specific titles.*

---

## M9 — D3D9 to Metal

| Game | AppID | Notes |
|---|---:|---|
| Dave the Diver | 1868140 | 32-bit. Requires vcrun2019 + DX Jun2010. |
| Mirror's Edge | 17410 | Sync-loading mitigation active. |
| Half-Life 2 | 220 | |
| Portal 2 | 620 | Steam Emu supported. |
| Among Us | 945360 | Steam online play. |
| Team Fortress 2 | 440 | Steam online play. VAC works. |

---

## Mono/FNA — XNA/FNA/MonoGame

| Game | AppID | Notes |
|---|---:|---|
| Celeste | 504230 | FNA/XNA assets, FMOD shims, Steamworks shim. x86_64 Mono. Install wizard fallback paths for `steam_api` detection. |
| Terraria | 105600 | TerrariaLauncher/patcher support, x86_64 Mono, XNA/FNA assemblies. |
| DREDGE | 1562430 | Auto-detected as FNA flavor. Generic FNA config. |

---

## Notes

- Game cards can be tested through the route dropdown in each game's bottle workspace.
- Shader caches are per-appid and can be cleared from Settings.
- Wine Steam remains the background Steam client for installed Windows Steam games.
- Installed Wine Steam games create `steam_<appid>` bottle records for runtime asset/component preflight before launch.
- Env-dependent Steam routes keep Wine Steam alive as the background client, then launch the game executable directly with the selected pipeline, bottle prefix, route env, and Steam identity variables.
- D3DMetal is the exception to normal bundled-runtime routing: it uses Homebrew GPTK, a shared `~/.metalsharp/prefix-gptk`, copied x64+x86 VC runtime DLLs, and Homebrew-matched D3DMetal route DLLs in prefix `system32`.
