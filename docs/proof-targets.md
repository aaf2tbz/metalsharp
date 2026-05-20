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

Fix the Wine Mono/mscompatdb crash and map WebView2 assets, then rerun the installer through the same bottle. This is now a concrete Phase 11 blocker rather than a generic "installer.exe does not launch" report.

### 2026-05-19: Minecraft Launcher legacy MSI route

Route used:

```text
POST /sharp-library/install {"srcPath":"~/.metalsharp/runtime/redist/Minecraft/MinecraftInstaller.msi","name":"Minecraft Launcher Legacy MSI"}
POST /sharp-library/import-bottle-app {"bottleId":"installer_6a0a76294c1d1364","exePath":".../MinecraftLauncher.exe","name":"Minecraft Launcher"}
POST /sharp-library/launch {"id":"bottle_app_ccf06adb8b050608","engine":"wine_bare"}
```

Observed result:

- The official Mojang MSI installed successfully into bottle `installer_6a0a76294c1d1364`.
- The installed launcher was detected at `C:\Program Files (x86)\Minecraft Launcher\MinecraftLauncher.exe`.
- The launcher created bottle-local state under `C:\users\alexmondello\AppData\Roaming\.minecraft`.
- CEF initialized successfully, but the initial launcher window rendered blank.
- General CEF compatibility was then applied by preserving `MinecraftLauncher_real.exe` and replacing `MinecraftLauncher.exe` with an architecture-matched wrapper.
- The wrapper launches the real executable with `--in-process-gpu --disable-gpu`, matching the working Steam CEF strategy while leaving Chromium child command lines intact.
- The wrapped launcher still renders blank. After clearing Minecraft's `launch_attempts.json` retry guard, logs show CEF initialization, successful network calls, successful XAL token initialization, and the main window opening.
- The remaining blocker is now the embedded Chromium subprocess path: Minecraft spawns renderer/GPU subprocesses from `MinecraftLauncher_real.exe`, so the wrapper's top-level flags do not automatically reach the renderer command lines.

Evidence:

- `~/.metalsharp/bottles/installer_6a0a76294c1d1364/bottle.json`
- `~/.metalsharp/bottles/installer_6a0a76294c1d1364/logs/launch-1779252105.log`
- `~/.metalsharp/bottles/installer_6a0a76294c1d1364/logs/launch-1779252174.log`
- `~/.metalsharp/bottles/installer_6a0a76294c1d1364/prefix/drive_c/users/alexmondello/AppData/Roaming/.minecraft/launcher_cef_log.txt`
- `~/.metalsharp/bottles/installer_6a0a76294c1d1364/prefix/drive_c/Program Files (x86)/Minecraft Launcher/.ms_cef_compat_MinecraftLauncher`

Failure classification:

- `store_bootstrapper_mismatch` for the Microsoft Store `.exe` route
- `cef_rendering_path` for the blank launcher window after MSI install
- `cef_child_process_flags` for the post-install blank UI state

Next action:

Add a CEF child-process command-line hook or launcher-specific shim so renderer and GPU subprocesses receive the Wine-safe compositor flags, then use the working MSI/bottle install to continue toward starting Minecraft Java from the same bottle.

Mono follow-up:

- See `docs/mono-runtime-lanes.md`.
- Keep Minecraft in the Wine-bottle lane for now because it is a Windows launcher/bootstrapper, not a native FNA game.
- Use the old Terraria/Celeste native Mono lanes as selective fallback profiles for known FNA/XNA games, not as a global replacement for Wine Mono.
