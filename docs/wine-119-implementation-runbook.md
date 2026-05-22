# Wine 11.9 Implementation Runbook

This is the branch execution plan derived from `docs/wine-119-rebuild-forensics.md`.
It is intentionally conservative: the first rebuild branch must prove Wine 11.9
as a layout-compatible runtime before anti-cheat, Steam handoff, WineMetal
relocation, native-only overrides, or Unity argument changes are allowed back in.

## Branch Ground Rules

- Start from current `main`, the rollback commit `cebf9362f47817580f5eceeb4000ef3987854b2e`.
- Do not merge or cherry-pick `backup/main-before-v0.33.28-reset-20260521T223249Z` as a branch.
- Do not cherry-pick merge/rollup commits:
  - `875004e`
  - `3dacbe7`
  - `797db72`
- Do not bump version or tag until all live gates pass.
- Do not change installed `~/.metalsharp/runtime/wine` during parity work.
- Use only `/tmp` or `/private/tmp` parity homes unless intentionally overriding with `METALSHARP_ALLOW_NON_TMP_PARITY_HOME=1`.

## Commit Policy

Allowed direct replay candidates are diagnostic only:

- `65032f7` Track direct game anti-cheat crash evidence
- `168d235` Add mscompatdb hook surface diagnostics
- `ce2c008` Format mscompatdb hook sources
- `1148f57` Tighten mscompatdb readiness checks
- `cfade4e` Read PE export and import directories separately
- `f654c10` Add D3D12 surface extraction diagnostics
- `1c0b788` Log direct MTSP runtime contracts
- `38fed0a` Format D3D12 feature checks
- `d269d74` Log D3D12 compute pipeline evidence
- `e796968` Log D3D12 compute dispatch evidence
- `1e54117` Log D3D12 draw execution evidence
- `ddba26f` Log D3D12 feature query decisions
- `e3c2a63` Track DXGI present count
- `557cb14` Route DXMT D3D12 traces per launch
- `05d7fd9` Harden DXMT patch preflight
- `a514813` Fix EAC Proton asset evidence detection
- `5e9a23c` Cover expanded DXGI factory surface
- `1683b79` Cover DXGI factory implementation surface

Before replaying any of these, inspect its patch. If it changes launch route
selection, Steam handoff, migration, runtime install source, Wine DLL override
semantics, or cache behavior, do not cherry-pick it. Rebuild only the diagnostic
piece manually.

Never cherry-pick as-is:

- `c95fd90` protected Steam handoff probes
- `6e235de` Wine 11.9 runtime hook promotion
- `14a5cb5` forced Wine 11.9 migration schema
- `ef3377b` mscompatdb mutation checks
- `10aeada` postinstall Steam handoff and migration restore
- `e2dff52` WineMetal runtime binding
- `c1dd812` native-only DirectX overrides
- `c01180f` DXMT launch cache contract
- `230c2ec` and `c95024d` M9 config narrowing

These commits are source material only. Rebuild their useful intent in smaller
patches after the relevant gate.

## Pass 0: Evidence Branch Base

Goal: add observability and candidate-prep tooling without changing runtime behavior.

Allowed files:

- `docs/wine-119-rebuild-forensics.md`
- `docs/wine-119-implementation-runbook.md`
- `scripts/runtime-manifest.sh`
- `scripts/compare-runtime-manifests.sh`
- `scripts/fetch-wine119-release-assets.sh`
- `scripts/prepare-wine119-candidate.sh`
- `scripts/prepare-wine119-parity-candidates.sh`
- `scripts/install-wine119-parity-home.sh`
- `scripts/probe-wine119-parity-backend.sh`
- `scripts/capture-steam-game-proof.sh`
- `scripts/run-wine119-live-control-suite.sh`
- `scripts/verify-wine119-live-control-suite.sh`
- `scripts/audit-wine119-readiness.sh`

Commands:

```bash
bash -n scripts/runtime-manifest.sh \
  scripts/compare-runtime-manifests.sh \
  scripts/fetch-wine119-release-assets.sh \
  scripts/prepare-wine119-candidate.sh \
  scripts/prepare-wine119-parity-candidates.sh \
  scripts/install-wine119-parity-home.sh \
  scripts/probe-wine119-parity-backend.sh \
  scripts/capture-steam-game-proof.sh \
  scripts/run-wine119-live-control-suite.sh \
  scripts/verify-wine119-live-control-suite.sh \
  scripts/audit-wine119-readiness.sh

(cd app/src-rust && cargo test launcher::tests::)
```

