# GPTK (D3DMetal) Guide

MetalSharp supports Apple's Game Porting Toolkit (GPTK) as the **D3DMetal** route. This route uses Apple's D3DMetal framework for D3D11/D3D12 translation instead of the DXMT Metal path used by M11/M12.

## How It Works

The D3DMetal route is architecturally different from the other MetalSharp routes:

- **Shared prefix**: All D3DMetal games share a single Wine prefix at `~/.metalsharp/prefix-gptk/`, seeded from your Wine Steam prefix
- **GPTK Wine binary**: Uses Apple's `wine64` from the Game Porting Toolkit, not MetalSharp's built-in Wine
- **D3DMetal framework**: Translation happens through Apple's D3DMetal.framework and its PE DLLs (`d3d11.dll`, `d3d12.dll`, `dxgi.dll`, etc.)
- **External game storage**: D3DMetal games can be migrated to external SSDs for better performance

## Setup

1. Install the Game Porting Toolkit via Homebrew:
   ```bash
   brew install game-porting-toolkit
   ```
   Or install it through MetalSharp's setup wizard.

2. MetalSharp creates the GPTK prefix automatically when you first launch a D3DMetal game. The prefix is seeded with:
   - Your Wine Steam prefix registry hives (`system.reg`, `user.reg`, `userdef.reg`)
   - Steam directory structure
   - VC++ 2015-2022 redistributable (x86 + x64)
   - Dosdevice symlinks matching your Wine Steam prefix

3. The VC++ runtime is installed automatically during prefix seeding. If it's missing, use Runtime Doctor or the bottle repair option.

## When to Use D3DMetal vs M11/M12

| | D3DMetal | M11/M12 |
|---|---|---|
| **Translation** | Apple D3DMetal framework | DXMT (custom Metal backend) |
| **Wine** | GPTK `wine64` | MetalSharp Wine |
| **Prefix** | Shared (`prefix-gptk`) | Per-game bottle |
| **Best for** | Games that need Apple's translation | Most games, better compatibility tracking |

Use M11 or M12 as the default. Switch to D3DMetal if a game has specific rendering issues on the DXMT path.

## GPTK Prefix Location

```
~/.metalsharp/prefix-gptk/
├── .gptk-ready          Marks prefix as fully seeded
├── drive_c/
│   ├── windows/         Windows system directory with GPTK DLLs
│   └── Program Files (x86)/Steam/   Steam directory (synced from prefix-steam)
├── dosdevices/          Drive letter symlinks
├── system.reg           Registry (copied from prefix-steam)
├── user.reg             Registry (copied from prefix-steam)
└── userdef.reg          Registry (copied from prefix-steam)
```

## Prefix Syncing

MetalSharp syncs the GPTK prefix with your Wine Steam prefix on every D3DMetal launch:

- Copies `user.reg` from `prefix-steam` to `prefix-gptk`
- Syncs Steam config files
- Ensures dosdevice symlinks are intact

## Component Repair

The D3DMetal bottle profile includes these components:

| Component | Purpose |
|---|---|
| `gptk` | Game Porting Toolkit installation |
| `rosetta` | Rosetta 2 for x86_64 translation |
| `gptk_prefix` | Prefix seeding and readiness |
| `vcrun2019` | VC++ 2015-2022 redistributable |

Use Runtime Doctor (`POST /bottles/doctor`) or the bottle repair UI to check and fix these components.

## Troubleshooting

- **"GPTK prefix is not ready"**: Repair the `gptk_prefix` component in bottle settings. This re-seeds the prefix from your Wine Steam prefix.
- **VC++ missing**: Repair the `vcrun2019` component. MetalSharp will download and install the Microsoft VC++ redistributable into the GPTK prefix.
- **Game can't find Steam**: Ensure the dosdevice symlinks in `~/.metalsharp/prefix-gptk/dosdevices/` are correct. MetalSharp auto-creates these during prefix sync.
- **External drive not visible**: Add a dosdevice symlink: `ln -sf /Volumes/YourDrive ~/.metalsharp/prefix-gptk/dosdevices/z:`
