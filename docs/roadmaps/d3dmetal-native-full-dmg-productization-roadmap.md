# D3DMetal Native Full DMG Productization Roadmap

Status: active execution roadmap
Date: 2026-07-03
Branch: `feat/d3dmetal-native-pipeline`

## Objective

Build and ship a **complete MetalSharp DMG** that contains a fresh full MetalSharp Wine 11.5 runtime with the same architecture coverage expected by the existing app runtime, plus D3DMetal Native as an additive x86_64/win64 backend.

This roadmap supersedes any shortcut that would package the temporary x86_64-only proof runtime as the final app runtime.

## Non-negotiable constraints

- The final DMG runtime must be a **full MetalSharp Wine 11.5 runtime**, not a D3DMetal-only proof tree.
- Preserve the original runtime architecture surface, including existing `i386-windows` / WoW64-era support expected by bottles, DXVK, WineBare, Steam, and migration flows.
- D3DMetal Native remains **x86_64/win64-only** inside that full runtime until separate compatibility work proves otherwise.
- Do not replace M12 automatically.
- Do not make D3DMetal the default route.
- Do not remove DXMT, DXVK, VKD3D, WineBare, Steam, bottle, or migration flows.
- Do not launch Steam or games until a full DMG has been built and verified.
- The PR branch must contain all code/tooling/manifest changes before building the final distributable DMG.
- The updated bundle artifacts must be uploaded to the main 2.0 bundle/release repo before final DMG build, so the DMG cannot be assembled from stale local-only bundles.
- CrossOver/GPTK are behavior oracles only. No CrossOver/CX runtime identity or dependency is allowed.

## Current state checkpoint

Already proven from the staged x86_64 runtime:

- MetalSharp Wine 11.5 x86_64/win64 runtime builds and runs.
- D3DMetal payload stages under `runtime/wine/lib/d3dmetal_native`.
- `MS_D3DMETAL_PAYLOAD_DIR` loader-priority patch makes D3D11 route through D3DMetal instead of stock Wine D3D11.
- No-game packaged-runtime proof:
  - `wineboot`: pass
  - `CreateDXGIFactory2`: pass
  - `D3D12CreateDevice(NULL)`: pass
  - `D3D11CreateDevice(NULL)`: pass
  - `D3D10CreateDevice` / `D3D10CreateDevice1`: still `E_FAIL`, tracked as extended compatibility.

Not yet acceptable for final DMG:

- The proof runtime was built x86_64/win64-only and does not reproduce the original full packaged runtime architecture surface.
- A full DMG must not be built from that x86_64-only tree as a blind replacement.
- Any current WIP changes to runtime bundle repair/build scripts must be reviewed against this roadmap before being committed.

## Target final runtime layout

The runtime bundle and installed DMG must contain:

```text
runtime/
  metalsharp-backend
  host/
    manifest.json
    HostRuntimeABI.h
    libmetalsharp_host_runtime.dylib
  wine/
    bin/
      wine
      wineserver
      metalsharp-wine
      wineboot
      ...
    lib/
      wine/
        i386-windows/          # preserved full-runtime surface
        x86_64-windows/        # patched Wine 11.5 PE modules
        x86_64-unix/           # patched Wine 11.5 Unix modules
      metalsharp/
      dxmt/
      dxmt_m12/
      dxvk/
      vkd3d/
      d3dmetal_native/
        x86_64-windows/
          d3d10.dll
          d3d11.dll
          d3d12.dll
          dxgi.dll
          nvapi64.dll
          nvngx-on-metalfx.dll
          d3d10_1.dll          # optional compatibility member when payload carries it
          d3d10core.dll        # optional compatibility member when payload carries it
        x86_64-unix/
        external/
          libd3dshared.dylib
          D3DMetal.framework/
```

Important route rule:

```text
WINEDLLPATH=<runtime>/wine/lib/d3dmetal_native:<runtime>/wine/lib/wine
```

Do not use architecture subdirectories in `WINEDLLPATH`.

## Phase 0 — Stop and normalize worktree state

Goal: make sure no shortcut runtime-bundle changes slip into the PR.

Tasks:

- Inspect all current uncommitted changes.
- Revert or rewrite any tool changes that assume replacing the runtime with an x86_64-only Wine tree is acceptable.
- Keep unrelated untracked files untouched:
  - `GPTK4/`
  - `app/src-rust/src/bottles.rs.tmp`
  - `tools/wine-patches/d3dmetal-host-abi/BUILTIN-D3D12-PLAN.md`
- Ensure the roadmap itself is committed early so later implementation follows it.

Acceptance:

- Working tree changes are intentional and aligned with this roadmap.
- No final-DMG build starts until this phase is clean.

## Phase 1 — Define the full runtime architecture contract

Goal: precisely match what the app/backend expects from `runtime/wine`.

Tasks:

- Extract/list the current known-good `metalsharp-runtime.tar.zst` runtime tree.
- Record required architecture surfaces:
  - `lib/wine/i386-windows`
  - `lib/wine/x86_64-windows`
  - `lib/wine/x86_64-unix`
  - any `lib64` compatibility paths currently consumed
