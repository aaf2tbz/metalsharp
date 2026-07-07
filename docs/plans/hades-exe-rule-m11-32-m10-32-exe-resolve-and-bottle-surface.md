# Plan: Hades exe rule + M11(32)/M10(32) exe resolution + bottle DLL-surface surfacing

Branch: `plan/hades-exe-rule-m11-32-m10-32-exe-resolve-and-bottle-surface`
Base: `main` @ `9208843` (0.51.2)

## Goal (three coupled fixes)

1. **Hades game rule** — Hades ships three executables (`x64/Hades.exe`, `x64Vk/Hades.exe`,
   `x86/Hades.exe`). The current rule (`pipeline = "m11"`, no `exe_names`) lets the generic
   exe scorer pick `x64Vk/Hades.exe` (Vulkan) because it scores highest (`x64` + `vk` + 64-bit
   + D3D API + name-token bonuses). Add an explicit rule so the correct D3D11 exe is launched.
2. **M11(32) / M10(32) exe resolution** — the new 32-bit DXMT pipelines are excluded from the
   `direct_wine_pipeline` match arms in `recipe.rs`, so they never resolve an exe and never
   deploy DLLs next to the real binary. Wire them into the same resolution path every other
   DXMT route uses.
3. **Bottle dropdown surface for M11(32) / M10(32)** — `runtime_profile_for_pipeline` maps
   `M11_32`/`M10_32` to `RuntimeProfile::Plain` (empty component set), so auto-synced Steam
   bottles for these routes get no DXMT components, no repair buttons, and no OK state. Fix the
   mapping so the deployed-DLL surface and repair actions appear.

## Evidence

### Hades install (AverySSD Steam library)
```
/Volumes/AverySSD/SteamLibrary/steamapps/common/Hades/
  x64/Hades.exe    -> machine 0x8664 (AMD64)   D3D11
  x64Vk/Hades.exe  -> machine 0x8664 (AMD64)   Vulkan   <-- currently wins the scorer
  x86/Hades.exe    -> machine 0x014c (i386)    D3D11    <-- the correct DXMT M11(32) target
appmanifest_1145360.acf -> installdir "Hades", appid 1145360
```
The game root already contains deployed DLLs (`d3d11.dll`, `dxgi.dll`, `d3d10core.dll`,
`dxgi_dxmt.dll`, `winemetal.dll`) placed at the **root** rather than next to the real exe in
`x86/` — a direct symptom of the (32) exe-resolution gap (DLLs deploy to `game_dir` root when
`exe_path` is `None`).

### Current Hades rule (`configs/mtsp-rules.toml`)
```toml
[overrides.1145360]
pipeline = "m11"
name = "Hades"
[overrides.1145360.diagnostics]
check_dlls = ["d3d11.dll", "dxgi.dll", "winemetal.dll"]
```
No `exe_names`; `m11` (64-bit). Hades' canonical Windows build is 32-bit D3D11.

### Exe scorer (`app/src-rust/src/mtsp/recipe.rs::score_exe_candidate`)
For the three `Hades.exe` candidates (no `exe_names`, no `preferred_exe_names` entry for 1145360):
- `x64Vk/Hades.exe`: `x64`(+15) + `vk`(+10) + 64-bit(+10) + D3D API(+15) + name-token "hades"(+20) = **70**
- `x64/Hades.exe`:  `x64`(+15) + 64-bit(+10) + D3D API(+15) + name-token(+20) = **60**
- `x86/Hades.exe`:  D3D API(+15) + name-token(+20) = **35**

→ Vulkan build wins under an M11/D3D11 route. Wrong.

