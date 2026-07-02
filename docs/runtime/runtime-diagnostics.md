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
- Steam and GOG prefix policy checks.

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
- `installReplacementGuard` — destructive replacement guard.
- `nextActions` — suggested non-destructive next steps.

## Intended workflow

1. Check `/runtime/contracts` for stable lane IDs and planned/available status.
2. Check `/runtime/manifest` for exact installed runtime surfaces and artifacts.
3. Check `/runtime/diagnostics` for a single aggregate readiness answer.
4. If green, proceed to per-route doctors or game-specific launch diagnostics.
5. Do not wipe the existing install until the final replacement step is explicitly confirmed.