- Record required graphics/runtime surfaces:
  - `lib/dxmt`
  - `lib/dxmt_m12`
  - `lib/dxvk`
  - `lib/vkd3d`
  - `lib/metalsharp`
  - MoltenVK dylib + ICD JSON paths
- Align with Rust contracts in:
  - `app/src-rust/src/runtime_contracts.rs`
  - `app/src-rust/src/mtsp/engine.rs`
  - `app/src-rust/src/mtsp/launcher.rs`
  - `app/src-rust/src/installer.rs`
  - `app/src-rust/src/migrate.rs`
  - `app/src-rust/src/bottles.rs`
  - `app/src-rust/src/source_launch.rs`
  - `app/src-rust/src/steam.rs`

Acceptance:

- A documented tree contract exists for the final runtime bundle.
- The contract explicitly distinguishes full-runtime architecture support from D3DMetal's x86_64-only backend support.

## Phase 2 — Build a fresh full MetalSharp Wine 11.5 runtime

Goal: rebuild Wine 11.5 with the original full architecture surface plus D3DMetal patches.

Tasks:

- Start from a clean Wine 11.5 source tree.
- Apply committed MetalSharp Wine patch stack only.
- Configure/build a full runtime that reproduces the original packaged architecture surface, including `i386-windows` support.
- Do not use the x86_64-only proof build as the final runtime.
- Resolve build blockers rather than dropping architecture support.
- Capture immutable logs:
  - configure command
  - build log
  - install log
  - patch application log
- Keep D3DMetal host ABI/loader patches active on the x86_64 path.

Acceptance:

- Fresh full runtime builds from clean source.
- Installed runtime contains required i386 and x86_64 surfaces.
- `wine --version` reports Wine 11.5.
- `wineboot` passes in an isolated prefix.

## Phase 3 — Stage D3DMetal Native into the full runtime

Goal: add D3DMetal as an extra backend inside the full runtime.

Tasks:

- Stage payload under `runtime/wine/lib/d3dmetal_native`.
- Verify required files:
  - PE bridge DLLs
  - Unix/native sidecars
  - `libd3dshared.dylib`
  - `D3DMetal.framework`
- Preserve Apple/GPTK licensing rules in docs and bundle metadata.
- Ensure no CrossOver/CX identifiers appear in runtime naming, receipts, env contracts, or route IDs.

Acceptance:

- `tools/runtime/check-d3dmetal-native-payload.py --runtime-root <full-runtime>/wine --json` returns ready.
- D3DMetal payload is present in the final runtime tree, not just a contract sidecar.

## Phase 4 — Backend, bottle, installer, and migration alignment

Goal: make the app consume the full runtime correctly.

Backend / launcher:

- Keep `PipelineId::D3DMetalNative` experimental/additive.
- Keep defaults unchanged: existing routes continue to resolve as before.
- D3DMetal route env includes:
  - `MS_GRAPHICS_BACKEND=d3dmetal_native`
  - `MS_ACTIVE_GRAPHICS_BACKEND=d3dmetal_native`
  - `MS_D3DMETAL_PAYLOAD_DIR`
  - `MS_D3DMETAL_SHARED_PATH`
  - `MS_D3DMETAL_FRAMEWORK_PATH`
  - `D3DMETAL_FRAMEWORK_PATH`
  - `WINEDLLPATH=<payload-root>:<wine-root>` for paired runtime/unixlib lookup
  - app-local GPTK4 PE route DLL staging for `dxgi.dll`, `d3d11.dll`, and `d3d12.dll`
  - native-first D3DMetal `WINEDLLOVERRIDES` (`=n,b`) so external GPTK4 DLLs win over Wine builtins
- Receipts remain secret-safe and brand-clean.

Bottles:

- Existing bottle pipeline choices remain valid.
- D3DMetal Native appears only as an intentional extra route when readiness passes.
- Bottle repair/staging must not overwrite existing DXMT/DXVK/VKD3D route DLLs with D3DMetal DLLs.
- 32-bit/i386 bottle assumptions must continue to work through existing non-D3DMetal routes.

Installer:

- `MAC_RUNTIME_BUNDLE_ASSETS` includes the correct runtime and graphics bundles.
- Bundle validation rejects stale/corrupt runtime bundles.
- Runtime validation checks full architecture surfaces, not only x86_64 D3DMetal files.
- D3DMetal payload validation checks actual payload inside `metalsharp-runtime.tar.zst`, not only the contract bundle.

Migration wizard:

- `needs_migration()` and migration readiness must detect the new runtime bundle version/hash.
- Migration preserves:
  - bottles
  - bottle manifests
  - Steam install/config
  - user preferences
  - existing route selections
  - shader caches where applicable
- Migration must reinstall/refresh the runtime bundle from uploaded 2.0 assets, not stale local artifacts.
- Migration verification must confirm full runtime readiness before reporting complete.

Acceptance:

- Focused Rust tests cover route resolution, runtime bundle asset list, bundle validation, migration readiness, and D3DMetal readiness.
- No app path assumes D3DMetal replaces M12.

