# MetalSharp Launcher Runtime

Status: Phase 3 foundation

MetalSharp treats launcher installers as bottle-managed Windows programs, not as one-off EXEs. The goal is to let launchers keep their login/session state, install child games into the same bottle, and produce logs that explain why a launcher or child game failed.

## Known Launcher Recipes

The installer classifier recognizes these launcher families before generic .NET, WebView, MSI, or PE import heuristics:

- Minecraft Launcher -> Java Launcher profile
- EA App / Origin -> WebView profile
- Ubisoft Connect / Uplay -> WebView profile
- Battle.net / Blizzard launcher -> WebView profile
- Epic Games Launcher -> WebView profile
- Rockstar Games Launcher / Social Club -> WebView profile
- GOG Galaxy -> Launcher profile

Known launcher hints are stored in the classifier output as `known_launcher:<id>` and `launcher_name:<display name>`. These hints make installer bottles more predictable, especially when launcher bootstrapper binaries also contain generic .NET or WebView strings.

## Runtime Behavior

`Install Windows Program` routes launcher-like EXEs and MSI packages into installer bottles. The bottle records:

- source installer path
- installer kind
- runtime profile
- prefix path
- launch log
- launch pid/status
- detected installed app candidates

Minecraft gets a Java Launcher profile even when the bootstrapper includes CLR metadata. Storefront launchers get WebView or Launcher profiles so their bottle dependency set matches the login and embedded-browser surface they actually need.

## CEF Compatibility

Steam already uses a wrapped `steamwebhelper.exe` to force CEF onto a Wine-safe software GPU path. Sharp Library bottles now generalize that behavior for launcher apps that carry CEF or Chromium payloads.

When a bottle app looks like a launcher and its install directory contains CEF assets such as `libcef.dll`, `chrome_*.pak`, `vk_swiftshader.dll`, or `app.asar`, MetalSharp preserves the original executable as `<name>_real.exe` and replaces `<name>.exe` with a small architecture-matched wrapper. The wrapper relaunches the real executable with `--in-process-gpu --disable-gpu`, while Chromium child process command lines are left otherwise intact.

The first proof target is Minecraft Launcher:

- the Microsoft Store `.exe` bootstrapper is a 32-bit .NET/WPF package and can fall into Wine Mono or native .NET setup failure before Minecraft exists
- the official Mojang `MinecraftInstaller.msi` installs cleanly into a `java_launcher` bottle
- `MinecraftLauncher.exe` now receives the generic CEF wrapper, but the current proof still renders a blank surface after CEF initializes

## Remaining Work

- Add a CEF child-process command-line hook for launchers that spawn renderer/GPU subprocesses from the preserved `_real.exe`.
- Persist child game processes spawned by launchers as bottle app records.
- Track launcher-owned game install folders separately from the launcher EXE.
- Add launcher-specific repair controls for WebView, Gecko, VC runtime, and session data.
- Add end-to-end smoke cases for at least three launchers once redistributable assets and test installers are available.
