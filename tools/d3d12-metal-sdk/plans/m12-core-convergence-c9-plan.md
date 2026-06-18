# M12 Core Convergence C9 Plan — Thin PE DLL Checkpoint

Date: 2026-06-18
Branch: `fix/m12-shader-probe-lab`

## Objective

Complete C9 by making the final PE/native responsibility split explicit, testable, and ready for C10 visual validation.

## Required final roles

- `d3d12.dll`: COM wrappers + command serialization.
- `dxgi.dll`: bootstrap/wrapper.
- `dxgi_dxmt.dll`: DXGI wrappers + bridge.
- `winemetal.dll`: thunk transport.
- `winemetal.so`: native loader / native object bridge.
- `libm12core`: runtime/renderer/cache policy owner.

## Implementation plan

1. Add append-only native checkpoint ABI:
   - feature `M12CORE_FEATURE_THIN_PE_CHECKPOINT`.
   - `M12CoreThinPECheckpointDesc/Summary`.
   - `m12core_plan_thin_pe_checkpoint`.
2. Wire unixcall `170 WMTM12CorePlanThinPECheckpoint`.
3. Emit PE checkpoint diagnostic on device startup:
   - `M12_THIN_PE_CHECKPOINT` with role flags, obsolete policy quarantine status, C10 readiness bit.
4. Extend convergence/detection probes.
5. Add C9 audit and C10 visual readiness plan.

## Acceptance criteria

- Native checkpoint confirms all final module roles.
- Summary says thin PE checkpoint ready only when COM facade, command serialization, bridge/thunk transport, native loader, core runtime/cache owner, fallback, and obsolete-policy quarantine are all present.
- PE diagnostic emits the checkpoint.
- Build/probes/detection/command replay/dry-run pass.
- Autoreview has no actionable blocker.
- C10 is ready to launch 2.5-minute visual confirmation tests, but no visual launch occurs without explicit user approval.
