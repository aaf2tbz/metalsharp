# MetalSharp CrossOver Parity Roadmap

Status: **In Progress** — PR #114 (codex/crossover-parity)

## Completed Phases

| Phase | Description | Status |
|-------|-------------|--------|
| 1A | WINEDLLPATH routing for DXMT M9-M12 | ✅ Done |
| 1B | M13 GPTK launch routing | ✅ Done |
| 1B.5 | Bundle Apple GPTK (D3DMetal) | ✅ Done |
| 2A | VC++ 2015-2022 expanded detection | ✅ Done |
| 2B | VC++ 2013 redistributable | ✅ Done |
| 2C | DirectX Jun 2010 full verification | ✅ Done |
| 2D | GPU vendor stubs (nvapi64, nvngx, atidxx64) | ✅ Done |
| 3A | Font substitution registry | ✅ Done |
| 3B | Declarative bottle templates (via RuntimeProfile) | ✅ Done |
| 3C | Drive mappings (y: -> user home) | ✅ Done |
| 4A | Anti-cheat without game-dir mutation (already clean) | ✅ Done |
| 4B | Explicit offline mode (via extra_env passthrough) | ✅ Done |
| 4C | EOS SDK env-based offline | ✅ Research — no env var exists |
| 5A | Post-wineboot config seeding | ✅ Done |
| 5B | Per-game TOML recipe expansion | ✅ Done |
| 5C | CrossTie-inspired install recipes | ✅ Done |

## Remaining (Future Work)

| Phase | Description | Status |
|-------|-------------|--------|
| 1C | mscompatdb.so v2 | Deferred — long-term architecture |

Based on a deep technical comparison between CrossOver 26.1.0 and MetalSharp v0.33.34
(current main at 72827cc), this roadmap tracks the work needed to reach and exceed
CrossOver's runtime reliability for Windows game compatibility on macOS.

## Scope

The roadmap covers five domains:

1. **DLL routing** -- replace game-dir DLL drops with Wine-level routing
2. **Redistributables** -- bundle and install full VC++ and DirectX runtimes
3. **Font and prefix configuration** -- proper font substitution, registry seeding, drive mappings
4. **Anti-cheat / offline mode** -- clean EOS handling without game-dir mutation
5. **Bottle template system** -- declarative, atomic prefix setup

Each domain is phased so that foundation work lands before dependent features.

---

## Domain 1: DLL Routing Architecture

### Current state

MetalSharp routes graphics DLLs by physically copying them into the game directory
(`deploy_recipe_dlls()` in `launcher.rs:257-311`). The pipeline node declares which DLLs
to deploy (`engine.rs` deploy_dlls), and at recipe-build time `recipe.rs:238-262` resolves
source paths and copies them next to the game exe. Wine picks them up because
`WINEDLLOVERRIDES` sets them `native,builtin`.

mscompatdb.so was removed in v0.16.0. No Wine-level DLL interception layer exists.
`MS_GRAPHICS_BACKEND` does not exist. `CX_ROOT` is set as an alias for `MS_ROOT`
(`steam.rs:663`) but is never consumed by any interception layer.

### CrossOver's approach

CrossOver routes DLLs through `cxcompatdb.so`, which reads `CX_GRAPHICS_BACKEND` from
the bottle config and prepends the appropriate DLL directory to `WINEDLLPATH`. No DLLs
are placed in game directories. The prefix's system32 DLLs are Wine builtins; the
graphics backend DLLs live in separate directories (`lib64/apple_gptk/wine/`,
`lib/dxmt/`, `lib/dxvk/`) and are selected at load time.

### Phase 1A: WINEDLLPATH routing for DXMT pipelines (M9-M12)

**Goal**: Stop copying d3d12.dll, d3d11.dll, dxgi.dll, d3d10core.dll, winemetal.dll
into game directories. Route through WINEDLLPATH instead.

**Changes**:

