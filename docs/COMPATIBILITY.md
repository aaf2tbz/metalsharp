# Compatibility Guide

## Status Levels

MetalSharp uses a five-tier compatibility rating system:

| Status | Icon | Description |
|--------|------|-------------|
| **Platinum** | Perfect | Runs flawlessly with no issues |
| **Gold** | Minor issues | Playable with minor graphical glitches or audio quirks |
| **Silver** | Playable | Runs with noticeable issues that don't prevent gameplay |
| **Bronze** | Boots | Starts but major issues prevent normal gameplay |
| **Broken** | Won't run | Crashes on launch or fails to initialize |

## How Compatibility Works

MetalSharp translates Direct3D (D3D11/D3D12) calls to Apple Metal. Game compatibility depends on:

1. **API coverage** — Does the game use Win32 or D3D APIs that MetalSharp implements?
2. **Shader complexity** — Does the game's shader bytecode translate cleanly to Metal?
3. **Anti-cheat/DRM** — Kernel-level anti-cheat systems are incompatible
4. **Graphics API** — D3D11 games have better coverage than D3D12

## Known Compatible Games

Games that have been tested and confirmed working. Submit reports via GitHub issues.

## Known Incompatible Games

Games with kernel-level anti-cheat will not work:

- **Fortnite** (BattlEye + EAC kernel driver)
- **Valorant** (Ricochet kernel driver)
- **PUBG** (BattlEye kernel driver)
- **Apex Legends** (EA AntiCheat kernel mode)

Games with Denuvo VMProtect may have issues during launch but can work once past the DRM check.

## DRM Detection

MetalSharp includes a binary signature scanner that detects 30+ known DRM and anti-cheat systems:

**Anti-cheat:** EasyAntiCheat, BattlEye, Ricochet, EA AntiCheat, ACE, nProtect, GameGuard, XIGNCODE3, VAC, PunkBuster

**DRM:** Denuvo, SecuROM, SafeDisc, VMProtect, Themida, Arxan, Steam Stub, CEG

**Engines:** .NET, Unity, Unreal Engine 4/5, Mono, XNA, FNA, Godot

The game validator (`GameValidator`) automatically scans executables before launch and warns about incompatible systems.

## Reporting Compatibility

1. Launch the game through MetalSharp
2. Note the compatibility status shown in the library
3. If the game crashes, check `~/.metalsharp/crashes/` for the crash report
4. Open a GitHub issue with:
   - Game name and version
   - MetalSharp version (`metalsharp --version`)
   - Compatibility status
   - Crash report (if applicable)
   - Any console output

## Improving Compatibility

The most common reasons games fail:

1. **Missing Win32 API** — Check logs for unresolved imports. These can be shimmed.
2. **Unsupported shader ops** — Check logs for shader translation errors.
3. **D3D feature gaps** — Some advanced D3D11/12 features may not be mapped yet.
4. **Timing issues** — Games using precise Windows timing APIs may behave differently.
