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
- The wrapper launches the real executable with `--in-process-gpu --disable-gpu`, matching the working Steam CEF strategy.
- The wrapper also deploys `metalsharp-cefchildhook.dll`; local hook logs show it loads and patches launcher/module `CreateProcessA/W`, `GetProcAddress`, and `ShellExecute` imports.
- The wrapped launcher still renders blank. After clearing Minecraft's `launch_attempts.json` retry guard, logs show CEF initialization, successful network calls, successful XAL token initialization, and the main window opening.
- The remaining blocker is now lower than top-level wrapping: Minecraft logs renderer/GPU subprocess command lines from `MinecraftLauncher_real.exe`, but those launches are not yet passing through the hooked process creation or shell execution paths.
- Minecraft's launcher binary exposes `disableGPU`, `disableGPUCommandLine`, `disableGPUForced`, and `additionalCEFOptions` strings. A bottle-local settings JSON attempt was accepted but did not appear in emitted CEF child command lines.

Evidence:

- `~/.metalsharp/bottles/installer_6a0a76294c1d1364/bottle.json`
- `~/.metalsharp/bottles/installer_6a0a76294c1d1364/logs/launch-1779252105.log`
- `~/.metalsharp/bottles/installer_6a0a76294c1d1364/logs/launch-1779252174.log`
- `~/.metalsharp/bottles/installer_6a0a76294c1d1364/prefix/drive_c/users/alexmondello/AppData/Roaming/.minecraft/launcher_cef_log.txt`
- `~/.metalsharp/bottles/installer_6a0a76294c1d1364/prefix/drive_c/Program Files (x86)/Minecraft Launcher/.ms_cef_compat_MinecraftLauncher`
- `~/.metalsharp/bottles/installer_6a0a76294c1d1364/prefix/drive_c/users/alexmondello/AppData/Local/Temp/metalsharp-cefchildhook.log`

Failure classification:

- `store_bootstrapper_mismatch` for the Microsoft Store `.exe` route
- `cef_rendering_path` for the blank launcher window after MSI install
- `cef_child_creation_path` for the post-install blank UI state

Next action:

Finish the Minecraft CEF child-process path by either reaching the lower-level process creation call or mapping Minecraft's native CEF preference surface correctly, then use the working MSI/bottle install to continue toward starting Minecraft Java from the same bottle.

Mono follow-up:

- See `docs/mono-runtime-lanes.md`.
- Keep Minecraft in the Wine-bottle lane for now because it is a Windows launcher/bootstrapper, not a native FNA game.
- Use the old Terraria/Celeste native Mono lanes as selective fallback profiles for known FNA/XNA games, not as a global replacement for Wine Mono.

### 2026-05-19: EA App installer

Route used:

```text
POST /sharp-library/install {"srcPath":"~/Downloads/EAappInstaller.exe","name":"EA App"}
POST /bottles/repair-component {"id":"installer_16c2e7d7a6e2d5e7","component":"dotnet48"}
POST /bottles/relaunch-installer {"id":"installer_16c2e7d7a6e2d5e7"}
```

Observed result:

- MetalSharp created bottle `installer_16c2e7d7a6e2d5e7`.
- The classifier mapped EA to `runtime_profile=webview`.
- Before this fix, the 32-bit PE fallback still launched the known launcher through `pipeline=M9`.
- The EA bootstrapper downloaded and verified `EAapp-13.700.0.6213-4218.msi`.
- After the visible install bar completed, the MSI failed with `0x80070643`.
- EA reports that MSI failure as `INST-14-1603`.
- The extracted MSI payload includes `Microsoft.Deployment.WindowsInstaller.dll` and `CustomAction.config` with `<supportedRuntime version="v4.0" />`, so EA is running .NET v4 custom actions during install.
- Installing `dotnet48` repaired the bottle component state, but the relaunch still reproduced `INST-14-1603`.

Evidence:

- `~/.metalsharp/bottles/installer_16c2e7d7a6e2d5e7/bottle.json`
- `~/.metalsharp/bottles/installer_16c2e7d7a6e2d5e7/prefix/drive_c/users/alexmondello/AppData/Local/Temp/EA_app_20260519233518.log`
- `~/.metalsharp/bottles/installer_16c2e7d7a6e2d5e7/prefix/drive_c/users/alexmondello/AppData/Local/Temp/EA_app_20260519233838.log`
- `~/.metalsharp/bottles/installer_16c2e7d7a6e2d5e7/prefix/drive_c/users/alexmondello/AppData/Local/Temp/msi56c0.tmp-/CustomAction.config`

Failure classification:

- `ea_inst_14_1603`
- `msi_custom_action_failure`
- `launcher_bootstrapper_needs_bare_wine`
- `webview_profile_needs_dotnet48`

Next action:

Rerun EA from a fresh WebView bottle after the known-launcher bare-Wine routing and WebView `dotnet48` provisioning changes, then inspect the generated MSI log instead of only the outer WiX bootstrapper log.

### 2026-05-19: BattlEye BERCon artifact

Observed result:

- `~/Downloads/BERCon.exe` is a PE32 console app, not a service installer.
- Embedded strings identify it as `BattlEye RCon v0.94 beta`.
- The binary exposes remote-console command-line help: `BERCon [-host IP ADDRESS / HOSTNAME] [-port PORT] [-pw PASSWORD]`.
- PE imports are limited to console/process basics plus `WS2_32.dll`; there is no visible `BEService`, driver, service-install, or anti-cheat runtime payload in this artifact.

Evidence:

- `file ~/Downloads/BERCon.exe`
- `shasum -a 256 ~/Downloads/BERCon.exe`
- `strings -a ~/Downloads/BERCon.exe`
- `i686-w64-mingw32-objdump -p ~/Downloads/BERCon.exe`

Failure classification:

- `artifact_mismatch`

Next action:

Do not use BERCon as proof that the BattlEye runtime is installed. Pick a real Steam title that ships BattlEye/BEService assets, then capture the launch-time service behavior from the game bottle and Launch Doctor.
