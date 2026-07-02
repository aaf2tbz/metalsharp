# Runtime Diagnostics

MetalSharp Wine 2.0 exposes a read-only aggregate diagnostics endpoint:

```text
GET /runtime/diagnostics
```

This endpoint is intentionally non-mutating. It does not install assets, launch Wine, run `wineboot`, repair prefixes, or authorize replacing the current app install.

## Scope

`/runtime/diagnostics` combines the safe diagnostic state from:

- runtime lane contracts;
- runtime manifest validation;
- Wine/DXMT artifact presence and current-version checks;
- canonical M12 installed surface checks;
- Steam and GOG prefix policy checks;
- filesystem-only Steam/GOG source readiness, including GOGDL binary/auth/prefix state.

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
- `sources` — filesystem-only source readiness. GOG reports `gogdlAvailable`, `authPresent`, and dedicated `gog-prefix` state without spawning `gogdl`.
- `lanes` — one read-only readiness row per runtime contract, with `availableReady`/`availableTotal` for implemented lanes plus `planned` and `external` counts for roadmap/external lanes. Each graphics runtime lane may include `artifactSummary` with `present`, `missing`, `total`, and `allPresent` counts derived from the filesystem-only manifest artifact report. Stable blocker ids include `wine_binary`, `runtime_manifest`, `dxmt_runtime`, `dxmt_m12_runtime`, `gogdl_source`, `lane_planned`, and `external_runtime`.
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

The scripts only read `/runtime/diagnostics`; they do not launch Wine, repair assets, mutate prefixes, or authorize install replacement. They print both implemented-lane readiness (`availableReady`/`availableTotal`) and all-lane coverage, where planned/external lanes are informational rather than required blockers. They also enforce `updateGuard.ok`, so a private Wine 2.0 build fails preflight if it is configured to use the public MetalSharp update feed. They exit non-zero when diagnostics are not green, update guard is unsafe, or the install replacement guard is unexpectedly enabled.

## Intended workflow

1. Check `/runtime/contracts` for stable lane IDs and planned/available status.
2. Check `/runtime/manifest` for exact installed runtime surfaces and artifacts.
3. Check `/runtime/diagnostics` for a single aggregate readiness answer.
4. Optionally run `tools/runtime/check-wine20-runtime-readiness.sh` for a terminal preflight summary.
5. If green, proceed to per-route doctors or game-specific launch diagnostics.
6. Do not wipe the existing install until the final replacement step is explicitly confirmed.