- `engine.rs`: Each DXMT pipeline node (M9, M10, M11, M12) already declares `dyld_paths`
  for `lib/dxmt/x86_64-unix`. Add a new field `winedllpath_dirs` that lists the PE DLL
  directories to prepend to WINEDLLPATH. For M12: `lib/dxmt/x86_64-windows`. For M9:
  `lib/wine/x86_64-windows` or `lib/wine/i386-windows` depending on PE arch.

- `launcher.rs`: In `launch_dxmt_metal_with_context()` and related launch functions,
  set `WINEDLLPATH` to the pipeline's `winedllpath_dirs` joined with colons, prepended
  before Wine's default DLL search path. Remove the `deploy_recipe_dlls()` call for
  DXMT pipelines.

- `recipe.rs`: When building recipes for M9-M12 pipelines, skip `deploy_dlls` resolution.
  The `selected_deploy_dlls_for_pipeline()` function should return an empty vec for
  pipelines that use WINEDLLPATH routing.

- `wine_overrides`: Keep `d3d12,dxgi,d3d11,d3d10core=n,b` so Wine prefers the WINEDLLPATH
  DLLs over its builtins. The `=n,b` still works -- Wine searches WINEDLLPATH directories
  before the prefix's system32.

- **Migration**: On next launch, if a game directory contains `.metalsharp/injections.json`,
  restore the original DLLs from `.metalsharp/originals/` and remove both directories.
  This cleans up legacy game-dir drops.

**Verification**:
- `cargo test` passes
- Launch a known M12 game (e.g., RE4) without any DLLs in the game directory
- Verify `WINEDLLPATH` contains the DXMT DLL dir in the Wine process environment
- Verify `.metalsharp/originals/` cleanup restores original game DLLs

**Files**: `engine.rs`, `launcher.rs`, `recipe.rs`

### Phase 1B: WINEDLLPATH routing for GPTK pipeline (M13)

**Goal**: M13 already uses WINEDLLPATH (`launcher.rs:582-588`). Verify it works without
game-dir drops.

**Changes**:

- `engine.rs` M13 node: Remove `deploy_dlls: ["d3d12", "dxgi"]`. M13 already sets
  WINEDLLPATH to GPTK's x86_64-windows directory.

- `launcher.rs`: Ensure `ensure_gptk_unix_links()` creates the correct `.so` symlinks
  in `lib/gptk/x86_64-unix/`.

- Verify EOSSDK override (`EOSSDK-Win64-Shipping=n,b`) works without game-dir drops.
  The EOS SDK lives in the game directory natively, so Wine will find it there.

**Verification**:
- Launch Elden Ring through M13 without any MetalSharp DLLs in the game directory
- Verify D3DMetal.framework is loaded (check Wine debug output)

**Files**: `engine.rs`, `launcher.rs`

### Phase 1B.5: Bundle Apple GPTK (D3DMetal) into MetalSharp runtime

**Goal**: Eliminate the hard dependency on `/Applications/Game Porting Toolkit.app/`.
Bundle D3DMetal.framework + GPTK PE wrappers into MetalSharp's runtime so M13
works out of the box with zero external setup.

**Why**: CrossOver bundles the entire Apple GPTK stack inside its app bundle at
`lib64/apple_gptk/`. This means CrossOver users never need to install Apple's
GPTK separately. MetalSharp currently requires users to download and install
Apple's Game Porting Toolkit DMG manually before M13 (GPTK) games work at all.
This is the single biggest UX gap for D3D12/anticheat game support.