Expected result:

- Shell syntax passes.
- Launcher tests pass.
- No installed runtime files change.
- Optional verifier fixture proves the live-suite verifier accepts surviving Wine Steam PIDs and rejects disappeared pre-existing Wine Steam PIDs.

## Pass 1: Runtime Candidate Preparation

Goal: build disposable Wine 11.9 runtime candidates from the release asset while
preserving the 11.5 final runtime shape.

Inputs:

- Release asset: `bundles/metalsharp_bundle.tar.zst`
- Reproducible local asset path after fetch: `/tmp/metalsharp-wine-assets/metalsharp_bundle.tar.zst`
- Working baseline: `/Users/alexmondello/.metalsharp/runtime/wine`
- DXMT i386 candidate: `/Volumes/AverySSD/metalsharp/dxmt-src/build32/src/winemetal/winemetal.dll`

Commands:

```bash
scripts/fetch-wine119-release-assets.sh \
  /tmp/metalsharp-wine-assets \
  metalsharp_bundle.tar.zst

scripts/prepare-wine119-parity-candidates.sh \
  /tmp/metalsharp-wine-assets/metalsharp_bundle.tar.zst \
  /tmp/metalsharp-wine119-parity

scripts/audit-wine119-readiness.sh \
  /private/tmp/metalsharp-home-wine119-dxmt32-state \
  /tmp/metalsharp-wine119-readiness-current
```

Expected candidate meaning:

- fetch report must verify `metalsharp_bundle.tar.zst` against the GitHub release digest before candidate preparation
- `clean`: must fail because release asset lacks i386 `winemetal.dll`.
- `dxmt32`: primary test candidate, still release-blocked until live M9 proof.
- `borrowed`: manifest-complete fallback experiment, not release-ready without live proof.
- every prepared candidate must rewrite Vulkan ICD `library_path` entries to
  its own runtime root and provide `lib/libMoltenVK.dylib`, otherwise proof can
  accidentally borrow MoltenVK from the installed 11.5 runtime.

## Pass 2: Isolated Parity Home

Goal: run backend and proof capture against Wine 11.9 without mutating the working install.

Commands:

```bash
METALSHARP_CLONE_USER_STATE=1 \
METALSHARP_COPY_MODE=clone \
scripts/install-wine119-parity-home.sh \
  /tmp/metalsharp-wine119-parity/candidates/dxmt32/wine \
  /tmp/metalsharp-home-wine119-dxmt32-state

scripts/probe-wine119-parity-backend.sh \
  /private/tmp/metalsharp-home-wine119-dxmt32-state \
  /private/tmp/metalsharp-home-wine119-dxmt32-state/backend-probe-main03327-guard-v2

node scripts/audit-electron-launch-routes.mjs \
  --library-json /private/tmp/metalsharp-home-wine119-dxmt32-state/backend-probe-main03327-guard-v2/steam-library.json \
  --out-dir /private/tmp/metalsharp-home-wine119-dxmt32-state/backend-probe-main03327-guard-v2/electron-launch-routes

scripts/audit-wine119-readiness.sh \
  /private/tmp/metalsharp-home-wine119-dxmt32-state \
  /tmp/metalsharp-wine119-readiness-current
```

Required proof before live launch:

- `/tmp/metalsharp-wine-assets/fetch-report.txt` verifies the GitHub `bundles/metalsharp_bundle.tar.zst` SHA256.
- Wine reports `wine-11.9`.
- Backend reports version `0.33.27`.
- Active bottle/compatdata/config manifests have zero references to `/Users/alexmondello/.metalsharp`.
- Copied historical `compatdata/*/logs` files are absent; empty log directories
  created by backend scans do not contaminate proof.
- Nidhogg 2, Schedule I, and Subnautica BZ manifests point at the parity MetalSharp home.
- Electron route audit proves renderer `auto` and explicit M9/M11 selections call `/steam/launch-game` for the control games.
- Readiness audit has only the live-suite failure remaining.

## Pass 3: Live Parity Gate

Goal: prove Wine 11.9 can run normal games before anti-cheat behavior is added.

Command:

