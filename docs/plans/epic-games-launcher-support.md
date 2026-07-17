# Epic Games Launcher Support — Implementation Plan

> **Updated**: 2026-07-16
> **Branch**: `codex/epic-games-launcher`
> **Target**: aaf2tbz/metalsharp — native PE-loader + D3D-to-Metal shim layer for Windows games on Apple Silicon

---

## Context: Epic Games Launcher Architecture

Epic Games Launcher is a Windows desktop application built on **Chromium Embedded Framework (CEF)** for its UI. It is distributed as an MSI installer and requires a Windows 10 (or newer) environment. Understanding what it needs to run cleanly on a non-Windows host is the foundation of this plan.

### What the Launcher Needs

| Requirement | Why |
|---|---|
| **Windows 10 64-bit** bottle | Launcher checks OS version; refuses to run on older Windows |
| **MSI installer support** | Distributed as `EpicInstaller-*.msi` via Epic's CDN |
| **Visual C++ Runtime** | Launcher + CEF subprocesses link against VC++ 2015-2022 DLLs |
| **d3dcompiler_47.dll** | Shader compiler dependency — UI elements use D3D for GPU compositing |
| **Core Windows fonts** | Arial, Times New Roman, etc. — CEF page rendering requires them |
| **Multi-process CEF** | Launcher spawns renderer, GPU, and network subprocesses |
| **GPU-accelerated compositing** | CEF requires working D3D11 or OpenGL context |
| **Robust TLS/SSL stack** | All launcher traffic is HTTPS (login, store, downloads) |
| **Multi-threaded synchronization** | CEF + launcher use multiple threads with inter-process sync |
| **Large-file download** | Game downloads are 10-100+ GB; must survive interrupted connections |

### Launcher Footprint on Windows

- **Install path**: `%ProgramFiles(x86)%/Epic Games/Launcher/Portal/Binaries/Win64/`
- **Key executables**: `EpicGamesLauncher.exe`, `EpicWebHelper.exe` (CEF renderer), `UnrealCEFSubProcess.exe` (GPU)
- **Registry**: `HKLM\SOFTWARE\EpicGames\EpicGamesLauncher` with `InstallLocation`
- **Install GUID**: `{FAC47927-1A6A-4C6E-AD7D-E9756794A4BC}`
- **Game manifests**: `%ProgramData%/Epic/EpicGamesLauncher/Data/Manifests/*.item` (JSON)
- **Download source**: `https://launcher-public-service-prod06.ol.epicgames.com/launcher/api/installer/download/EpicGamesLauncherInstaller.msi`
- **Landing page**: `https://store.epicgames.com/download`

### Compatibility Precedent

The Epic Games Launcher has been a challenging target for Wine-based compatibility layers. The primary obstacles have historically been:

1. **CEF rendering** — Chromium's multi-process GPU compositing under Wine required years of upstream fixes
2. **MSI installer reliability** — `msiexec` under Wine has edge cases
3. **Multi-process synchronization** — Wine's `esync`/`fsync` mechanisms were needed for stable CEF
4. **Auto-update survival** — The launcher self-modifies on boot; Wine prefixes needed to survive this

These are well-understood problems with known solutions in the Wine ecosystem. metalsharp's native PE-loader approach may bypass some (sync, process management) but must still solve CEF rendering and MSI.

---

## Existing metalsharp Architecture (Relevant Files)

| Component | File | Role |
|---|---|---|
| Native launcher | `tools/launcher/NativeLauncher.cpp` | PE loader, Win32 shims, crash handler |
| Steam integration | `tools/launcher/SteamIntegration.h/.cpp` | Game enumeration, library scanning — Epic will follow this pattern |
| Wine prefix | `tools/launcher/WinePrefix.h/.cpp` | Prefix creation, DLL overrides, registry |
| C backend launcher | `app/src-c/launcher.h/.c` | Pipeline-based launch policy |
| C backend bottles | `app/src-c/bottles.h/.c` | Bottle policies, DXMT deployment |
| Steam C shims | `src/steam/bridge/unix_steamclient.c` | Steamworks API surface (Epic equivalent needed for EOS?) |
| Launcher binary | `tools/launcher/NativeLauncher.cpp` | Entry point for game launches |
| Bundles | `app/bundles/steamwebhelper.exe`, `*-wrapper.c` | Steam-specific helper binaries |

**Architecture note**: metalsharp uses a **native PE loader + Win32 API shim layer** (not a full Wine fork). Windows PE binaries are loaded directly into the macOS process; Win32/D3D calls are intercepted via trampoline stubs and forwarded to native macOS APIs (Metal for D3D, etc.). For certain workloads (Steam), it also wraps a Wine prefix for compatibility data. The C backend (`app/src-c/`) provides narrow launch-policy functions called from the Electron shell.