**CrossOver's bundled GPTK layout** (for reference):
```
lib64/apple_gptk/
  external/
    D3DMetal.framework/
      Versions/A/D3DMetal           (5.3 MB x86_64, D3DMetal v3.0)
      Versions/A/Resources/
        default.metallib            (369 KB, precompiled Metal shaders)
        libdxcompiler.dylib         (28 MB, DX shader compiler)
        libdxilconv.dylib           (2 MB, DXIL converter)
        libmetalirconverter.dylib   (29 MB, Metal IR converter)
        libdxccontainer.dylib       (102 KB, DXC container)
    libd3dshared.dylib              (shared helper)
  wine/
    x86_64-windows/
      d3d12.dll                     (120 KB PE wrapper)
      d3d11.dll                     (112 KB PE wrapper)
      dxgi.dll                      (92 KB PE wrapper)
      nvapi64.dll                   (72 KB GPU stub)
      nvngx.dll                     (88 KB GPU stub)
      atidxx64.dll                  (112 KB GPU stub)
    x86_64-unix/                    (.so symlinks to .dll)
```

**Changes**:

- `installer.rs`: New install step `install_gptk_bundle()` that:
  1. Checks if GPTK is already present at `~/.metalsharp/runtime/wine/lib/gptk/`
  2. If not, downloads `gptk-bundle.tar.zst` from
     `https://github.com/aaf2tbz/metalsharp/releases/download/bundles/gptk-bundle.tar.zst`
  3. Extracts to `~/.metalsharp/runtime/wine/lib/gptk/` with this layout:
     ```
     lib/gptk/
       external/
         D3DMetal.framework/Versions/A/D3DMetal
         D3DMetal.framework/Versions/A/Resources/default.metallib
         D3DMetal.framework/Versions/A/Resources/libdxcompiler.dylib
         D3DMetal.framework/Versions/A/Resources/libdxilconv.dylib
         D3DMetal.framework/Versions/A/Resources/libmetalirconverter.dylib
         D3DMetal.framework/Versions/A/Resources/libdxccontainer.dylib
         libd3dshared.dylib
       x86_64-windows/
         d3d12.dll
         dxgi.dll
         nvapi64.dll
         nvngx.dll
         atidxx64.dll
       x86_64-unix/
         d3d12.so -> ../external/libd3dshared.dylib
         dxgi.so -> ../external/libd3dshared.dylib
     ```
  4. Verifies D3DMetal.framework is loadable via `dlopen` check
  5. Records install state in `~/.metalsharp/setup.json`

- `installer.rs` macOS install steps: Add `GPTKBundle` step between
  `DXMTRuntime` and `GoldbergSteamEmulator` in the install sequence.

- `launcher.rs`: Update M13 launch to prefer the bundled GPTK at
  `~/.metalsharp/runtime/wine/lib/gptk/` over `/Applications/Game Porting Toolkit.app/`.
  Keep the external path as a fallback for users who already have it installed.

  Current M13 wine64 path:
  ```rust
  // launcher.rs:552-591 -- currently hardcodes external GPTK path
  let gptk_wine = "/Applications/Game Porting Toolkit.app/Contents/Resources/wine/bin/wine64";
  ```
  New logic:
  ```rust
  let gptk_wine = if bundled_gptk_exists() {
      format!("{}/lib/gptk/wine/bin/wine64", ms_root)
  } else {
      "/Applications/Game Porting Toolkit.app/Contents/Resources/wine/bin/wine64".into()
  };
  ```

  Note: M13 also needs GPTK's own wine64 binary. The bundle should include it,
  OR MetalSharp should use its own Wine binary with WINEDLLPATH pointing to
  the bundled GPTK DLLs (which is the cleaner approach and what Phase 1B enables).

- `steam.rs`: Update GPTK Steam install to use bundled GPTK paths instead of
  external app path.

- `bottles.rs`: Add `gptk_bundle` as a detectable component. Runtime doctor
  should report if the bundled GPTK is present and its D3DMetal version.

- `migrate.rs`: Add migration entry for GPTK bundle installation state.

**Bundle creation** (build-time):
- New script `scripts/build-gptk-bundle.sh` that:
  1. Locates `/Applications/Game Porting Toolkit.app/` on the build machine
  2. Extracts D3DMetal.framework, libd3dshared.dylib, and PE wrapper DLLs
  3. Packages as `gptk-bundle.tar.zst`
  4. Uploads to GitHub releases as a bundle asset

