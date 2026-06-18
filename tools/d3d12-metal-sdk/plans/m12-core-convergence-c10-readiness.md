# M12 Core Convergence C10 Readiness — 2.5-minute Visual Confirmation

Date: 2026-06-18

## Status

C10 is **ready/staged** for visual confirmation tests. Do not launch games until the user explicitly approves.

## Staged runtime

Runtime target: `~/.metalsharp/runtime/wine/lib/dxmt_m12`

C9 staging completed via:

```bash
./tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime
```

The refreshed runtime reports:

- `m12core_build_id_high=0x0000001b`
- `m12core_feature_flags=0xffffffff`
- `feature_flags=0x00000000007fffff`
- `build_string=MetalSharp DXMT M12 convergence-c9 thin-pe abi=1`

## Preflight evidence

```bash
./tools/d3d12-metal-sdk/scripts/m12-dev.sh preflight
```

Result: PASS

- D3D12 Metal SDK contracts: PASS
- M12 pipeline contract: PASS
- Runtime preflight JSON emitted: `tools/d3d12-metal-sdk/results/runtime-preflight-metalsharp.json`
- Shader engine contract: PASS

## C10 visual checklist, pending user approval

Run bounded 2.5-minute visual confirmation only after explicit approval:

1. Armored Core VI: verify retained Phase 9 visual output and no startup regression.
2. Elden Ring: verify retained Phase 9 visual output and no startup regression.
3. Optional Subnautica 2 remains diagnostic-only because its Metal/XPC compute PSO instability is a separate/future issue.

## Guardrails

- Do not clear live shader caches unless explicitly validating runtime source-compile fixes.
- Do not commit generated results/logs/screenshots/cache payloads.
- Native execution gates remain default-off unless explicitly enabled by validation environment.
- PE fallback remains mandatory.
- If a game launch is approved, keep it bounded to 2.5 minutes and collect only summary evidence.
