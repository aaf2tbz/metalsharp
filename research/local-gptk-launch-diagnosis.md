# Code Context

## Files Retrieved
1. `app/src-rust/src/d3dmetal_gptk.rs` (lines 24-27) - GPTK DLL list and `WINEDLLOVERRIDES` string.
2. `app/src-rust/src/d3dmetal_gptk.rs` (lines 316-355) - D3DMetal launch environment used by the backend.
3. `app/src-rust/src/d3dmetal_gptk.rs` (lines 750-779) - Homebrew GPTK payload replacement/verification paths.
4. `app/src-rust/src/d3dmetal_gptk.rs` (lines 782-805) - GPTK prefix wineboot environment.
5. `app/src-rust/src/d3dmetal_gptk.rs` (lines 975-996) - seed writes `steam_appid.txt` and launch metadata.
6. `app/src-rust/src/d3dmetal_gptk.rs` (lines 1135-1141, 1166-1180, 1190-1203) - framework readiness, hard-coded Homebrew app root, DYLD path.
7. `/Users/alexmondello/.metalsharp/d3dmetal-gptk/bottles/steam_1962700/state.json` - current D3DMetal readiness state for Subnautica 2.
8. `/Users/alexmondello/.metalsharp/bottles/steam_1962700/bottle.json` - bottle manifest and last launch metadata.
9. `/tmp/metalsharp-pr230-gptk-wine-layout.txt` - local GPTK Wine app layout.
10. `/tmp/metalsharp-pr230-gptk4-prefix-and-winedir-20260630-101432/01-source.txt` and `02-after-copy.txt` - source/Homebrew/prefix GPTK DLL and framework hash evidence.
11. `/tmp/metalsharp-pr230-gptk4-prefix-and-winedir-20260630-101432/launch-overrides.log` and `05-inspect.txt` - loader trace from explicit launch with overrides.
12. `/Users/alexmondello/.metalsharp/d3dmetal-gptk/bottles/steam_1962700/logs/play-1782835430.log` and `play-1782835629.log` - backend play logs.
13. `/Users/alexmondello/.metalsharp/prefix-gptk/drive_c/users/crossover/AppData/Local/Subnautica2/Saved/Logs/Subnautica2.log` - current UE/game log path.

## Key Code

Backend D3DMetal route is intentionally direct-game-exe through Homebrew GPTK Wine:

```rust
// app/src-rust/src/d3dmetal_gptk.rs:24-27
const GPTK_PE_DLLS: &[&str] =
    &["d3d10.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll", "nvapi64.dll", "nvngx-on-metalfx.dll"];
const GPTK_OVERRIDES: &str =
    "d3d10,d3d11,d3d12,dxgi,nvapi64,nvngx-on-metalfx=n,b;gameoverlayrenderer,gameoverlayrenderer64=d";
```

```rust
// app/src-rust/src/d3dmetal_gptk.rs:343-355
let mut cmd = Command::new(homebrew_wine64());
cmd.arg(&game_exe)
    .args(&launch_args)
    .current_dir(game_exe.parent().unwrap_or_else(|| Path::new("/")))
    .env("WINEPREFIX", gptk_prefix())
    .env("WINEARCH", "win64")
    .env("WINEDEBUG", "-all")
    .env("WINEDLLOVERRIDES", GPTK_OVERRIDES)
    .env("SteamAppId", &appid)
    .env("SteamGameId", &appid)
    .env("SteamOverlayGameId", &appid)
    .env("SteamAppUser", "MetalSharp")
    .env("DYLD_FALLBACK_LIBRARY_PATH", gptk_dyld_path())
```

Payload replacement copies GPTK PE DLLs to `/Applications/Game Porting Toolkit.app/Contents/Resources/wine/lib/wine/x86_64-windows` and framework to `/Applications/Game Porting Toolkit.app/Contents/Resources/wine/lib/external/D3DMetal.framework` (`app/src-rust/src/d3dmetal_gptk.rs:750-779`). Prefix `system32` is not managed by this function; prefix copies present locally came from experiments, not the backend replacement path.

## Architecture

- The active D3DMetal state is `steam_1962700`, appid `1962700`, game dir `/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2`, selected exe `/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2/Subnautica2/Binaries/Win64/Subnautica2-Win64-Shipping.exe`.
- Backend launch uses one shared GPTK prefix: `/Users/alexmondello/.metalsharp/prefix-gptk`.
- Homebrew GPTK wine is hard-coded to `/Applications/Game Porting Toolkit.app/Contents/Resources/wine/bin/wine64`; `/opt/homebrew/bin/wine64` is a symlink there.
- DYLD path built by backend is GPTK wine `lib`, `lib/wine/x86_64-unix`, `lib/wine/x86_32on64-unix`, and `lib/external`.
- Runtime GPTK source payload exists under `/Users/alexmondello/.metalsharp/runtime/wine/lib/gptk/x86_64-windows` plus `/Users/alexmondello/.metalsharp/runtime/wine/lib/external/D3DMetal.framework`.

## Local diagnosis

