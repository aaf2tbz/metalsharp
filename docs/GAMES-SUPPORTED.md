# Games Supported

These notes reflect the current MetalSharp pipeline names.

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
| **Steam** | Wine Steam |
| **MacOS Steam** | Native macOS Steam |
| **Wine** | Plain Wine custom-library fallback |

## Compatibility Notes

| Game | Status | Recommended |
|---|---|---|
| Schedule 1 | M12 works. M11 is more optimized. | **M11**, Medium settings |
| Subnautica | M11 works well and is most optimized. | **M11**, High/Medium settings |
| Subnautica: Below Zero | M12 looks better. M11 is more optimized. | **M12**, Medium/Low settings |
| Rain World | M11 is best. M9 also works well. | **M11** |
| Undertale | M32 is best. M9 also works. | **M32**, fallback **M9** |
| Nidhogg 2 | M32 is best. | **M32**, High settings |
| Ghostrunner | Only M12 works. | **M12**, FSR Balanced/Performance, Medium/High, V-Sync Off |
| Garry's Mod | Launches on M9 and M32, but needs more work. M32 reports Steam connection issues. | Work in progress |
| Portal 2 | Best with M9. Audio does not work once inside the game. Goldberg deploys. | **M9** |
| Hollow Knight | Works with native Mono ARM launch. | **Native macOS**, High settings |
| Hollow Knight: Silksong | Works with Native macOS and M12. | **Native macOS**, High settings |
| Skate 2 | Not compatible because of kernel-level anti-cheat. | Unsupported |
| Cyberpunk 2077 | Errors out. | Work in progress |
| Resident Evil 4 | Work in progress. | Work in progress |
| Goat Simulator | Does not load yet. | Work in progress |
| Borderlands 3 | Works through Wine Steam. | **Steam**, DX12, Low settings |
| Among Us | Works through native Steam / D3DMetalFX. | **MacOS Steam** |
| Stardew Valley | Runs through native macOS Steam. | **MacOS Steam** |
| Fall Guys | Fails due to anti-cheat. | Unsupported |
| Dredge | Already has Mac support. Native Steam launches an invisible window. | Work in progress |
| Elden Ring | Needs current notes. | Untested |
| High on Life | Works through Steam with `-dx11`. DX12 reports unsupported. | **Steam**, with `-dx11` launch option |

## Notes

- Game cards can be tested through the pipeline dropdown.
- Shader caches are per appid and can be cleared from Settings.
- Portal 2 uses Goldberg Steam emulator deployment.
- Steam and MacOS Steam are separate launch paths.
