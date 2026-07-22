# Ubisoft Connect Sharp Library Integration Plan

## Objective

Add a dedicated Ubisoft Connect source to Sharp Library, modeled after the GOG experience, with:

- an isolated Ubisoft Wine prefix;
- official Ubisoft Connect installation and Start/Stop controls;
- local discovery and synchronization of downloaded Ubisoft games;
- game launching through Ubisoft's `uplay://` protocol;
- launcher and game process lifecycle management;
- suppression of recurring `winedbg` crash windows; and
- no Ubisoft Connect launcher card in the game library.

The integration must use MetalSharp's installed Wine 11.5 runtime and must not depend on CrossOver-specific fixes.

## Research basis

The proposed design combines proven patterns from several open-source projects:

- [Heroic Games Launcher](https://github.com/Heroic-Games-Launcher/HeroicGamesLauncher) installs the official launcher, launches Ubisoft-managed titles through `uplay://launch/<id>`, and demonstrates why shutdown must be scoped to the title's Wine prefix.
- [Bottles](https://github.com/bottlesdevs/Bottles) discovers Ubisoft configuration data, launches games through Ubisoft Connect, disables overlay behavior, and configures launcher close behavior.
- [Lutris](https://github.com/lutris/lutris) parses Ubisoft configuration and ownership records, Wine registry install records, and `uplay_install.state`.
- [gcenx/Wineskin](https://github.com/Gcenx/WineskinServer) provides the Wine registry settings used to disable Wine debugger dialogs.
- Valve Wine commits [334f7a7](https://github.com/ValveSoftware/wine/commit/334f7a78190a0a0a4abede0d598b28a2743f468a) and [10a46db](https://github.com/ValveSoftware/wine/commit/10a46db3c80835d7a6038dbbac5163531dab6158) implement the write-copy behavior required by `UplayWebCore.exe`.

Heroic and Wineskin are useful implementation references, but neither can substitute for fixing MetalSharp's own Wine runtime. Heroic still depends on the selected Wine runtime, and the current upstream-style gcenx Wine tree does not contain Valve's `WINE_SIMULATE_WRITECOPY` implementation.

## Runtime constraint

MetalSharp uses:

```text
~/.metalsharp/runtime/wine/bin/metalsharp-wine
~/.metalsharp/runtime/wine/bin/wine
~/.metalsharp/runtime/wine/lib/wine/x86_64-unix/ntdll.so
```

The active runtime reports Wine 11.5 and its Unix ntdll is x86_64. The existing `/Volumes/AverySSD/Arm64WINE_Build/wine-11.5` configuration produces an arm64 Unix ntdll, so its current build artifact cannot be copied into the installed runtime.

This work will **not rebuild all of Wine**. It will create a separate x86_64 Wine 11.5 configuration, compile only Unix ntdll, and replace only:

```text
runtime/wine/lib/wine/x86_64-unix/ntdll.so
```

The PE ntdll modules, Wine loader, Wine server, graphics libraries, and all unrelated runtime files remain unchanged.

## Phased implementation

### Phase 0: Preserve and characterize the installed runtime — Complete

1. Record hashes, architecture, Mach-O load commands, exported symbols, deployment target, and version for the installed ntdll.
2. Back up the installed ntdll outside the runtime directory.
3. Create a disposable copy of the MetalSharp runtime for initial testing.
4. Provide an atomic rollback procedure.
5. Confirm that the disposable runtime behaves identically before patching.

**Completion gate:** The original ntdll can be restored immediately and the baseline disposable runtime passes Wine startup tests.

**Evidence:** [`ubisoft-connect-phase-0-1-evidence.md`](ubisoft-connect-phase-0-1-evidence.md)

### Phase 1: Incrementally rebuild only x86_64 Unix ntdll — Complete

1. Create a separate x86_64 Wine 11.5 build configuration compatible with the installed runtime.
2. Use installed MetalSharp build tools where appropriate, including `~/.metalsharp/runtime/wine/bin/winebuild`.
3. Adapt Valve's write-copy changes to Wine 11.5 in:
   - `dlls/ntdll/unix/loader.c`
   - `dlls/ntdll/unix/unix_private.h`
   - `dlls/ntdll/unix/virtual.c`
4. Build only:

   ```bash
   make dlls/ntdll/ntdll.so
   ```

5. Validate architecture, exports, load commands, rpaths, version, and isolated-prefix startup.
6. Functionally verify `WINE_SIMULATE_WRITECOPY=1` and automatic `UplayWebCore.exe` handling.
7. Swap the patched ntdll into the disposable runtime first, then atomically replace the installed ntdll only after validation.

**Completion gate:** The patched x86_64 ntdll passes startup and write-copy tests without changing any other Wine component.

**Evidence:** [`ubisoft-connect-phase-0-1-evidence.md`](ubisoft-connect-phase-0-1-evidence.md)

### Phase 2: Prove Ubisoft Connect manually

1. Create a temporary isolated prefix.
2. Run `wineboot -u`.
3. Configure Wine debugger suppression:

   ```text
   HKLM\Software\Microsoft\Windows NT\CurrentVersion\AeDebug\Debugger = false
   HKCU\Software\Wine\WineDbg\ShowCrashDialog = 0
   ```

4. Download and run the official `UbisoftConnectInstaller.exe`.
5. Start Ubisoft Connect with `WINEDEBUGGER=none` and `WINE_SIMULATE_WRITECOPY=1`.
6. Verify that WebCore remains alive, login renders, the launcher restarts, and no recurring debugger windows appear.
7. Verify prefix-scoped shutdown with the matching Wine server.

**Completion gate:** Ubisoft Connect is usable with MetalSharp Wine before application UI work begins.

### Phase 3: Add the dedicated Ubisoft backend

Create `app/src-rust/src/ubisoft.rs` and register its routes in `app/src-rust/src/main.rs`.

Use isolated storage:

```text
~/.metalsharp/bottles/ubisoft-prefix/prefix/
~/.metalsharp/ubisoft/installers/
~/.metalsharp/ubisoft/library.json
~/.metalsharp/ubisoft/logs/
```

Initial endpoints:

```text
GET  /sharp-library/ubisoft/status
POST /sharp-library/ubisoft/initialize-prefix
POST /sharp-library/ubisoft/remove-prefix
POST /sharp-library/ubisoft/install
POST /sharp-library/ubisoft/start
POST /sharp-library/ubisoft/stop
```

Backend responsibilities:

- verify runtime write-copy capability;
- initialize and configure the dedicated prefix;
- apply debugger suppression;
- download and validate the official installer transactionally;
- install and start Ubisoft Connect with the required environment;
- update supported launcher settings without discarding unrelated settings;
- log launcher activity separately; and
- stop only the Ubisoft prefix using its exact `WINEPREFIX` and Wine server.

**Completion gate:** Every manual operation from Phase 2 works through a backend endpoint.

### Phase 4: Synchronize locally installed games

Add:

```text
POST /sharp-library/ubisoft/sync
GET  /sharp-library/ubisoft/games
GET  /sharp-library/ubisoft/cover?id=...
```

Discovery sources:

```text
cache/configuration/configurations
HKLM\Software\Ubisoft\Launcher\Installs\<id>\InstallDir
uplay_install.state
known executable paths
```

Synchronization will:

1. locate supported Ubisoft configuration paths;
2. parse framed YAML configuration records;
3. extract stable install and launch IDs, localized titles, artwork, install paths, and executable information;
4. verify local installation through registry and filesystem evidence;
5. deduplicate records by Ubisoft identifier;
6. write `library.json` transactionally; and
7. preserve the previous valid cache if fresh parsing fails.

MetalSharp will not handle Ubisoft passwords, authentication tokens, or unofficial account APIs. Authentication stays inside the official launcher.

**Completion gate:** A game downloaded through Ubisoft Connect appears exactly once after synchronization.

### Phase 5: Add game launch and process lifecycle

Add:

```text
POST /sharp-library/ubisoft/play
POST /sharp-library/ubisoft/stop-game
```

Launch games through:

```text
UbisoftConnect.exe uplay://launch/<launch-id>/0
```

Lifecycle requirements:

- start Ubisoft Connect automatically when required;
- identify games using exact launch IDs, including `-upc_uplay_id <id>` where available;
- do not mistake the persistent launcher for the running game;
- stop an individual game only when its process is identified safely;
- reserve prefix-wide termination for the explicit Stop Ubisoft action; and
- never use unscoped `killall wine` behavior.

**Completion gate:** A game launches by stable Ubisoft ID and its running state clears after exit even when Ubisoft Connect remains open.

### Phase 6: Guarantee launcher hiding

Update `app/src-rust/src/sharp_library.rs` so exact Ubisoft launcher executables are excluded from game discovery:

```text
UbisoftConnect.exe
upc.exe
UbisoftGameLauncher.exe
UbisoftGameLauncher64.exe
Uplay.exe
```

Avoid broad substring matching that could hide real games. The dedicated Ubisoft prefix will also remain outside ordinary installer-bottle scanning.

**Completion gate:** Ubisoft Connect never appears as a game card, while installed Ubisoft games remain visible.

### Phase 7: Add the Ubisoft Sharp Library tab

Update `app/src/renderer/views/SharpView.vue` to support explicit source modes:

```ts
"installers" | "gog" | "ubisoft"
```

Add header controls for:

- Initialize/Remove Prefix
- Install Ubisoft Connect
- Start Ubisoft
- Stop Ubisoft
- Sync Games

Render explicit states for runtime incompatibility, missing prefix, missing launcher, sign-in instructions, empty installed library, synchronization errors, and installed games. Ubisoft Connect itself remains a header-level service and is never rendered as a card.

**Completion gate:** The full flow works through Sharp Library without terminal commands.

### Phase 8: Automated and manual validation

Automated coverage will include:

- runtime capability detection;
- safe prefix initialization and removal;
- installer validation and atomic writes;
- debugger registry generation;
- required launch environment;
- framed configuration parsing, including malformed input;
- localized title fallback;
- Wine registry and `uplay_install.state` parsing;
- stable deduplication;
- exact launcher filtering; and
- exact process/launch-ID matching.

Project validation:

```bash
cargo fmt --check
cargo test
npm run build
git diff --check
```

Manual acceptance will cover ntdll rollback, existing Steam/GOG startup, clean Ubisoft installation, absence of recurring `winedbg`, login persistence, prefix-scoped Start/Stop, downloaded-game synchronization, launcher hiding, URI game launch, and game-state cleanup.

### Phase 9: Package the one-file runtime update

1. Store the adapted Valve patches and attribution under `tools/wine/patches/`.
2. Document or script the reproducible x86_64 incremental ntdll build.
3. Add verification for ntdll architecture and write-copy capability.
4. Replace only `runtime/wine/lib/wine/x86_64-unix/ntdll.so` in a candidate runtime bundle.
5. Verify that all unrelated Wine files retain their previous hashes.
6. Test the candidate bundle in a clean MetalSharp installation before promotion.

**Completion gate:** The distributed runtime includes the patched ntdll without rebuilding or replacing the rest of Wine.

## Pull request boundaries

The draft PR should remain reviewable in two logical sections:

1. **Runtime prerequisite:** adapted Valve patches, incremental x86_64 ntdll tooling, capability checks, rollback instructions, and the one-file candidate runtime update.
2. **Ubisoft integration:** backend, discovery, lifecycle, launcher hiding, Sharp Library UI, tests, and documentation.

## Out of scope

- Full Wine rebuilds.
- CrossOver-only fixes or runtime dependencies.
- Unofficial Ubisoft account authentication or credential handling.
- Synchronizing uninstalled account-owned games in the initial implementation.
- Guarantees for anti-cheat-protected multiplayer titles.
- Global Wine process termination.