### Finding 1 - High severity: loader traces show `dxgi.dll` is still loading as Wine builtin, not clearly as native D3DMetal/GPTK

Evidence:
- `/tmp/metalsharp-pr230-gptk4-prefix-and-winedir-20260630-101432/03-launch.txt` records launch with `overrides=d3d10,d3d11,d3d12,dxgi,nvapi64,nvngx-on-metalfx=n,b;gameoverlayrenderer,gameoverlayrenderer64=d`.
- The associated trace in `/tmp/metalsharp-pr230-gptk4-prefix-and-winedir-20260630-101432/05-inspect.txt` and `launch-overrides.log` shows the game exe native, but `dxgi.dll` as builtin: `Loaded L"...Subnautica2-Win64-Shipping.exe" ...: native` followed by `Loaded L"C:\\windows\\system32\\dxgi.dll" ...: builtin`.
- The same pattern appears in `/tmp/metalsharp-pr230-gptk-bare-system32-20260630-101116/bare-system32-wine64.log`.

Interpretation: this is the strongest local explanation for “no D3D12 adapters”. If DXGI is Wine builtin instead of GPTK/D3DMetal DXGI, D3D12 adapter enumeration can legitimately return none or never reach D3DMetal. The local payload is present, so the issue is likely DLL resolution/override semantics for this GPTK Wine, registry override state, or copied DLL placement/registration rather than missing files.

### Finding 2 - High severity: backend play logs suppress loader evidence

Evidence:
- Backend sets `WINEDEBUG=-all` at `app/src-rust/src/d3dmetal_gptk.rs:349`.
- Current backend play logs only show preloader warnings and UE/Steam init messages; no `loaddll`, DXGI, D3D12, or D3DMetal load evidence:
  - `/Users/alexmondello/.metalsharp/d3dmetal-gptk/bottles/steam_1962700/logs/play-1782835430.log`
  - `/Users/alexmondello/.metalsharp/d3dmetal-gptk/bottles/steam_1962700/logs/play-1782835629.log`

Interpretation: local failures cannot be conclusively tied to D3DMetal from backend logs because backend intentionally disables Wine debug channels. The tmp traces are more useful than the official play logs.

### Finding 3 - Medium severity: app-local D3D/DXGI shims are mostly quarantined, but native `nvapi64.dll` remains app-local

Evidence from `find /Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2`:
- Quarantined: `.metalsharp/d3dmetal-quarantine/20260630-100640/{d3d10.dll,d3d11.dll,d3d12.dll,dxgi.dll}`.
- Backup: `.metalsharp/pipeline-backup/{d3d10core.dll,d3d11.dll,d3d12.dll,dxgi.dll,nvapi64.dll}`.
- Still present app-local:
  - `/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2/nvapi64.dll`
  - `/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2/Engine/Binaries/Win64/nvapi64.dll`
  - `/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2/Subnautica2/Binaries/Win64/nvapi64.dll`
  - app-local `d3d10core.dll` also remains in Engine/Binaries and game Binaries.

Interpretation: this probably does not directly cause “no D3D12 adapters” if DXGI/D3D12 are not native, but it is a confounder. `nvapi64=n,b` could prefer these native app-local copies, possibly changing adapter/vendor behavior or introducing side effects.

### Finding 4 - Low/Medium severity: GPTK payload files and D3DMetal.framework appear correctly staged

Evidence:
- `/tmp/metalsharp-pr230-gptk4-prefix-and-winedir-20260630-101432/02-after-copy.txt` shows hash parity for `d3d10.dll`, `d3d11.dll`, `d3d12.dll`, `dxgi.dll`, `nvapi64.dll`, and `nvngx-on-metalfx.dll` between MetalSharp source, Homebrew GPTK wine dir, and prefix `system32`.
- Runtime framework files exist under `/Users/alexmondello/.metalsharp/runtime/wine/lib/external/D3DMetal.framework/Versions/A/`, including `D3DMetal`, `libdxcompiler.dylib`, `libdxccontainer.dylib`, `libdxilconv.dylib`, `libmetalirconverter.dylib`, and `default.metallib`.
- Backend framework readiness check only requires `Versions/A/D3DMetal` and at least one dylib resource (`app/src-rust/src/d3dmetal_gptk.rs:1135-1141`).

Interpretation: missing D3DMetal.framework is unlikely. The larger question is whether Wine ever loads the PE DLL that bridges to this framework.

### Finding 5 - Medium severity: current UE game log is empty after recent launch

Evidence:
- `/Users/alexmondello/.metalsharp/prefix-gptk/drive_c/users/crossover/AppData/Local/Subnautica2/Saved/Logs/Subnautica2.log` is `0` bytes as of the latest inspection.
- Earlier backend log `play-1782835430.log` reached UE pak mounting and Steam/EOS initialization, then stopped at Steam/EOS failure messages; it did not show RHI/D3D12 adapter enumeration.

Interpretation: latest launch may be failing before UE writes its game log, or the log location moved/was truncated. The reported “no D3D12 adapters” may have come from UI/dialog rather than this file.

