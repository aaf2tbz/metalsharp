# M12 Elden Ring Runtime Guardrails

This document records the safety rules for continuing Elden Ring M12 repair after the 2026-06-15 no-render regression.

## Known-good runtime snapshot

The last confirmed Elden Ring-loaded M12 runtime is preserved at:

```text
/Volumes/AverySSD/MetalSharp-M12-Preserved/elden-ring-loaded-20260615-181806/runtime-dxmt_m12
```

Known-good hashes after restore:

```text
d3d12.dll      92fba1da24895a9bb3c66c7f5a595001caf6f4375e6195966ccbdbabf3525a16
dxgi_dxmt.dll  793884a39b195c74121fe322be56617b100a952c1f1bb54d1aaa43d4c6fd31a2
winemetal.so   e82da21aca2a5b748cefdacc9794be9d96c1d028b90d4f94414c14214993daaa
```

Rollback command:

```bash
rsync -a --delete \
  /Volumes/AverySSD/MetalSharp-M12-Preserved/elden-ring-loaded-20260615-181806/runtime-dxmt_m12/ \
  "$HOME/.metalsharp/runtime/wine/lib/dxmt_m12/"
```

## Regressed runtime evidence

The no-render regression state was preserved at:

```text
/Volumes/AverySSD/MetalSharp-M12-Preserved/elden-ring-regression-20260615-190901
```

The regressed runtime differed in `d3d12.dll` and `dxgi_dxmt.dll`; `winemetal.so` matched the known-good runtime. The runtime-affecting commit `f6567a0` was reverted by `8963305`.

## Rules for future runtime changes

1. One runtime behavior change per commit.
2. Prefer offline/tooling-only commits first.
3. Runtime diagnostics must be post-failure, opt-in, and side-effect free.
4. Diagnostics must not overwrite successful manifests or affect PSO cache identity.
5. Any behavior change that is not obviously required must be env-gated.
6. Do not change Metal PSO creation order as part of diagnostics.
7. Rebuild/stage only after offline validation passes.
8. Immediately run one bounded launch after staging.
9. If visual output regresses, preserve evidence, restore the known-good runtime, and revert the suspect runtime commit.

## Current safe optimization baseline

Use workers=2 for bounded correctness probes unless deliberately testing worker-count behavior:

```bash
tools/d3d12-metal-sdk/scripts/m12-bounded-launch.sh --profile elden-ring --seconds 60 --workers 2
```

Do not promote higher worker counts until correctness is stable and bounded metrics show no unsafe draw skips or visual quality regression.
