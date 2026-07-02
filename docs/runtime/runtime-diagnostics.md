# Runtime Diagnostics

MetalSharp Wine 2.0 exposes read-only diagnostics endpoints:

```text
GET /runtime/diagnostics
GET /diagnostics/gog
```

These endpoints are intentionally non-mutating. They do not install assets, launch Wine, run `wineboot`, run GOGDL, repair prefixes, or authorize replacing the current app install.

## Scope

`/runtime/diagnostics` combines the safe diagnostic state from:

- runtime lane contracts;
- runtime manifest validation;
- Wine/DXMT artifact presence and current-version checks;
- canonical M12 installed surface checks;
- Steam and GOG prefix policy checks;
- filesystem-only Steam/GOG source readiness, including GOGDL binary/auth/prefix state;
- Phase 8 Native Mono/FNA platform readiness, including Mono ARM64/x86 binary and architecture checks plus required support inventory.

`/diagnostics/gog` returns `metalsharp.gog.diagnostics.v1` for the dedicated GOGDL source. It reports GOGDL binary/auth state, the dedicated `gog-prefix` policy, cached library counts, and persisted launch receipt counts without invoking GOGDL or Wine.

`prefixMetadata.entries[]` uses `metalsharp.prefix.v2`. Existing mutating prefix operations now persist metadata at `<prefix>/.metalsharp/prefix-metadata-v2.json` and wineboot receipts under `<prefix>/.metalsharp/receipts/`. The diagnostics endpoint only reads those files and surfaces `metadataPersisted`, `metadataPath`, `persisted`, and `lastWinebootUpdate` when present.

The endpoint exists so the app can answer “is the Wine 2.0 runtime shape coherent?” before deeper per-game doctors or launch experiments run.

## Important invariants

### Canonical M12 installed runtime

Installed runtime diagnostics must report:

```text
runtime/wine/lib/dxmt_m12
```

and the canonical surface id:

```text
dxmt_m12
```

The release bundle/archive root can still use:

```text
Graphics/dll/dxmt-m12
```

but that hyphenated name must not become the installed runtime contract.

### Steam and GOG prefix separation

Steam owns:

```text
~/.metalsharp/prefix-steam
```

GOGDL-backed GOG owns:

```text
~/.metalsharp/bottles/gog-prefix/prefix
```

GOG must not use `prefix-steam`. The diagnostics endpoint reports this policy even when either prefix is not present yet.

### Install replacement guard

The response includes:

```json
"installReplacementGuard": {
  "allowedNow": false
}
```

This is deliberate. Diagnostics can prove runtime state, but wiping/replacing the current MetalSharp install is a final explicit user-confirmed step after Wine 2.0 validation passes.

## Response shape

Top-level fields:

- `ok` — true only when runtime readiness, manifest validation, canonical M12 contract, and prefix policy checks are green.
- `schema` — `metalsharp.runtime.diagnostics.v1`.
- `readOnly` — always true.
- `summary` — human-readable state summary.
- `paths` — resolved MetalSharp home, runtime, Wine binary, Steam prefix, and GOG prefix paths.
- `contracts` — contract counts and canonical M12 contract summary.
- `runtime` — Wine/DXMT/M12/manifest readiness.
- `prefixes` — Steam/GOG prefix separation report.
- `prefixMetadata` — read-only Phase 3 prefix-v2 preview for Steam, GOG, and GPTK prefixes. Entries report owner, path, presence, `drive_c`/registry state, canonical path, preserve policy, game payload policy, and notes without creating or winebooting prefixes.
- `sources` — filesystem-only source readiness. GOG reports `gogdlAvailable`, `authPresent`, and dedicated `gog-prefix` state without spawning `gogdl`.
- `nativeMono` — filesystem-only Native Mono/FNA platform doctor. It reports `native_mono_arm64` and `native_mono_x86` Mono binary presence, Mach-O architecture match, required FNA/FNA3D/FAudio/SDL2/Carbon/HiView/Kernel32 support inventory, optional shims, and non-mutating next actions.
- `vulkan` — filesystem-only Vulkan-family doctor. It checks canonical `runtime/wine/lib/dxvk`, `runtime/wine/lib/vkd3d`, required MoltenVK/VKD3D sidecars, MoltenVK ICD JSON paths, and DXVK state-cache path writability inferred from permission bits without launching Wine/Vulkan or creating probe files. It also reports non-blocking `limitations` for filesystem-only coverage, unproven MoltenVK feature level, and the rule that VKD3D-Proton must remain a fallback below M12/dxmt_m12 until game proof exists.
- `lanes` — one read-only readiness row per runtime contract, with `availableReady`/`availableTotal` for implemented lanes plus `planned` and `external` counts when present. Native Mono/FNA lanes consume `nativeMono` doctor blockers such as `mono_binary`, `mono_arch`, and `native_mono_support_assets`. Each graphics runtime lane may include `artifactSummary` with `present`, `missing`, `total`, and `allPresent` counts derived from the filesystem-only manifest artifact report. Stable blocker ids include `wine_binary`, `runtime_manifest`, `dxmt_runtime`, `dxmt_m12_runtime`, `dxvk_runtime`, `vkd3d_runtime`, `gogdl_source`, `lane_planned`, and `external_runtime`.
- `updateGuard` — read-only private-fork update-source safety. It reports whether public MetalSharp updates are disabled, a custom feed is configured, or the public release feed is in use.
- `installReplacementGuard` — destructive replacement guard.
- `nextActions` — suggested non-destructive next steps.

## Local readiness script

With a backend already running, use:

```bash
tools/runtime/check-wine20-runtime-readiness.sh
# or from app/: npm run wine20:readiness
```

or point it at a specific backend:

```bash
tools/runtime/check-wine20-runtime-readiness.sh --url http://127.0.0.1:9284
```

For a self-contained local preflight that builds the debug backend, starts it on an ephemeral port, reads diagnostics, and shuts it down:

```bash
tools/runtime/check-wine20-runtime-readiness-local.sh
# or from app/: npm run wine20:readiness:local
```

The scripts only read `/runtime/diagnostics`; they do not launch Wine, repair assets, mutate prefixes, or authorize install replacement. They print both implemented-lane readiness (`availableReady`/`availableTotal`) and all-lane coverage; external lanes remain informational rather than required blockers, and planned lanes are reported if future roadmap lanes are added. They also enforce `updateGuard.ok`, so a private Wine 2.0 build fails preflight if it is configured to use the public MetalSharp update feed. They exit non-zero when diagnostics are not green, update guard is unsafe, or the install replacement guard is unexpectedly enabled.

## Intended workflow

1. Check `/runtime/contracts` for stable lane IDs and planned/available status.
2. Check `/runtime/manifest` for exact installed runtime surfaces and artifacts.
3. Check `/runtime/diagnostics` for a single aggregate readiness answer.
4. Optionally run `tools/runtime/check-wine20-runtime-readiness.sh` for a terminal preflight summary.
5. If green, proceed to per-route doctors or game-specific launch diagnostics.
6. Do not wipe the existing install until the final replacement step is explicitly confirmed.
