# Private Fork Update Policy

MetalSharp Wine 2.0 is a private code fork. Builds from this fork must not silently update from the public `aaf2tbz/metalsharp` release feed.

## Default behavior

The backend update checker returns a successful but disabled update response by default:

```json
{
  "ok": true,
  "available": false,
  "updates_disabled": true
}
```

This keeps the Settings UI stable while preventing a Wine 2.0 private build from replacing itself with a public MetalSharp DMG.

## Opt-in overrides

Use one of these environment variables only when intentionally testing updates:

```text
METALSHARP_UPDATE_REPO_API=https://api.github.com/repos/aaf2tbz/MetalSharp-Wine-2.0/releases/latest
```

or, to explicitly re-enable the public feed:

```text
METALSHARP_ALLOW_PUBLIC_UPDATES=1
```

`METALSHARP_UPDATE_REPO_API` wins over `METALSHARP_ALLOW_PUBLIC_UPDATES`.

## Diagnostics and preflight

`GET /runtime/diagnostics` includes an `updateGuard` object:

```json
{
  "ok": true,
  "releaseFeed": "disabled",
  "publicUpdatesDisabled": true,
  "customRepoConfigured": false,
  "allowPublicUpdates": false,
  "usingPublicRepo": false
}
```

For a normal private fork build, `releaseFeed` should be `disabled` until a private release feed is intentionally configured. A custom private feed reports `releaseFeed: "custom"`. The public MetalSharp feed reports `releaseFeed: "public"` and makes `updateGuard.ok` false.

Before replacement or release validation, run:

```bash
cd app
npm run wine20:readiness:local
```

The preflight starts a temporary backend, reads `/runtime/diagnostics`, and fails if `updateGuard.ok` is false or if the install replacement guard is unexpectedly enabled. It does not launch Wine, mutate prefixes, download updates, or authorize replacement.

## Replacement rule

Do not wipe or replace the current MetalSharp install from updater state alone. The final switch to MetalSharp Wine 2.0 should happen only after runtime diagnostics and launch validation are green and the user explicitly confirms the destructive install replacement step.