- OR: Download Apple's GPTK DMG at build time and extract programmatically
  (this is what CrossOver does in their build pipeline).

**Fallback behavior**:
- If the bundled GPTK is not present AND external GPTK is not installed,
  the runtime doctor should report `gptk_missing` with instructions to either:
  1. Run `POST /setup/install-all` to download the bundle
  2. Or install Apple's GPTK manually

**Size budget**: D3DMetal.framework (~5.3 MB) + dylibs (~60 MB) + PE wrappers (~0.5 MB)
= ~66 MB compressed. This is significant but necessary for zero-setup D3D12 support.

**Verification**:
- `POST /setup/install-all` installs GPTK bundle
- M13 launches Elden Ring using only the bundled GPTK (no external GPTK installed)
- Runtime doctor reports `gptk_bundle: installed` with D3DMetal version
- Fallback to external GPTK still works if bundled is missing

**Files**: `installer.rs`, `launcher.rs`, `steam.rs`, `bottles.rs`, `migrate.rs`,
new `scripts/build-gptk-bundle.sh`

### Phase 1C: mscompatdb.so v2 -- Wine ntdll interception layer

**Goal**: Build a proper Wine ntdll interception layer (like CrossOver's cxcompatdb.so)
that routes DLL loading at the Wine loader level. This is the long-term architecture
that enables per-game DLL routing without environment variable tricks.

**Design** (clean-room, no CrossOver code):

1. Build as `mscompatdb.so` -- a Wine-loadable DLL that hooks into the ntdll DLL
   loading path.

2. Read a rules file at `share/metalsharp/mscompatdb-rules.json` that maps:
   - Process name patterns (e.g., `eldenring.exe`)
   - DLL name patterns (e.g., `d3d12.dll`)
   - To source DLL paths (e.g., `lib/dxmt/x86_64-windows/d3d12.dll`)

3. On DLL load, intercept `LdrLoadDll` and redirect matching DLLs to the configured
   source path. Non-matching DLLs pass through to Wine's normal loader.

4. The rules file is generated from `mtsp-rules.toml` at install time by the Rust
   backend, so the C++ layer only needs to read and apply rules, not resolve pipelines.

5. Environment variable `MS_GRAPHICS_BACKEND` selects the active graphics backend
   globally (like CrossOver's `CX_GRAPHICS_BACKEND`). This overrides per-game rules
   when set.

**Phasing**: This is Phase 1C because WINEDLLPATH routing (1A/1B) is sufficient for
the immediate term. mscompatdb.so v2 is the investment for per-game DLL injection,
mod support, and future features like runtime hot-swapping.

**Files**: New `src/wine/mscompatdb.cpp`, `configs/mscompatdb-rules.schema.json`,
`app/src-rust/src/mscompatdb.rs`

---

## Domain 2: Redistributables

### Current state

MetalSharp installs VC++ runtimes from Steam's `_CommonRedist` directory or from
`~/.metalsharp/runtime/redist/`. Only `vcrun2019` is a defined component. Detection
checks for `vcruntime140.dll` in system32/syswow64 but does not install the full
v14.x family (vcruntime140_1, msvcp140 variants, concrt140, MFC140).

DirectX Jun 2010 is a defined component but also relies on Steam's CommonRedist.
It installs DXSETUP.exe silently but does not verify all d3dx9/d3dx10/d3dx11 DLLs
after installation.

No GPU vendor stubs exist (nvapi64, nvngx, atidxx64).

### CrossOver's approach

CrossOver installs four VC++ MSI packages (2013 x86+x64, 2022 x86+x64) giving
complete coverage from v12.0 through v14.44. All d3dx9 (20 versions), d3dx10 (11),
d3dx11 (2), XAudio2 (10), XACT (19), XAPOFX (6), and X3DAudio (8) are present in
both x86 and x64. GPU vendor stubs (nvapi64 1KB, nvngx 1KB, atidxx64 1KB) are
present as tiny placeholder DLLs.

### Phase 2A: Full VC++ 2015-2022 redistributable

**Goal**: Bundle and install the complete VC++ 2015-2022 redistributable (v14.44)
for both x86 and x64.

**Changes**:

- `bottles.rs`: Rename `vcrun2019` component to `vcrun1422` (or add as new component
  alongside the existing one for backward compat). The new component installs:

  **x64** (to system32):
  - vcruntime140.dll
  - vcruntime140_1.dll
  - vcruntime140_threads.dll
  - msvcp140.dll
  - msvcp140_1.dll
  - msvcp140_2.dll
  - msvcp140_atomic_wait.dll
  - msvcp140_codecvt_ids.dll
  - concrt140.dll
  - vcomp140.dll

  **x86** (to syswow64):
  - Same set as x64 (including vcruntime140_1.dll -- CrossOver misses this)

  **MFC** (both architectures):
  - mfc140.dll, mfc140u.dll, mfcm140.dll, mfcm140u.dll
  - mfc140chs/cht/deu/enu/esn/fra/ita/jpn/kor/rus.dll

- `installer.rs`: Add download for `VC_redist.x64.exe` and `VC_redist.x86.exe`
  from `https://github.com/aaf2tbz/metalsharp/releases/download/bundles/vc_redist_2022.tar.zst`
  or bundle them in the DMG.

- `bottles.rs` detection: Check for `vcruntime140_1.dll` in addition to
  `vcruntime140.dll`. The full family should be verified.

- `runtime_profile_definition()`: Update M12, M11, M10, M9, GameInstall, Launcher,
  Dotnet, Win32Dotnet, Webview profiles to use the new component.

**Files**: `bottles.rs`, `installer.rs`

### Phase 2B: VC++ 2013 redistributable

**Goal**: Install VC++ 2013 (v12.0) for games that need msvcp120/msvcr120.

**Changes**:

- `bottles.rs`: Add `vcrun2013` component. Installs msvcp120.dll, msvcr120.dll for
  both x86 and x64.

- `bottles.rs` detection: Check for `msvcp120.dll` in system32/syswow64.

- Source: Bundle `vcredist_2013_x86.exe` and `vcredist_2013_x64.exe` in
  `~/.metalsharp/runtime/redist/` or download from bundles release.

**Files**: `bottles.rs`, `installer.rs`

### Phase 2C: DirectX June 2010 full verification

**Goal**: After DXSETUP.exe runs, verify all expected DLLs are present.

**Changes**:

- `bottles.rs` `directx_jun2010` component: Expand detection to check for a broader
  set of DLLs: `d3dx9_43.dll`, `d3dx10_43.dll`, `d3dx11_43.dll`, `xinput1_3.dll`,
  `xaudio2_7.dll`, `x3daudio1_7.dll`.

- After install, run a verification pass that lists all d3dx9/d3dx10/d3dx11/D3DCompiler
  versions present and logs any gaps.

- Bundle DXSETUP.exe + all CAB files in `~/.metalsharp/runtime/redist/DirectX/Jun2010/`
  so it doesn't depend on Steam CommonRedist.

**Files**: `bottles.rs`, `installer.rs`

### Phase 2D: GPU vendor stubs

**Goal**: Ship tiny placeholder DLLs for NVIDIA and AMD APIs so games that check for
these at startup don't crash.

**Changes**:

- Create `src/stubs/nvapi64.dll`, `src/stubs/nvngx.dll`, `src/stubs/atidxx64.dll`
  as minimal PE DLLs that export the expected symbols but return success/no-op.

- Alternatively, use CrossOver's approach: ship 1KB stubs that are just enough to
    satisfy `LoadLibrary` and `GetProcAddress`.

- `engine.rs`: Add a `deploy_stubs` field to pipeline nodes. M12 and M13 should
  deploy these stubs to the prefix's system32.

- `bottles.rs`: Add a `gpu_vendor_stubs` component that deploys these as part of
  the GameInstall and M12+ profiles.

**Files**: New stub source files, `engine.rs`, `bottles.rs`

---

## Domain 3: Font and Prefix Configuration

### Current state

MetalSharp copies 9 core fonts (Arial, Arial Bold, Courier New, Georgia, Impact,
Times New Roman, Trebuchet MS, Verdana, Webdings) from macOS system font directories
to `{prefix}/drive_c/windows/Fonts/`. No font substitution registry entries exist.
No CJK font mapping. No Adobe Source Han, no Arial Black.

Prefix creation runs `wineboot --init` and sets `DYLD_FALLBACK_LIBRARY_PATH`. No
post-creation registry seeding, no drive mapping customization.

### CrossOver's approach

CrossOver doesn't install fonts to the prefix's Fonts directory. It maps ~400+ macOS
system fonts through Wine's `[Software\Wine\Fonts\External Fonts]` registry mechanism.
CJK fonts are mapped (SimSun -> STSong, MS Gothic -> Hiragino Kaku Gothic). 69 Wine
bundled fonts are available. The prefix has a `y:` drive mapped to `$HOME`.

### Phase 3A: Wine font substitution registry

**Goal**: Replace font file copying with Wine registry font substitution. This is
more reliable because it uses macOS's font rendering and covers all installed fonts.

**Changes**:

- `bottles.rs`: Replace `install_host_core_fonts()` (line 2051-2104) with
  `seed_font_substitution()`. This function writes registry entries to the prefix's
  `user.reg` under `[Software\Wine\Fonts\External Fonts]` mapping macOS system font
  paths (via `Z:\System\Library\Fonts\...`).

- Font map should include:
  - Core: Arial, Arial Bold, Arial Black, Courier New, Georgia, Impact, Times New Roman,
    Trebuchet MS, Verdana, Webdings, Tahoma, Marlett, Symbol
  - CJK: STSong (for SimSun), Hiragino Kaku Gothic (for MS Gothic),
    LiSong Pro (for MingLiU), NanumGothic (for Gulim), PingFang SC (for SimHei)
  - Adobe Source Han Sans / Source Han Serif if installed
  - Bitstream Vera Sans (bundled with Wine) as fallback for sans-serif

- Keep the existing `corefonts` component ID for backward compat, but change its
  implementation to registry-based.

**Files**: `bottles.rs`

### Phase 3B: Declarative bottle templates

**Goal**: Define bottle templates as TOML configs that atomically set up a Wine prefix
with all required configuration (Windows version, DLL overrides, font substitution,
drive mappings, registry entries).

**Design**:

```
configs/bottle-templates/
  win10_64.toml
  win10_64_dxmt.toml
  win10_64_gptk.toml
  win7_64.toml
```

Each template defines:
```toml
[template]
name = "win10_64"
windows_version = "win10"
arch = "win64"

[drives]
y = "$HOME"

[dll_overrides]
ole32 = "builtin"
oleaut32 = "builtin"
rpcrt4 = "builtin"
wininet = "builtin"
msi = "builtin"

[fonts."Arial"]
source = "macos:Arial"

[fonts."Arial Black"]
source = "macos:Arial-Black"

[fonts."Impact"]
source = "macos:Impact"

[registry."Software\Wine\Direct3D"]
"cb_access_map_w" = "dword:00000001"
```

- `bottles.rs`: New function `apply_bottle_template(prefix_path, template_name)` that
  reads the TOML and applies all configuration in one pass after `wineboot --init`.

- `runtime_profile_definition()`: Each profile references a bottle template by name.

**Files**: New `configs/bottle-templates/`, `bottles.rs`

### Phase 3C: Drive mappings and username

**Goal**: Match CrossOver's drive mapping and username isolation.

**Changes**:

- `launch.rs`: After prefix creation, add `y:` -> `$HOME` symlink in `dosdevices/`.

- Consider using a fixed username (like CrossOver's `crossover`) instead of the macOS
  username. This is a breaking change for existing prefixes, so it should be opt-in
  with a migration path.

**Files**: `launch.rs`

---

## Domain 4: Anti-Cheat / Offline Mode

### Current state

M13 pipeline sets `EOSSDK-Win64-Shipping=n,b` in wine_overrides, which tells Wine to
load the game's native EOS SDK. No offline mode stub exists. The EAC toggle system
deploys `_winhttp.dll` + config files into game directories.

The CrossOver comparison revealed that CrossOver does NOT patch EOS SDK files at all.
It just uses the GPTK backend with `WINED3DMETAL=1` and lets the game handle online/offline
status natively.

### Phase 4A: Clean M13 launch without game-dir mutation

**Goal**: Ensure M13 launches anticheat games without modifying any game files.

**Changes**:

- Remove any EOS SDK renaming/patching code. If `EOSSDK-Win64-Shipping.dll.metalsharp-original`
  exists in a game directory, restore the original.

- Remove the EAC toggle DLL drops (`_winhttp.dll`, `anti_cheat_toggler_config.ini`) from
  M13 launches. These should only be used in explicit offline mode (see Phase 4B).

- Verify M13 launches Elden Ring with only WINEDLLPATH routing and `WINEDLLOVERRIDES`.
  No game-dir file modifications.

**Files**: `launcher.rs`, `anticheat.rs`

### Phase 4B: Explicit offline mode

**Goal**: Add a user-facing "Launch Offline" option that safely enables offline play.

**Changes**:

- `recipe.rs`: New recipe field `offline_mode: bool`. When true:
  - Deploy EAC toggle assets (`_winhttp.dll` + config) to game directory
  - Set `SteamAppId` / `SteamGameId` env vars (required for Steam game identity)
  - Do NOT attempt Steam connection
  - Log offline mode activation

- `bottles.rs`: New endpoint `POST /bottles/launch-offline` that sets `offline_mode: true`
  before building the recipe.

- `launcher_evidence.rs`: Track offline mode in launch evidence for diagnostics.

**Files**: `recipe.rs`, `bottles.rs`, `launcher.rs`

### Phase 4C: EOS SDK environment-based offline (research)

**Goal**: Determine if EOS SDK supports environment-variable-based offline mode
(similar to how some games respect `EOS_OFFLINE=1` or similar).

**Changes**:

- Research EOS SDK documentation and behavior for offline flags
- If EOS supports an env var for offline mode, add it to M13's `env_vars` instead
  of deploying the EAC toggle

**Files**: `engine.rs` (M13 env_vars)

---

## Domain 5: Bottle Template System

### Current state

Bottle creation runs `wineboot --init` (`launch.rs:308-330`), then components are
installed individually via `repair_component()` calls. There is no atomic template
application. DLL overrides are set per-launch via `WINEDLLOVERRIDES` env var, not
persisted in the prefix's user.reg.

### Phase 5A: Post-wineboot configuration seeding

**Goal**: After `wineboot --init`, apply a standard set of configuration to every
new prefix.

**Changes**:

- `bottles.rs`: New function `seed_bottle_defaults(prefix_path)` that writes to
  `user.reg`:

  **DLL overrides** (from CrossOver's proven set):
  ```
  [Software\Wine\DllOverrides]
  "ole32" = "builtin"
  "oleaut32" = "builtin"
  "olepro32" = "builtin"
  "rpcrt4" = "builtin"
  "wininet" = "builtin"
  "msi" = "builtin"
  "atl" = "native,builtin"
  "mshtml" = "native,builtin"
  "riched20" = "native,builtin"
  "quartz" = "native,builtin"
  ```

  **Wine settings**:
  ```
  [Software\Wine\Direct3D]
  "cb_access_map_w" = "dword:00000001"

  [Software\Wine\Systray]
  "Hidden" = "dword:00000001"
  ```

- `launch.rs`: Call `seed_bottle_defaults()` after `wineboot --init`.

**Files**: `bottles.rs`, `launch.rs`

### Phase 5B: Per-game recipe expansion in mtsp-rules.toml

**Goal**: Expand `mtsp-rules.toml` to support per-game dependency declarations,
diagnostic checks, and fix application.

**Changes**:

- Extend the TOML schema to support per-game entries like:
  ```toml
  [[game]]
  appid = 1245620
  pipeline = "m13"
  name = "ELDEN RING"

  [game.dependencies]
  components = ["vcrun1422", "directx_jun2010"]

  [game.env]
  WINED3DMETAL = "1"

  [game.diagnostics]
  check_dlls = ["d3d12.dll", "dxgi.dll"]
  ```

- `rules.rs`: Parse new fields. When resolving a game's pipeline, also resolve
  its dependencies and verify them against the bottle's component state.

- `bottles.rs`: When running `doctor` for a Steam bottle, check the game's
  declared dependencies against installed components.

**Files**: `configs/mtsp-rules.toml`, `rules.rs`, `bottles.rs`

### Phase 5C: CrossTie-inspired install recipe system

**Goal**: Support install-time dependency chains (like CrossOver's CrossTie
`predepend` field).

**Changes**:

- `installer.rs`: When installing a game through Steam, check if the game has
  declared dependencies in `mtsp-rules.toml`. Install missing dependencies
  before the game launches for the first time.

- This is the "install recipe" concept: a declaration of what needs to be
  installed before a game can run, applied automatically.

**Files**: `installer.rs`, `rules.rs`, `bottles.rs`

---

## Implementation Order

The phases are ordered by dependency and impact:

```
Phase 1A    WINEDLLPATH routing for DXMT        -- unblocks all DLL routing
Phase 1B    WINEDLLPATH routing for GPTK         -- fixes Elden Ring routing
Phase 1B.5  Bundle Apple GPTK into runtime       -- zero-setup D3D12, no external GPTK needed
Phase 2A    Full VC++ 2015-2022                  -- fixes Subnautica 2, many games
Phase 2D    GPU vendor stubs                     -- prevents startup crashes
Phase 4A    Clean M13 without game-dir mutation  -- clean anti-cheat launches
Phase 3A    Font substitution registry            -- proper font support
Phase 5A    Post-wineboot config seeding          -- stable prefix defaults
Phase 2B    VC++ 2013                             -- older game support
Phase 2C    DX Jun 2010 verification              -- d3dx completeness
Phase 3B    Declarative bottle templates           -- maintainable prefix setup
Phase 4B    Explicit offline mode                  -- user-facing offline play
Phase 5B    Per-game recipe expansion              -- scalable game support
Phase 3C    Drive mappings and username            -- CrossOver parity polish
Phase 5C    CrossTie install recipes               -- automated dependency install
Phase 1C    mscompatdb.so v2                      -- long-term routing architecture
Phase 4C    EOS env-based offline (research)      -- cleanest offline path
```

## Success Metrics

- Elden Ring launches in offline mode through M13 without any game-dir file modification
- Elden Ring works without `/Applications/Game Porting Toolkit.app/` installed (uses bundled GPTK)
- Subnautica 2 launches through M12 with full VC++ 2022 x64 installed
- No DLLs are placed in game directories for any DXMT pipeline (M9-M12)
- All d3dx9 (24-43) and d3dx10 (33-43) DLLs present in GameInstall bottles
- Font rendering matches CrossOver (Arial Black, Impact, CJK fonts functional)
- `cargo test` + `cargo fmt --check` + `npm run build` all pass
- 90+ games in mtsp-rules.toml with per-game dependency declarations

## Boundaries

- No CrossOver code or proprietary technology is used. mscompatdb.so v2 is clean-room.
- Anti-cheat support does not include bypassing, spoofing, or tampering with protected
  modules (per `anticheat-hard-route-roadmap.md` rejected paths).
- Per-game Steam prefixes remain a hard boundary until Steam bootstrap/install
  migration is proven safe (per existing roadmap).
- Linux container/substrate for Linux-user-space anti-cheat modules is a separate
  project outside this roadmap's scope.