```bash
METALSHARP_RUN_LIVE_GAMES=1 \
METALSHARP_REQUIRE_PREEXISTING_WINE_STEAM=1 \
scripts/run-wine119-live-control-suite.sh \
  /private/tmp/metalsharp-home-wine119-dxmt32-state \
  /tmp/metalsharp-live-controls-dxmt32

scripts/verify-wine119-live-control-suite.sh \
  /tmp/metalsharp-live-controls-dxmt32

METALSHARP_LIVE_SUITE_DIR=/tmp/metalsharp-live-controls-dxmt32 \
scripts/audit-wine119-readiness.sh \
  /private/tmp/metalsharp-home-wine119-dxmt32-state \
  /tmp/metalsharp-wine119-readiness-current
```

Required pass conditions:

- Nidhogg 2 launches with `launchMethod: "m9"`.
- Schedule I launches with `launchMethod: "m11"`.
- Subnautica Below Zero launches with `launchMethod: "m11"`.
- Each game has a live game PID, not just a Steam URL/helper PID.
- Wine Steam is prestarted before the controls.
- Each game launch has a pre-existing `Steam.exe` PID before launch.
- The pre-existing `Steam.exe` PID survives each launch.
- `lsof` and launch summaries prove the expected DXMT/WineMetal/MoltenVK/cache paths.
- Before release, keep the app-facing route audit green and, if the packaged UI
  changes, repeat this proof through Electron before tag bump.
- M12 remains a mapped rebuild surface until a dedicated M12 control game has
  passed with the same process/module/cache proof.

If `dxmt32` fails specifically on the i386 WineMetal artifact, repeat Passes 2
and 3 with the `borrowed` candidate. A borrowed i386 bridge is only acceptable
as a compatibility artifact after live M9 proof and an explicit rationale.

## Pass 4: Anti-Cheat Hook Surface

Allowed only after Pass 3.

Goal: add hook symbols and readiness checks without changing normal route behavior.

Allowed intent:

- Rebuild the mscompatdb hook contract from `2644948`.
- Rebuild dylib parity from `d86fd03`.
- Rebuild readiness probes from `1148f57`.

Not allowed:

- Runtime schema bump.
- Prefix/game migration.
- Global Steam handoff replacement.
- Mutation during normal-game routes.

Required proof:

- `ntdll.so` exports `_MetalSharpGetMscompatdbHookContract`.
- `ntdll.so` exports `_MetalSharpGetMscompatdbHookContractVersion`.
- Runtime endpoint distinguishes `loaded`, `symbol_present`, and `hook_ready`.
- The three normal control games still pass Pass 3 after hook surface lands.

## Pass 5: Protected Steam Handoff

Allowed only after Pass 4.

Goal: support explicit anti-cheat recipes without changing normal game launches.

Rules:

- Protected handoff is opt-in per anti-cheat recipe.
- Already-running Wine Steam must not be stopped for normal M9/M11/M12 launches.
- Normal `/steam/launch-game` must keep 0.33.27 attach semantics.
- Any delegated helper PID must be labeled as delegated and must not satisfy game PID proof.

Required proof:

- Protected route logs show why a game opted in.
- Normal Nidhogg 2, Schedule I, and Subnautica BZ still pass Pass 3.
- A protected anti-cheat target has separate evidence and does not borrow normal-game proof.

## Pass 6: Later Experiments

These are not part of the first Wine 11.9 parity release:

- WineMetal prefix/runtime relocation from `e2dff52` and `2f0d9c6`.
- Native-only DirectX overrides from `c1dd812`.
- Cache contract rewrite from `c01180f`.
- Unity route/argument changes from `f548c5e`, `03c0826`, `4e3afde`, and `610411d`.
- D3D12/DXGI runtime behavior patches.

Each later experiment needs:

- a feature flag or route-level toggle,
- before/after launch logs,
- fresh live control proof,
- an automatic fallback to the proven parity model.

## Final Release Checklist

- Readiness audit passes with `METALSHARP_LIVE_SUITE_DIR` set.
- Version bump is created fresh, not replayed from deleted releases.
- New tag is created only after the live suite passes.
- Release notes identify the candidate type:
  - true 11.9 i386 WineMetal,
  - AverySSD DXMT i386 bridge,
  - or borrowed 11.5 i386 compatibility bridge.
- Release notes explicitly say whether anti-cheat hook surface is enabled, diagnostic-only, or deferred.
- GitHub release asset is verified after upload by re-downloading and re-running `scripts/runtime-manifest.sh`.
