# WTMKT Anti-Cheat Hard-Route Roadmap
**Updated:** 2026-07-08


This roadmap covers the post-VM plan for MetalSharp anti-cheat/runtime support. The current local Linux VMs are ARM64, so they cannot serve as clean x86_64 Steam/Proton control machines. MetalSharp has to collect its own evidence and reduce the failure to concrete runtime contracts.

## Phase 1: Anti-Cheat Evidence Collector

Build a backend report that gathers the launch evidence for a Steam appid:

- Easy Anti-Cheat service logs and launcher logs.
- BattlEye launcher/service logs when present.
- Steam `gameprocess_log.txt` and `runprocess_log.txt`.
- EAC settings context: product id, sandbox id, deployment id, executable path, launcher title, downloaded module target, Wine version, exit code, and module mapping status.
- Clear next-action hints for known failures such as `Failed to map the anti-cheat module`.

The goal is not to declare success. The goal is to turn "anti-cheat failed" into a repeatable report we can diff after every Wine/runtime change.

Initial backend surface:

```http
POST /steam/anticheat-evidence
{"appid":1888160}
```

The report returns a normalized `status`, a human summary, EAC fields, Steam protected-launch fields, collected artifact paths, log tails, and next-action hints. For Rubicon, the expected current status is `module_mapping_failed`, with EAC setup exit `0`, module target `linux64`, Wine version `11.5`, launcher exit `206`, and Steam tracking `start_protected_game.exe`.

## Phase 2: Wine Module-Mapping Probe

Create a small probe that exercises the same class of module mapping that protected launchers expect from Wine:

- host module mapping,
- executable memory mapping,
- syscall dispatch expectations,
- loader transitions,
- dyld/dylib boundary failures on macOS,
- log output that can be attached to the appid evidence report.

Initial safe backend surface:

```http
POST /steam/anticheat-probe
{"appid":1888160}
```

This probe does not load or tamper with anti-cheat modules. It classifies the host/runtime boundary from inspectable evidence: host OS and architecture, Wine runtime paths, EAC module target, game-local anti-cheat module assets, binary magic (`ELF`, `PE`, or `Mach-O`), and whether the selected module implies a Linux-user-space substrate requirement on macOS. If Steam has only staged a download under `steamapps/downloading/<appid>` and the protected launcher or game executables still have unknown/null headers, the probe returns `staged_download_incomplete` instead of treating that payload as launchable. For Rubicon with completed protected-launch evidence, the expected current status is `linux_module_on_darwin_boundary`.

## Phase 3: Proton/Wine Delta Audit

Map MetalSharp Wine against Proton and upstream Wine behavior:

- `ntdll` loader and syscall dispatch,
- wineserver process and handle behavior,
- `steamclient` and `lsteamclient` behavior,
- mmap and memory protection behavior,
- pressure-vessel/container assumptions,
- anti-cheat runtime file layout and module target selection.

Initial backend surface:

```http
POST /steam/anticheat-delta-audit
{"appid":1888160}
```

This report groups the local runtime into audit surfaces:

- Wine loader/syscall baseline: `wine`, `wineserver`, Unix `ntdll.so`, and Windows `ntdll.dll` lanes.
- Wineserver state: whether a live `wineserver` process and per-user socket directory are present during runtime observation. Absence is expected in a clean idle install, but protected launch evidence should show the correct shared server boundary.
- Win32 translation contract: PE `kernel32.dll`, `user32.dll`, and `ntdll.dll` plus Unix-side `ntdll.so`, proving that Windows API calls have the Wine translation lanes required before any graphics or anti-cheat diagnosis is meaningful.
- Steam runtime bridge: Windows `steamclient.dll`/`steamclient64.dll` and whether a Proton-style `lsteamclient` bridge exists.
- Linux runtime assumptions: pressure-vessel, seccomp, and Linux namespaces, which are comparison rows on macOS rather than direct requirements.
- Darwin executable module boundary: whether the host can directly load Linux ELF modules, whether any vendor Mach-O module is present, and whether shipped Linux ELF assets imply a Linux user-space substrate.
- Graphics runtime adjacency: DXMT, DXVK, and MoltenVK assets that must stay intact while protected launch is debugged.
- Anti-cheat module contract: whether EAC selected a Linux module, whether Darwin can directly load it, and whether a vendor macOS module is present.

For Rubicon, the expected status is `blocking_delta_found`: the ordinary Wine/DXMT runtime pieces exist, but the protected launcher selected `linux64`, no vendor Mach-O module was found, and macOS cannot directly load Linux ELF modules.

## Phase 4: macOS Runtime Substrate Decision

Choose the truthful compatibility path:

- vendor-supported macOS anti-cheat module path if available,
- or a signed Linux-user-space compatibility substrate that can satisfy the protected module loader without spoofing, hiding, tamper evasion, or bypass behavior.

Initial backend surface:

```http
POST /steam/anticheat-substrate-decision
{"appid":1888160}
```

The decision report synthesizes the evidence, probe, and delta audit into one explicit result. For Rubicon, the expected current decision is `requires_linux_user_space_substrate_or_vendor_macos_asset`.

### Phase 4: Harmless Host Contract Probe

Add an endpoint that records the host contract without loading protected modules:

```http
POST /steam/anticheat-contract-probe
{"appid":1888160}
```

This endpoint uses the appid only for scoping existing logs and game-local identity. The host probe itself uses synthetic temporary data:

- anonymous read/write memory mapping followed by read/execute protection transition,
- synthetic ELF direct-load attempt through the host dynamic loader,
- Wine loader and wineserver path/state evidence,
- selected EAC module target from scoped protected-launch logs.

Expected macOS result for Elden Ring and Rubicon is `linux_elf_host_gap_confirmed`: EAC selected `linux64`, Wine reached module mapping, and the host dynamic loader does not accept Linux ELF modules directly. That does not prove anti-cheat support is impossible; it proves the next implementation target is a truthful Linux user-space substrate or vendor-supported macOS module assets, not another graphics route.

Allowed paths:

- Build a signed Linux user-space compatibility substrate for ELF module hosting.
- Obtain or document vendor-supported macOS anti-cheat module assets.
- Work with publisher/vendor enablement instead of spoofing trust.

Rejected paths:

- spoof anti-cheat host identity,
- hide MetalSharp or Wine from the protected launcher,
- fake kernel driver support,
- tamper with protected modules,
- claim online anti-cheat support before the protected module maps and launches with vendor-supported assets.

## Current Proof Target

Rubicon showed useful progress but not success: EAC EOS setup completed, protected launch downloaded the `linux64` module, Wine module mapping started under Wine 11.5, and then EAC failed with `Failed to map the anti-cheat module` / exit code 206. That is the first failure to reduce.