### Finding 6 - Low severity: backend state says the bottle is ready, but that only verifies file presence/hash and seed material

Evidence:
- `/Users/alexmondello/.metalsharp/d3dmetal-gptk/bottles/steam_1962700/state.json`: `gptk_payload=updated`, `x64_redist=installed`, `seed=seeded`, `play_ready=true`, last launch log `play-1782835629.log`.
- `/Users/alexmondello/.metalsharp/bottles/steam_1962700/bottle.json`: runtime profile/preferred pipeline both `d3dmetal`, health `ready`.

Interpretation: `play_ready` does not prove native DXGI/D3D12 is actually chosen at runtime. It proves staged assets exist and hash-match.

## Start Here

Open `/tmp/metalsharp-pr230-gptk4-prefix-and-winedir-20260630-101432/05-inspect.txt` first. It has the decisive local trace: launch used GPTK overrides, payload hashes matched, but `dxgi.dll` still appears as builtin.

## Next experiments

1. Run a one-off launch with `WINEDEBUG=+loaddll,+module,+seh` and no `-all`, preserving the exact backend env, to confirm whether `d3d12.dll` and `dxgi.dll` are native or builtin.
2. Query the GPTK prefix registry DLL override state with `wine reg query 'HKCU\\Software\\Wine\\DllOverrides'` and `HKLM` equivalent; compare with env override behavior.
3. Try explicit per-DLL env overrides that remove ambiguity: `WINEDLLOVERRIDES=dxgi=n;d3d12=n;d3d11=n;d3d10=n;nvapi64=d;nvngx-on-metalfx=n,b` and inspect loader traces. This separates DXGI/D3D12 from NVAPI confounders.
4. Try launching from a clean throwaway GPTK prefix with only Homebrew wine-dir GPTK DLLs, not prefix `system32` copies, to test whether prefix-cached builtin/native metadata is involved.
5. Temporarily quarantine remaining app-local `nvapi64.dll` files and app-local `d3d10core.dll` files for one run, then restore; observe whether adapter enumeration changes.
6. If native DXGI still does not load, test with `wine64 start /unix <exe>` and with direct Z: path to see if loader path mode changes native/builtin resolution.
7. If native DXGI/D3D12 does load but adapters are still zero, switch focus to D3DMetal.framework dynamic loading (`DYLD_PRINT_LIBRARIES=1`, `DYLD_PRINT_RPATHS=1`) and Metal device availability.

## Supervisor coordination

No supervisor decision needed. Inspection only; no files edited except this requested report.

## Review findings

- **High:** `dxgi.dll` loader trace shows builtin despite GPTK override and staged native DLLs. Likely root cause candidate for no D3D12 adapters.
- **High:** backend launch sets `WINEDEBUG=-all`, hiding loader/D3DMetal evidence in normal play logs.
- **Medium:** app-local `nvapi64.dll` remains in several locations and could confound adapter/vendor path.
- **Medium:** latest UE Subnautica2 log is empty; no durable game-side adapter evidence found locally.
- **Low:** payload/framework staging appears correct; readiness state is file/hash readiness, not runtime adapter proof.

## Residual risks

- Wine trace label `builtin` could have GPTK-specific nuances; confirm with a minimal PE/DLL load test before making a code change.
- The exact “no D3D12 adapters” message was not found in local searched logs; it may be a dialog, a truncated log, or from a different run.
- Current state includes manual experiments (prefix `system32` D3D DLL copies) that backend code does not normally create.

```acceptance-report
{
  "criteriaSatisfied": [
    {
      "id": "criterion-1",
      "status": "satisfied",
      "evidence": "Concrete findings include paths and severities under Review findings; primary evidence is /tmp/metalsharp-pr230-gptk4-prefix-and-winedir-20260630-101432/05-inspect.txt showing dxgi.dll as builtin despite overrides, plus app/src-rust/src/d3dmetal_gptk.rs line ranges for launch env and payload staging."
    }
  ],
  "changedFiles": [],
  "testsAddedOrUpdated": [],
  "commandsRun": [
    {
      "command": "find/read/ls inspections of /tmp/metalsharp-pr230-* , ~/.metalsharp, and app/src-rust/src/d3dmetal_gptk.rs",
      "result": "passed",
      "summary": "Mapped artifacts, state files, prefix DLLs, Homebrew GPTK layout, logs, and backend launch code."
    },
    {
      "command": "grep/find targeted searches for D3D/DXGI/adapter evidence in tmp artifacts and ~/.metalsharp logs",
      "result": "passed",
      "summary": "Found loader traces showing dxgi builtin; did not find the literal no-adapters message in persisted logs."
    }
  ],
  "validationOutput": [],
  "residualRisks": [
    "Wine trace builtin/native semantics should be confirmed with a minimal test before code changes.",
    "The literal no-D3D12-adapters message was not present in inspected log files.",
    "Manual local experiments modified prefix/system32 state, so clean-prefix behavior may differ."
  ],
  "noStagedFiles": true,
  "notes": "No edits were made other than writing this requested report."
}
```
