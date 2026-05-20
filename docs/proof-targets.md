# MetalSharp Proof Targets

Status: Phase 10 foundation

The roadmap has to be driven by reproducible evidence. `configs/proof-targets.json` is the starting ledger for installer, launcher, Steam runtime, and anti-cheat proof targets.

## Target Groups

- Installer baseline: Minecraft Launcher, GOG offline installer, Itch/Unity demo, Unreal demo.
- Launcher baseline: EA App, Ubisoft Connect, Battle.net, Epic Games Launcher.
- Steam runtime baseline: known M9, M11, M12, Steam CommonRedist, and Steam third-party launcher cases.
- Anti-cheat baseline: EAC title with Proton support, BattlEye title with Proton support, policy-blocked title, Windows-kernel-only title.

## Evidence Fields

Every target should record:

- install result
- launcher open result
- login/session result
- game launch result
- graphics route
- audio status
- input status
- online/session status
- anti-cheat status
- failure evidence paths
- next action

## Rule

Do not mark a target as working from vibes. A target moves forward only when the matching bottle log, compatdata record, Launch Doctor report, crash log, or screenshot exists.
