# MetalSharp Vendor Trust Kit

Status: Phase 9 foundation

The anti-cheat path is cooperative vendor trust, not bypass. A vendor or game developer should be able to inspect MetalSharp's runtime identity, launch model, logs, and non-evasion policy without reverse-engineering the repository.

## Kit Contents

The vendor kit should include:

- runtime identity and version
- host ABI manifest
- signed/notarized artifact evidence when available
- Steam identity model
- compatdata sample
- launch log sample
- process tree and environment handoff explanation
- anti-cheat classification output
- no-bypass/no-spoof policy
- known unsupported boundaries
- contact and reproduction instructions

## Generator

`tools/package/create-vendor-trust-kit.sh` creates a local bundle under:

```text
dist/vendor-trust-kit/
```

The generator copies the core policy/runtime docs and writes a `manifest.json` that records the current git commit, version files, and included documents. It does not claim a game is supported; it prepares the evidence package for a vendor conversation.

## Required Before External Use

- Replace placeholder signing/notarization state with real notarized artifact evidence.
- Attach real launch logs from a target game.
- Attach real anti-cheat classification JSON from Launch Doctor.
- Add explicit vendor/game contact context.
- Remove any stale "bypass" terminology from shipped UX and docs.
