# Proton Runtime Research

Created: 2026-05-19

Purpose: Phase 0 evidence lock for the MetalSharp macOS Proton roadmap. This document records what Proton actually provides, what CrossOver proves about installer bottles, and what MetalSharp should copy or intentionally avoid.

## Source Baseline

- Valve describes Proton as a compatibility layer for Windows games on Linux that uses a modified Wine plus high-performance graphics API implementations. Source: [Steam Deck and Proton](https://partner.steamgames.com/doc/steamdeck/proton?l=english).
- Proton creates a per-game Wine prefix under `steamapps/compatdata/<appid>/pfx`. Source: [Proton FAQ](https://github.com/ValveSoftware/Proton/wiki/Proton-FAQ).
- Proton exposes runtime options through Steam launch environment such as `PROTON_LOG`, `PROTON_USE_WINED3D`, and other compatibility toggles. Source: [Proton README](https://github.com/ValveSoftware/Proton/blob/proton_11.0/README.md).
- Proton build/release artifacts are structured as redistributable compatibility tools installable under Steam's `compatibilitytools.d`. Source: [Proton README](https://github.com/ValveSoftware/Proton/blob/proton_11.0/README.md).
- Steam Common Redistributables exist because games need shared components such as Visual C++, .NET, DirectX 9, OpenAL, XNA, and PhysX installed before they run. Source: [Steamworks Common Redistributables](https://partner.steamgames.com/doc/features/common_redist?l=english).

## What Proton Is

Proton is not just "Wine with graphics DLLs." It is a Steam-integrated runtime contract:

- a patched Wine distribution
- per-game prefix ownership
- default prefix templates
- Steam-provided game command and appid context
- launch environment generation
- compatibility flags
- graphics translation layers
- audio/input/media compatibility work
- CommonRedist and install-script expectations
- debug logging conventions
- crash/debug hooks
- build artifacts Steam can discover as a compatibility tool

For MetalSharp, the core idea to copy is not Linux-specific container internals. The core idea is: every game launch has an authoritative runtime record, an isolated prefix, a known route, a repairable dependency set, and a reproducible log.

## How Proton Launches A Game

Observed model from public Proton docs:

1. Steam owns the appid, install path, launch command, and user/session context.
2. Proton is selected as the compatibility tool for that game.
3. Proton creates or reuses `steamapps/compatdata/<appid>/pfx`.
4. Proton applies config and compatibility flags for that appid.
5. The Windows executable is launched through Wine inside that prefix.
6. Logs can be enabled with `PROTON_LOG=1 %command%`.

MetalSharp translation:

1. Wine Steam should remain the account/download/session provider.
2. MetalSharp should own the game process route when the game needs route-specific env, DLLs, shims, or bottle assets.
3. Each Steam game should have a MetalSharp compatdata record under `~/.metalsharp/compatdata/<appid>/`.
4. The compatdata record should point at the bottle/prefix, executable, route, dependencies, runtime assets, Steam identity mode, and logs.

## Steam Identity Must Stay Separate From Runtime Authority

The recent Steam bottle work already exposed the important split:

- Steam must stay alive as Steam.
- Steam should not be globally mutated every time one game needs a different graphics/runtime route.
- The game process must still receive correct appid identity and ownership context.
- A game route should be able to fail before launch with a clear readiness error instead of hanging after a socket failure.

For Phase 2, treat this as a hard invariant:

```text
Steam identity/session provider != game runtime authority
```

## CrossOver Evidence For Installer Bottles

CrossOver's Mac guide describes a bottle as a virtual Windows environment with a `C:` drive, registry, CrossOver settings, Windows applications, and user data. It also supports unlisted apps and standalone `.exe` files by creating/running them inside bottles. Source: [CrossOver Mac User Guide](https://www.codeweavers.com/support/docs/crossover-mac/index).

This proves the installer/launcher side of the roadmap is not speculative. The successful pattern is:

- installers run inside bottles
- apps can be installed into an existing bottle
- bottles isolate undesirable settings
- apps can be launched by saved commands
- debug logs and launch options are attached to bottle execution

MetalSharp should continue the Beta 7 bottle model and extend it toward Proton-like compatdata for games.

## Common Redistributables

Steam's documented redistributables include Microsoft Visual C++, .NET, DirectX 9, OpenAL, XNA, and PhysX. Steam also supports install scripts for custom redists. Source: [Steamworks Common Redistributables](https://partner.steamgames.com/doc/features/common_redist?l=english).

MetalSharp needs a local equivalent:

- component detection per bottle
- component receipts per bottle
- repair/reinstall controls
- legal local asset discovery under Steam CommonRedist or MetalSharp runtime redist folders
- logs for redist installer exit codes

## macOS Difference From Linux Proton

Linux Proton can rely on Linux interfaces and, increasingly, Linux kernel support such as `ntsync` for NT synchronization semantics. The Linux kernel docs describe `ntsync` as a compatibility driver for user-space NT emulators, implemented in software and exposed through `/dev/ntsync`. Source: [Linux ntsync documentation](https://www.kernel.org/doc/html/latest/userspace-api/ntsync.html).

macOS does not expose `/dev/ntsync`, Linux futexes, or Linux kernel modules. The macOS plan must map Windows behavior onto Darwin, Mach, pthreads, `ulock`, system frameworks, and Apple-approved extension mechanisms where appropriate.

## MetalSharp Implications

The Proton-like path for MetalSharp is:

1. Keep Wine Steam as identity/session/download provider.
2. Create authoritative compatdata for each Steam appid.
3. Reuse installer bottles for non-Steam and launcher apps.
4. Promote current C/Objective-C shims into a versioned host runtime ABI.
5. Route D3D/audio/input/Steam identity through stable host services.
6. Make launch logs and crash bundles first-class artifacts.
7. Treat anti-cheat as a vendor trust surface, not a graphics/runtime toggle.

## Phase 0 Conclusions

- Proton's useful lesson is runtime discipline: prefix, route, config, logs, redists, appid identity.
- CrossOver's useful lesson is bottle discipline: installers and launchers need stable bottle state, not ad hoc executable launching.
- MetalSharp already has enough bottle and shim code to proceed to Phase 1.
- The next design target is not "make anti-cheat work"; it is "make MetalSharp runtime truth inspectable enough that anti-cheat support has a legitimate path."
