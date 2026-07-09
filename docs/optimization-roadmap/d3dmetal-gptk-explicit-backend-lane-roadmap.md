# D3DMetal / GPTK Explicit Backend Lane Roadmap
**Updated:** 2026-07-08


## Objective

Replace the current mixed GPTK handling with a clean, explicitly named D3DMetal/GPTK backend lane that does not race current installer/migration/bottle code and does not expect GPTK during initial install or migration.

This roadmap intentionally avoids implementation details that reuse the current generic `bottles`, `installer`, or `platform` orchestration paths. The new work should live behind explicit GPTK/D3DMetal names while still running through the MetalSharp backend.

## Non-goals

- Do not require GPTK during MetalSharp install.
- Do not require GPTK during migration.
- Do not silently seed GPTK prefixes during generic bottle save.
- Do not silently install VC++ redistributables during seed.
- Do not launch Steam for D3DMetal play.
- Do not reuse generic installer/platform/bottle component flows for this lane.
- Do not stage a separate MetalSharp-owned GPTK app/Wine copy.

## Required user flow

### 1. User saves a game with a D3DMetal bottle

When the user is in the MetalSharp library and saves a game as a D3DMetal bottle:

1. Backend enters the explicit D3DMetal/GPTK lane.
2. Backend checks for Homebrew GPTK.
3. If needed, backend performs explicit Homebrew trust for the GPTK cask/tap path required by `brew install game-porting-toolkit`.
4. Backend runs/ensures:
   - `brew install game-porting-toolkit`
   - `softwareupdate --install-rosetta --agree-to-license`
5. Backend verifies the Homebrew-owned GPTK app at `/Applications/Game Porting Toolkit.app` has its self-consistent D3DMetal route DLLs/framework.
6. Backend records the bottle as D3DMetal-prepared enough to show next explicit actions.

MetalSharp must not bundle, stage, replace, or patch GPTK itself. Homebrew owns GPTK; MetalSharp only consumes the installed Homebrew payload.

Saving the bottle must not seed VC runtime DLLs/registry and must not seed Steam/user/game payloads.

### 2. User sees honest next actions

After a D3DMetal bottle is saved and Homebrew GPTK + Rosetta are prepared, the UI should show two separate actions:

- `Repair Redist`
- `Seed Prefix`

If prefix seed previously failed, the seed label becomes:

- `Repair Seed`

### 3. User clicks `Repair Redist`

Backend behavior:

1. Do not download or run any Microsoft VC++ redistributable installer for this lane.
2. Copy MetalSharp-bundled VC runtime DLLs from `~/.metalsharp/runtime/wine/lib/wine` into the GPTK prefix:
   - x64 DLLs into `drive_c/windows/system32`.
   - x86 DLLs into `drive_c/windows/syswow64`.
3. Write explicit VC runtime registry keys for both `x64` and `x86` under `Software\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes`.
4. Verify both DLL presence and registry presence before marking the action installed.
5. If verification fails, mark the VC runtime action as repairable and keep showing `Repair Redist`.

### 4. User clicks `Seed Prefix`

Backend behavior:

1. Use Homebrew GPTK Wine.
2. Create/wineboot the MetalSharp-owned GPTK prefix.
3. Ensure Homebrew GPTK’s own D3DMetal framework path and dyld paths are active.
4. Copy Homebrew GPTK route DLLs from `/Applications/Game Porting Toolkit.app/Contents/Resources/wine/lib/wine/x86_64-windows` into the GPTK prefix `drive_c/windows/system32`:
   - `d3d10.dll`
   - `d3d11.dll`
   - `d3d12.dll`
   - `dxgi.dll`
   - `nvapi64.dll`
   - `nvngx-on-metalfx.dll`
5. Quarantine app-local D3D/DXGI/NVAPI/Winemetal shims near the game exe so they cannot win over the Homebrew-matched prefix route DLLs.
6. Immediately seed the prefix with:
   - Steam redist/user files needed by the game.
   - Game executable routing material.
   - Game config files.
   - Any other D3DMetal-specific seed material explicitly required for direct game launch.
7. Verify all seed requirements:
   - Homebrew GPTK DLLs/framework are present where expected.
   - GPTK prefix exists and is winebooted.
   - Steam/user seed files are present.
   - Game exe material is present.
   - Game config files are present.
