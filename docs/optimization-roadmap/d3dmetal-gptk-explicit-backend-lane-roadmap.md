# D3DMetal / GPTK Explicit Backend Lane Roadmap

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
5. Backend updates/replaces Homebrew GPTK’s D3DMetal DLL/framework payload using MetalSharp’s bundled updated GPTK4 payload.
6. Backend records the bottle as D3DMetal-prepared enough to show next explicit actions.

Saving the bottle must not install x64 VC++ redist and must not seed Steam/user/game payloads.

### 2. User sees honest next actions

After a D3DMetal bottle is saved and GPTK/Homebrew + updated DLL/framework are prepared, the UI should show two separate actions:

- `Install x64 redist`
- `Seed Prefix`

If either previously failed, labels become:

- `Repair x64`
- `Repair Seed`

### 3. User clicks `Install x64 redist`

Backend behavior:

1. Download Microsoft Visual C++ 2015-2022 x64 redistributable from the same x64 URL already used by the install wizard:
   - `https://aka.ms/vs/17/release/vc_redist.x64.exe`
2. Do not rely on a stale bundled/cached MetalSharp asset unless it was freshly downloaded by this action.
3. Run the installer through Homebrew GPTK Wine, non-quiet:
   - `/install`
4. Treat the installer window disappearing / process returning as the completion signal.
5. Verify that the GPTK prefix has an actual completed x64 install marker/state sufficient for D3DMetal readiness.
6. If verification fails, mark the x64 action as repairable and show `Repair x64`.

### 4. User clicks `Seed Prefix`

Backend behavior:

1. Use Homebrew GPTK Wine.
2. Create/wineboot the MetalSharp-owned GPTK prefix.
3. Ensure the wineboot happens with the updated GPTK4 DLL/framework payload active.
4. Immediately seed the prefix with:
   - Steam redist/user files needed by the game.
   - Game executable routing material.
   - Game config files.
   - Any other D3DMetal-specific seed material explicitly required for direct game launch.
5. Verify all seed requirements:
   - Updated GPTK DLLs/framework are present where expected.
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
4. Apply native/builtin overrides for the updated GPTK DLL/framework route:
   - GPTK D3DMetal route DLL overrides must be `n,b`.
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
gptk_payload: missing | updating | updated | failed
x64_redist: missing | installing | installed | repair_required
seed: missing | seeding | seeded | repair_required
play_ready: false | true
```

A D3DMetal bottle is play-ready only when:

- Homebrew GPTK is installed.
- Rosetta is installed.
- Updated GPTK4 DLL/framework payload has been applied to GPTK’s active route.
- x64 VC++ 2015-2022 redist is installed in the GPTK prefix.
- Seed verification succeeds.

## UI labels

Use user-facing labels that say exactly what happens:

- `Install x64 redist`
- `Repair x64`
- `Seed Prefix`
- `Repair Seed`
- `Play D3DMetal`

Avoid `vcrun2019_x64` in UI text for this lane. This is explicitly Microsoft Visual C++ 2015-2022 x64.

## Verification checklist

Before coding is considered complete:

1. Fresh machine/state with no GPTK should pass D3DMetal bottle save by installing Homebrew GPTK + Rosetta only.
2. Save must not require GPTK during app install or migration.
3. Save must not install x64 redist.
4. Save must not seed prefix.
5. `Install x64 redist` must run the Microsoft VC++ 2015-2022 x64 installer non-quiet through Homebrew GPTK Wine.
6. Failed x64 install must become `Repair x64`.
7. `Seed Prefix` must wineboot and seed only after x64 is installed or clearly report missing x64.
8. Failed seed must become `Repair Seed`.
9. Play must launch the game exe directly, not Steam.
10. Play must use GPTK Wine and `n,b` overrides for updated GPTK route DLLs/framework.

## Implementation sequencing

1. Freeze current generic GPTK behavior; do not extend it further.
2. Add explicit D3DMetal/GPTK status model and JSON persistence.
3. Add Homebrew GPTK install/verify action with trust + Rosetta.
4. Add updated GPTK4 DLL/framework replacement action for Homebrew GPTK route.
5. Add x64 redist install/repair action.
6. Add seed/repair-seed action.
7. Add direct-game D3DMetal play action.
8. Wire UI to explicit action labels.
9. Add tests for each state transition and failure label.
10. Run live proof through PR backend only after explicit approval.
