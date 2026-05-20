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

## Remaining Work

- Persist child game processes spawned by launchers as bottle app records.
- Track launcher-owned game install folders separately from the launcher EXE.
- Add launcher-specific repair controls for WebView, Gecko, VC runtime, and session data.
- Add end-to-end smoke cases for at least three launchers once redistributable assets and test installers are available.