6. If any verification fails, mark seed as repairable and show `Repair Seed`.

### 5. User clicks Play on D3DMetal bottle

Backend behavior:

1. Do not launch Steam.
2. Launch the game exe directly through Homebrew GPTK Wine.
3. Use the MetalSharp-owned GPTK prefix.
4. Apply the proven launch shape:
   - `WINEDLLOVERRIDES=d3d10,d3d11,d3d12,dxgi,nvapi64,nvngx-on-metalfx=n,b;gameoverlayrenderer,gameoverlayrenderer64=d`
   - `D3DMETAL_FRAMEWORK_PATH=/Applications/Game Porting Toolkit.app/Contents/Resources/wine/lib/external/D3DMetal.framework/D3DMetal`
   - `DYLD_FALLBACK_LIBRARY_PATH` including Homebrew GPTK `lib`, Wine unix dirs, and `lib/external`
   - `WINEESYNC=1`
5. Launch state should be recorded as a D3DMetal direct-game launch, not a Steam launch.

## Proposed backend shape

Use explicit new names instead of generic component paths. Suggested module/route naming:

- `d3dmetal_gptk` backend module.
- `POST /d3dmetal/bottles/save`
- `POST /d3dmetal/bottles/install-x64-redist`
- `POST /d3dmetal/bottles/seed-prefix`
- `POST /d3dmetal/bottles/play`
- `GET /d3dmetal/bottles/status`

The exact route names can change, but they should be explicit and not hidden behind generic repair/install paths.

## State model

Track D3DMetal-specific state separately from generic bottle components.

Suggested states:

```text
gptk_homebrew: missing | installing | installed | failed
gptk_payload: missing | updating | updated | failed  # legacy field name; now means Homebrew GPTK payload verified, not MetalSharp-staged GPTK
x64_redist: missing | installing | installed | repair_required  # legacy field name; now represents copied x64+x86 VC runtime DLL/registry seed
seed: missing | seeding | seeded | repair_required
play_ready: false | true
```

A D3DMetal bottle is play-ready only when:

- Homebrew GPTK is installed.
- Rosetta is installed.
- Homebrew GPTK’s own D3DMetal DLL/framework payload has been verified and its route DLLs copied into the GPTK prefix.
- x64 and x86 VC runtime DLLs and registry keys are seeded into the GPTK prefix from MetalSharp’s runtime.
- Seed verification succeeds.

## UI labels

Use user-facing labels that say exactly what happens:

- `Repair Redist`
- `Seed Prefix`
- `Repair Seed`
- `Play D3DMetal`

Avoid `vcrun2019_x64` in UI text for this lane. This action is explicitly a copied x64+x86 VC runtime DLL/registry seed from MetalSharp’s own runtime, not a Microsoft installer run.

## Verification checklist

Before coding is considered complete:

1. Fresh machine/state with no GPTK should pass D3DMetal bottle save by installing Homebrew GPTK + Rosetta only.
2. Save must not require GPTK during app install or migration.
3. Save must not seed VC runtime DLLs/registry.
4. Save must not seed prefix.
5. `Repair Redist` must copy x64+x86 VC runtime DLLs from MetalSharp’s runtime into the GPTK prefix and write x64+x86 registry keys.
6. Failed VC runtime seed must remain repairable via `Repair Redist`.
7. `Seed Prefix` must wineboot and seed only after VC runtimes are seeded or clearly report missing VC runtimes.
8. Failed seed must become `Repair Seed`.
9. Play must launch the game exe directly, not Steam.
10. Play must use Homebrew GPTK Wine, prefix-seeded Homebrew route DLLs, `D3DMETAL_FRAMEWORK_PATH`, `DYLD_FALLBACK_LIBRARY_PATH`, `WINEESYNC=1`, and `n,b` overrides.

## Implementation sequencing

1. Freeze current generic GPTK behavior; do not extend it further.
2. Add explicit D3DMetal/GPTK status model and JSON persistence.
3. Add Homebrew GPTK install/verify action with trust + Rosetta.
4. Verify Homebrew GPTK route DLL/framework payload without replacing or staging GPTK.
5. Add VC runtime copy/registry repair action.
6. Add seed/repair-seed action.
7. Add direct-game D3DMetal play action.
8. Wire UI to explicit action labels.
9. Add tests for each state transition and failure label.
10. Run live proof through PR backend only after explicit approval.