## Phase 5 — Build and verify updated bundles before DMG

Goal: produce real uploaded bundle assets before building the final DMG.

Tasks:

- Build `metalsharp-runtime.tar.zst` from the fresh full runtime.
- Include in that runtime bundle:
  - full Wine 11.5 runtime
  - D3DMetal Native payload
  - MetalSharp host runtime
  - MetalSharp backend
  - MetalSharp hook libraries
- Build/refresh any dependent bundles whose manifests changed:
  - `metalsharp-graphics-dll.tar.zst`
  - `metalsharp-assets.tar.zst`
  - `dxvk.tar.zst`
  - `vkd3d-proton.tar.zst`
  - `metalsharp-scripts-tools.tar.zst`
  - `metalsharp-steam.tar.zst`
  - `metalsharp-d3dmetal-native-contract.tar.zst` if still shipped as a binary-free contract/reference bundle
- Update bundle manifest/hash metadata.
- Upload updated bundle artifacts to the main 2.0 bundle/release repo.
- Verify a clean fetch pulls the uploaded artifacts, not local stale copies.

Acceptance:

- `tools/bundles/verify-bundles.sh --require mac` passes from downloaded/uploaded bundles.
- Extracted `metalsharp-runtime.tar.zst` contains both full runtime architecture support and D3DMetal payload.
- Extracted runtime bundle passes no-game D3DMetal probes.

## Phase 6 — Land PR before final DMG build

Goal: avoid building a final DMG from unreviewed local state.

Tasks:

- Commit roadmap, source changes, patch updates, bundle tooling, installer/migration/bottle alignment, and manifests to PR branch.
- Push PR branch.
- CI/local validation must include:
  - Rust tests for installer/migration/bottle/runtime contracts
  - bundle verifier
  - payload verifier
  - no-game D3DMetal probe logs from extracted runtime bundle
- Resolve review findings.

Acceptance:

- PR branch contains every code/tooling/manifest change required for the DMG.
- Updated bundles are available from the main 2.0 upload target.
- No final DMG build proceeds from uncommitted local-only code or stale local-only bundles.

## Phase 7 — Build the final DMG from uploaded bundles

Goal: produce the actual complete DMG.

Tasks:

- Clean local bundle cache or force re-fetch.
- Fetch uploaded 2.0 bundles.
- Build DMG from PR/landed branch state.
- Verify DMG contains:
  - full `metalsharp-runtime.tar.zst`
  - D3DMetal payload inside runtime bundle
  - updated backend
  - updated host runtime
  - updated graphics/runtime support bundles
- Mount DMG and run `tools/dmg/verify-dmg-runtime-assets.sh`.
- Extract runtime bundle from the mounted DMG and rerun no-game runtime proof.

Acceptance:

- DMG verifies structurally.
- DMG-contained runtime bundle, not local staging runtime, passes:
  - `wine --version`
  - full runtime tree contract
  - `wineboot`
  - D3DMetal payload verifier
  - DXGI factory probe
  - D3D12 device probe
  - D3D11 device probe
  - D3D10 expected-failure evidence

## Phase 8 — Native install flow after complete DMG only

Goal: test like a normal user only after the DMG is complete.

Tasks:

- Install MetalSharp from rebuilt DMG.
- Launch app.
- Let runtime setup initialize.
- Confirm installed runtime is the full rebuilt Wine 11.5 runtime.
- Confirm migration wizard behavior on an existing `~/.metalsharp` install.
- Confirm fresh install behavior on an isolated/new test home if possible.
- Confirm D3DMetal Native appears only as an extra route when readiness passes.

Acceptance:

- App opens.
- Runtime setup completes.
- Migration does not lose bottles, Steam config, preferences, or route selections.
- D3DMetal readiness is accurate.

## Phase 9 — Steam and game testing last

Goal: only after final DMG install passes, test user-facing runtime behavior.

Tasks:

- Install or launch Steam through the normal MetalSharp flow.
- Confirm Steam uses MetalSharp Wine 11.5, not GPTK/CrossOver Wine.
- Confirm known-working games still launch through their existing routes.
- Confirm DXMT/M12 and WineBare are no worse than before.
- Exercise D3DMetal only after baseline route regression passes.

Acceptance:

- Steam works under packaged MetalSharp Wine 11.5.
- Known-working games remain functional through existing routes.
- D3DMetal remains additive/experimental.
- No D3DMetal game-launch claim is made until baseline routes are proven.

## Final release gate

The DMG is considered complete only when all are true:

- Fresh full Wine 11.5 runtime includes expected i386 and x86_64 surfaces.
- D3DMetal Native payload is inside the runtime bundle.
- Updated bundles are uploaded to the main 2.0 repo/release target.
- PR branch contains and validates all required source/tooling/manifest changes.
- Final DMG is built from uploaded bundles, not stale local artifacts.
- DMG-contained runtime passes no-game D3DMetal proof.
- Native install/migration flow passes.
- Steam/game testing has not been used to justify skipping any prior gate.