---

## Phased Implementation Plan

### Phase 1 — Foundation: Research & Bottle Infrastructure

- [ ] **1.1** Document Epic Launcher Windows filesystem layout and registry footprint
  - Expected install path, key executables, registry keys (see Context section)
  - Community-built open-source launchers ([Heroic](https://github.com/Heroic-Games-Launcher/HeroicGamesLauncher), [Legendary](https://github.com/derrod/legendary)) as clean-room API reference
  - Epic's own manifest JSON schema for installed games

- [ ] **1.2** Create `win10_64` bottle template for Epic
  - Verify Windows version reporting to `10.0` (Epic Launcher refuses < Win10)
  - Configure base DLL namespace: Wine builtins + metalsharp shims
  - Files: `configs/bottle_templates/` or new template under `app/src-c/`

- [ ] **1.3** Implement MSI installer support
  - Epic provides `.msi` installer (not `.exe`) — metalsharp needs `msiexec` path or MSI extraction
  - Option A: Launch `msiexec /i EpicInstaller-*.msi` inside the Wine prefix
  - Option B: Extract MSI contents with `msiextract` (msitools) and deploy files directly
  - Detection glob: `EpicInstaller-*.msi`
  - Download hook: Epic's CDN URL (see Context)

- [ ] **1.4** Create `tools/launcher/EpicIntegration.h/.cpp` skeleton
  - Model on `SteamIntegration.h/.cpp`:
    ```cpp
    struct EpicGame {
        std::string appName;       // e.g. "Fortnite"
        std::string installDir;    // e.g. "C:\Program Files\Epic Games\Fortnite"
        std::string launchExe;     // e.g. "FortniteGame.exe"
        std::string version;
    };
    class EpicIntegration {
        static std::string findEpicInstallDir();
        static std::vector<EpicGame> enumerateLibrary(const std::string& epicDir);
        static std::string findGameExecutable(const std::string& gameDir);
        static std::string defaultDownloadDir();
    };
    ```

- [ ] **1.5** Add launch policy for Epic pipeline
  - Extend `app/src-c/launcher.h` with epic-specific launch discriminants
  - Register with `metalsharp_launch_policy()` routing table
  - Define: graphics backend (D3DMetal), DLL overrides, direct-executable flag

### Phase 2 — Shared Launcher Dependencies

These are standard Windows runtime components that every game launcher needs. They are not Epic-specific and should be designed as a **shared dependency layer** — Steam, Battle.net, GOG, and others all need the same stack.

- [ ] **2.1** Core Fonts: bundle Windows TrueType fonts
  - Required fonts: Arial, Times New Roman, Courier New, MS Sans Serif
  - Deploy to `%WINDIR%\Fonts\` during bottle initialization
  - These fonts are essential for CEF page rendering in the launcher UI
  - Alternative: configure fontconfig to map to macOS system fonts via `WinePrefix::writeRegistryDllOverrides`

- [ ] **2.2** Visual C++ Redistributable: deploy latest VC++ runtime
  - Source: Microsoft "evergreen" installer from `https://aka.ms/vc14/vc_redist.x64.exe`
  - Both 64-bit and 32-bit DLLs: `vcruntime140.dll`, `msvcp140.dll`, `concrt140.dll`, `vccorlib140.dll`
  - Deploy to `%WinSysDir32%` and `%WinSysDir64%` in the bottle
  - metalsharp approach: extract DLLs from redist installer without running it (skip MSI installer EXE bootstrapper)

- [ ] **2.3** d3dcompiler_47: deploy shader compiler DLL
  - Source: Mozilla's `fxc2` project on GitHub ([d3dcompiler_47.dll 32/64-bit variants](https://github.com/mozilla/fxc2))
  - Placed in `%WinSysDir32%` and `%WinSysDir64%`
  - No DLL override needed — just ensure the file is loadable
  - Prevents "d3dcompiler_47.dll not found" at launcher startup

- [ ] **2.4** CJK font support (Asian character sets)
  - International users see tofu (□) for CJK game names without East Asian fonts
  - Optional for MVP but required for full release
  - Package: subset of Noto CJK or Source Han Sans fonts

- [ ] **2.5** Dependency deployment tests
  - Unit: each DLL is present and loadable in a fresh bottle
  - Integration: Epic Launcher installer completes without missing-dependency errors
  - Isolation: shared deps don't leak into Steam bottles or vice versa

### Phase 3 — Wine & Process Integration Layer

- [ ] **3.1** Multi-process synchronization
  - Epic Launcher spawns multiple CEF processes requiring inter-process sync
  - Wine mechanisms: `WINEESYNC=1` (eventfd-based), `WINEFSYNC=1` (futex-based)
  - metalsharp may use its native `SyncContext.h` for the PE-loaded main process
  - For Wine-wrapped CEF subprocesses, enable ESync/FSync in the prefix
  - Assess whether metalsharp's native loader needs Wine-level sync at all

- [ ] **3.2** CEF (Chromium Embedded Framework) rendering
  - Epic Launcher UI is built on CEF 100+ 
  - Key requirements:
    - GPU compositing via D3D11 or OpenGL (D3DMetal should handle this)
    - Shared memory between browser host and renderer processes
    - Working TLS stack for HTTPS (SecureTransport or GnuTLS backend)
  - Known CEF flags for compatibility environments:
    ```
    --no-sandbox
    --disable-gpu-sandbox
    --disable-features=RendererCodeIntegrity
    --in-process-gpu
    ```
  - Store as launch-policy env vars; test progressively removing sandbox flags

- [ ] **3.3** Wine prefix registry defaults
  - Windows version: `win10_64` (10.0)
  - Font replacements: map missing Windows fonts to available ones
  - Certificate store: ensure root CA bundle is present for HTTPS
  - Browser emulation: set IE version key for embedded webviews

- [ ] **3.4** Child process lifecycle management
  - Process tree: launcher → CEF renderer → CEF GPU → game executable
  - metalsharp native loader intercepts EXE spawns; route through appropriate pipeline
  - Ensure launcher's self-update cycle doesn't orphan child processes
  - Pattern: `WINE_WAIT_CHILD_PIPE_IGNORE` for Wine-subprocess boundaries

### Phase 4 — Launcher-Specific Implementation

- [ ] **4.1** `EpicIntegration::findEpicInstallDir()`
  - Search priority: registry key → known paths → GUID-based detection
  - Registry: `HKLM\SOFTWARE\EpicGames\EpicGamesLauncher\InstallLocation`
  - Known path: `%ProgramFiles(x86)%/Epic Games/Launcher`
  - GUID fallback: scan uninstall registry for `{FAC47927-1A6A-4C6E-AD7D-E9756794A4BC}`

- [ ] **4.2** `EpicIntegration::enumerateLibrary()` — game manifest parsing
  - Manifests path: `%ProgramData%/Epic/EpicGamesLauncher/Data/Manifests/*.item`
  - Format: JSON, one file per installed game
  - Parse: `AppName`, `InstallLocation`, `LaunchExecutable`, `AppVersionString`
  - Support multiple library directories (user-configurable in Epic Launcher settings)

- [ ] **4.3** Launcher MSI installation bootstrap
  - Launch `msiexec /i EpicInstaller-*.msi` inside the configured Wine prefix
  - Detect existing install via GUID; skip if already present
  - Surface installation progress to the Electron UI
  - Download integrity: verify SHA256 of downloaded MSI before installing

- [ ] **4.4** Auto-update cycle handling
  - Epic Launcher checks for updates and applies them on every boot
  - The update process rewrites the launcher's own binaries in-place
  - Must survive: bottle integrity after update, detection of new launcher version
  - Post-update re-scan: re-verify install GUID and paths

- [ ] **4.5** Authentication flow
  - Epic uses web-based OAuth (CEF page → `epicgames.com/id/login`)
  - Requires: working HTTPS, cookies, localStorage in CEF
  - Ensure TLS stack works for Epic's CDN and auth domains
  - Fallback consideration: external browser auth (like Heroic's approach)

- [ ] **4.6** Launcher UX integration in metalsharp
  - Add Epic as a game source in the Electron UI (model on Steam source)
  - Launcher status: installed / needs update / launching / running
  - Game library: surface Epic-installed games alongside Steam games

### Phase 5 — Game Launch Pipeline

- [ ] **5.1** Game installation through the launcher
  - Verify Epic's download/verify/unpack pipeline works in the bottle
  - Detect new game manifests after installation completes
  - Game manifests appear instantly after download starts (pre-allocate); detect completion via manifest fields

- [ ] **5.2** Per-game launch policy routing
  - Epic Launcher spawns game EXE via `CreateProcess`
  - metalsharp intercepts and routes through the appropriate `MetalsharpBottlePolicy`
  - Map Epic `AppName` to metalsharp pipeline profiles (D3DMetal, DXVK, etc.)
  - Use existing `metalsharp_bottle_policy()` infrastructure

- [ ] **5.3** EAC / Easy Anti-Cheat assessment
  - Many Epic titles use EAC (Fortnite, Rocket League, Fall Guys)
  - EAC does not currently work under Wine, Proton, or metalsharp
  - Document explicitly as "not supported" for now
  - Track upstream EAC-on-Linux progress for future enablement

- [ ] **5.4** Unreal Engine game support
  - Epic-published games are overwhelmingly Unreal Engine (D3D11/D3D12)
  - metalsharp's D3DMetal pipeline handles UE rendering well (validated with UE4/UE5 demos)
  - UE-specific considerations: shader model 6, ray tracing (defer), Nanite (D3D12 only)

- [ ] **5.5** External library directory support
  - Epic supports multiple game library directories (including external drives)
  - Scan all configured paths from Epic's settings
  - Common Mac pattern: games installed on external SSD via symlink or separate library

### Phase 6 — Testing & Acceptance Gates

- [ ] **6.1** Smoke: Launch Epic Games Launcher
  - Fresh bottle, install Epic via MSI
  - Launcher window appears and renders correctly
  - No crash within first 5 minutes; process tree healthy

- [ ] **6.2** Auth: Sign in to Epic account
  - Login page loads and renders in CEF
  - Complete OAuth sign-in flow
  - Session persists across launcher restarts

- [ ] **6.3** UI: Browse library and store
  - Library tab loads owned games list
  - Store pages render functional content
  - Settings dialog works

- [ ] **6.4** Download: Install a game
  - Pick a free game from the library
  - Download completes without corruption
  - Verify/install pass succeeds
  - Game appears in metalsharp library view

- [ ] **6.5** Launch: Run an installed game
  - Click Play → game process spawns and renders
  - D3DMetal pipeline active (verify with frame stats)
  - Clean exit; no crash on close

- [ ] **6.6** Regression: Steam support unaffected
  - Existing Steam games still launch and play
  - Steam bottle not corrupted by shared deps
  - No cross-contamination between launcher prefixes

- [ ] **6.7** Platform: Apple Silicon (arm64)
  - All tests pass on M-series Macs
  - Native arm64 pipeline used (no Rosetta 2 dependency)
  - Memory usage reasonable (<2GB for launcher + light game)

### Phase 7 — Release Readiness

- [ ] **7.1** UI integration
  - Epic as a game source in the Electron shell
  - Install/launch/status controls for Epic Launcher
  - Epic-installed games in the main library grid

- [ ] **7.2** CI pipeline update
  - Epic Launcher smoke test in CI
  - Automated bottle dependency verification
  - MSI installation test (offline with pre-downloaded MSI)

- [ ] **7.3** Documentation
  - User guide: setting up Epic Games Launcher
  - Developer docs: `EpicIntegration` API and pipeline architecture
  - Known issues: EAC games, CEF quirks, unsupported titles

- [ ] **7.4** CHANGELOG entry
  - Feature announcement
  - Version bump: minor (new feature, non-breaking)

---

## Key Risks & Open Questions

| Risk | Impact | Mitigation |
|---|---|---|
| **CEF rendering under native PE loader** — Chromium multi-process may not work in metalsharp's shim layer | High | Test early with `--no-sandbox`; fall back to Wine prefix for CEF subprocesses if needed |
| **MSI installer support** — metalsharp may lack `msiexec` path | Medium | Use `msiextract` as static alternative; or ship pre-configured prefix |
| **Multi-process sync** — Native PE loader may need Wine-level sync for CEF | Medium | ESync/FSync well-tested in Wine ecosystem; metalsharp can enable per-prefix |
| **Auto-update survival** — Launcher self-modifies on boot | Medium | Post-update re-scan; GUID-based re-detection |
| **EAC games** — Fortnite and other EAC titles won't work | Low (documented) | Mark as unsupported; track upstream EAC-on-Linux progress |
| **Steam interference** — Shared deps may leak into Steam bottles | Low | Isolate Epic in separate prefix; validate regression test gate |

---

## Timeline Estimate (Rough)

| Phase | Effort | Dependencies |
|---|---|---|
| Phase 1: Foundation | 2-3 days | None |
| Phase 2: Shared Deps | 1-2 days | Phase 1 |
| Phase 3: Wine Integration | 3-5 days | Phase 1 (parallel with Phase 2) |
| Phase 4: Launcher Specific | 3-4 days | Phase 1 + 3 |
| Phase 5: Game Pipeline | 2-3 days | Phase 4 |
| Phase 6: Testing | 2-3 days | Phase 1-5 |
| Phase 7: Release | 1 day | Phase 6 |

**Total**: ~14-21 days for MVP, longer for CEF/stability polish on edge-case hardware.
