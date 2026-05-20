# MetalSharp Proof Targets

Status: Phase 11 active

The roadmap has to be driven by reproducible evidence. `configs/proof-targets.json` is the starting ledger for installer, launcher, Steam runtime, and anti-cheat proof targets.

## Target Groups

- Installer baseline: Minecraft Launcher, GOG offline installer, Itch/Unity demo, Unreal demo.
- Launcher baseline: EA App, Ubisoft Connect, Battle.net, Epic Games Launcher.
- Steam runtime baseline: known M9, M11, M12, Steam CommonRedist, and Steam third-party launcher cases.
- Anti-cheat baseline: EAC title with Proton support, BattlEye title with Proton support, policy-blocked title, Windows-kernel-only title.

## Evidence Fields

Every target should record:

- source
- appid, installer path, bottle id, compatdata path, runtime profile, launch pipeline, and prefix path
- install result
- launcher open result
- login/session result
- child game process result
- game launch result
- graphics route
- audio status
- input status
- online/session status
- anti-cheat status
- failure classification
- failure evidence paths
- next action

## Rule

Do not mark a target as working from vibes. A target moves forward only when the matching bottle log, compatdata record, Launch Doctor report, crash log, or screenshot exists.

## Phase 11 Run Log

### 2026-05-19: Minecraft Launcher installer

Route used:

```text
POST /sharp-library/install {"srcPath":"~/Downloads/MinecraftInstaller.exe","name":"Minecraft Launcher Phase 11"}
```

Observed result:

- MetalSharp created/reused bottle `installer_057475b8830b64bc`.
- The bottle manifest classifies the target as `runtime_profile=java_launcher`, `installer_kind=java`, `arch=win32`.
- The launcher route started as `pipeline=M9`.
- The process exited quickly and no final launcher app was detected.
- Bottle Doctor reports the prefix exists, but tracked components are not ready and app detection is still empty.
- A manual bare-Wine rerun in the same bottle reproduces the same native crash, so this is not only an M9 graphics-route issue.
- A manual rerun with `WINEDLLOVERRIDES=mscompatdb=d` still reproduces the same native crash.
- `gecko` repair launched, logged, and is now detected from Wine's `system32/gecko` / `syswow64/gecko` locations.
- `corefonts` is now repaired by mapping locally installed host fonts into the bottle font directory.
- `webview2` remains a missing local runtime asset.
- A rerun after font/Gecko cleanup still exits with the same `mscompatdb` / Mono native crash.
- The launch log records repeated `mscompatdb` load attempts followed by a native crash report in the Mono/native runtime path.

Evidence:

- `~/.metalsharp/bottles/installer_057475b8830b64bc/bottle.json`
- `~/.metalsharp/bottles/installer_057475b8830b64bc/logs/launch-1779249524.log`
- `~/.metalsharp/bottles/installer_057475b8830b64bc/logs/launch-1779250143.log`
- `~/.metalsharp/bottles/installer_057475b8830b64bc/logs/manual-winebare-1779249756.log`
- `~/.metalsharp/bottles/installer_057475b8830b64bc/logs/manual-disable-mscompatdb-1779250267.log`
- `~/.metalsharp/bottles/installer_057475b8830b64bc/logs/component-corefonts-1779250127.log`
- `~/.metalsharp/bottles/installer_057475b8830b64bc/logs/component-gecko-1779249576.log`
- `POST /bottles/doctor {"id":"installer_057475b8830b64bc"}`

Failure classification:

- `runtime_bug`
- `missing_runtime_asset`

Next action:

Fix the native Mono/mscompatdb crash and map WebView2 assets, then rerun the installer through the same bottle. This is now a concrete Phase 11 blocker rather than a generic "installer.exe does not launch" report.