### (32) pipelines missing from `direct_wine_pipeline` (`recipe.rs`)
`build_launch_recipe` (lines ~58 and ~91), `build_custom_launch_recipe` (line ~251), and
`diagnose_recipe` (line ~454) all use a match of the form:
```rust
PipelineId::Dxmt | M9 | M10 | M11 | M12 | M13 | M32 | FnaArm64 | WineBare
```
`M11_32` and `M10_32` are **absent**. Consequences for an M11(32)/M10(32) bottle:
- `game_dir` is resolved via `resolve_game_dir` (macOS-or-Windows) instead of
  `resolve_windows_game_dir` (Windows-only) — wrong for a Windows DXMT title.
- `exe_path` is forced to `None` — `resolve_game_exe_for_pipeline` is never called.
- `selected_deploy_dlls_for_pipeline` receives `exe_path = None`, so `deploy_target_dirs_for_pipeline`
  falls back to `game_dir` root → DLLs land in the game root, not next to the 32-bit exe.
- `diagnose_recipe` reports the exe check as *"Not required for this pipeline"* and skips
  `inspect_exe_route_compatibility`, so the launch doctor lies about readiness.

Note `launcher.rs` already routes M11_32/M10_32 through `launch_dxmt_metal` /
`prepare_steam_pipeline_env` — only `recipe.rs` was not updated when the (32) pipelines landed
(PR #253 / commit c22c0b80).

### Bottle dropdown root cause (`bottles.rs::runtime_profile_for_pipeline`, line ~3429)
```rust
fn runtime_profile_for_pipeline(pipeline: PipelineId) -> RuntimeProfile {
    match pipeline {
        Dxmt => GameInstall, M9 => M9, M10 => M10, M11 => M11, M12 => M12,
        M13 => M13, D3DMetal => D3DMetal, FnaArm64 => FnaArm64,
        _ => RuntimeProfile::Plain,   // <-- M10_32 and M11_32 fall here
    }
}
```
`RuntimeProfile::Plain` has `components = &[]` (line ~3263). So `ensure_steam_game_bottle_inner`
(line ~1132) builds the bottle with `default_components_for(Plain)` = **empty**, and
`refresh_dxmt_runtime_before_save` returns early because the profile is `Plain`, not
`M11_32`/`M10_32`. Result: the bottle dropdown (`diagnose_bottle` → `component_actions`) has
zero components → no repair buttons, no OK badge, no deployed-DLL surface.

`refresh_dxmt_runtime_before_save` (line ~715) **already** handles `M11_32`/`M10_32` correctly,
and `runtime_profile_definition(M11_32)` (line ~3293) already declares `["d3d11","dxgi","vcrun2019_x86"]`.
The only break is the `runtime_profile_for_pipeline` mapping. Once that's fixed, the existing
inspect/repair machinery produces the surface automatically.

## Changes

### 1. `configs/mtsp-rules.toml` — Hades rule
Switch Hades to the 32-bit D3D11 route with an explicit exe name so the Vulkan build is never
selected and DLLs deploy next to the 32-bit binary.

```toml
[overrides.1145360]
pipeline = "m11_32"
name = "Hades"
exe_names = ["x86/Hades.exe"]

[overrides.1145360.diagnostics]
check_dlls = ["d3d11.dll", "dxgi.dll", "winemetal.dll"]
```
- `pipeline = "m11_32"` matches Hades' canonical 32-bit D3D11 build and exercises the new route.
- `exe_names = ["x86/Hades.exe"]` is resolved case-insensitively by `find_case_insensitive`
  (max_depth 5), so it picks `/.../Hades/x86/Hades.exe` regardless of the scorer.
- Keep diagnostics as-is (d3d11/dxgi/winemetal are the M11(32) deploy set).

### 2. `app/src-rust/src/mtsp/recipe.rs` — wire (32) pipelines into exe resolution
Add `PipelineId::M11_32 | PipelineId::M10_32` to the three `direct_wine_pipeline`-style match
arms:

- **`build_launch_recipe`** (two arms):
  - the `direct_wine_pipeline` bool (line ~58) — so `game_dir` uses `resolve_windows_game_dir`;
  - the `exe_path` resolution match (line ~91) — so `resolve_game_exe_for_pipeline(appid, dir, Some(node.id))` runs.
- **`build_custom_launch_recipe`** `exe_path` match (line ~251) — same treatment.
- **`diagnose_recipe`** `direct_wine_pipeline` bool (line ~454) — so the exe check and
  `inspect_exe_route_compatibility` run for (32) routes.

After this, an M11(32) bottle resolves `x86/Hades.exe`, deploys the i386 DXMT DLLs into
`x86/` next to it, and the launch doctor reports exe + route compatibility honestly.

### 3. `app/src-rust/src/bottles.rs` — map (32) pipelines to their runtime profiles
Extend `runtime_profile_for_pipeline` (line ~3429):
```rust
crate::mtsp::engine::PipelineId::M10_32 => RuntimeProfile::M10_32,
crate::mtsp::engine::PipelineId::M11_32 => RuntimeProfile::M11_32,
```
This makes auto-synced Steam bottles for M11(32)/M10(32) games pick up the
`["d3d11","dxgi","vcrun2019_x86"]` (M11_32) / `["d3d10","d3d10_1","dxgi","vcrun2019_x86"]` (M10_32)
component sets, which flow through `refresh_dxmt_runtime_before_save` → `inspect_components_for_manifest`
→ `component_actions` to produce the repair buttons and OK state in the bottle dropdown.

### 4. Tests
- `mtsp/rules.rs`: extend `shipped_rules_cover_researched_installed_titles` (or add a sibling
  test) to assert `rules.get(&1145360) == Some(&PipelineId::M11_32)` and that the Hades recipe
  carries `exe_names == ["x86/Hades.exe"]`.
- `mtsp/recipe.rs`: add a test `m11_32_pipeline_resolves_exe_and_targets_exe_dir` that, given a
  fake game dir with `x86/Hades.exe` and `x64Vk/Hades.exe`, asserts
  `build_launch_recipe` for `M11_32` selects `x86/Hades.exe` and that every
  `recipe.dlls[].dest_path` is rooted under `x86/` (not the game root). Mirror it for `M10_32`.
  Also assert `diagnose_recipe` reports the exe check (not "Not required for this pipeline").
- `bottles.rs`: add a test `m11_32_pipeline_maps_to_m11_32_runtime_profile` asserting
  `runtime_profile_for_pipeline(PipelineId::M11_32) == RuntimeProfile::M11_32` (and same for
  `M10_32`), and that `default_components_for` for those profiles contains `d3d11`/`dxgi`
  (M11_32) and `d3d10`/`dxgi` (M10_32).

## Build / verify

```
cd app/src-rust && cargo test --package metalsharp-backend
```
Then, with the installed Hades on the AverySSD Steam library, exercise the end-to-end path via
the backend:
- `POST /bottles/sync-steam` → confirm the Hades bottle's `runtime_profile` is `m11_32` and
  `installed_components` includes `d3d11`, `dxgi`, `vcrun2019_x86`.
- `POST /bottles/get` `{id:"steam_1145360"}` → `POST /bottles/doctor` → confirm the dropdown
  shows repair buttons / OK state and a non-zero `deployed_dlls` preflight.
- `POST /mtsp/prepare` `{appid:1145360}` → confirm `recipe.exe_path` ends in
  `Hades/x86/Hades.exe` and `recipe.dlls` target `Hades/x86/`.

## Risk / scope notes
- Changing Hades from `m11` to `m11_32` changes its route for all users. This is correct: Hades
  is a 32-bit D3D11 title and the 64-bit `m11` route was picking the Vulkan exe. If we want to
  preserve a 64-bit option, the `exe_names` override alone (on `m11`) would fix the exe pick,
  but the user's intent is to route Hades through the new 32-bit path.
- No DB / migration changes; `mtsp-rules.toml` is read at runtime via `OnceLock` and the
  shipped copy is already first in `rule_candidates`.
- The recipe/bottle changes are additive match-arm extensions plus two mapping arms; no
  existing 64-bit or M9/M12 behavior changes.
