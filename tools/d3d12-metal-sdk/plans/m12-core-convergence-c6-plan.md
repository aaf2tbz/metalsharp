# M12 Core Convergence C6 Plan — Native Presenter Ownership + Thin Transport Boundary

Date: 2026-06-17
Branch: `fix/m12-shader-probe-lab`

## Objective

Complete C6 without weakening fallback safety:

- Expand native present ownership beyond the narrow raw-blit execution primitive by adding a native scalar present ownership/transport plan.
- Keep `winemetal.dll` transport-oriented and append-only at the thunk boundary.
- Let `winemetal.so`/`libm12core` own the decision record for native present sequencing where safe.
- Prove double-present and double-blit prevention with native summary fields and PE logs.

## Non-goals

- No visual launch in C6.
- No removal of PE presenter fallback.
- No C++/COM/Objective-C/Metal ownership-bearing object crosses `libm12core` ABI.
- No cache payload reuse changes.

## Implementation plan

1. Add append-only `libm12core` C/POD ABI:
   - feature `M12CORE_FEATURE_NATIVE_PRESENT_OWNERSHIP`.
   - `M12CoreNativePresentOwnershipDesc`.
   - `M12CoreNativePresentOwnershipSummary`.
   - `m12core_plan_native_present_ownership`.
2. Wire the ABI through winemetal transport as unixcall `167`:
   - PE `winemetal.dll` thunk copies scalar fields only.
   - native `winemetal.so` resolves and calls the `libm12core` symbol.
3. Integrate PE swapchain diagnostics:
   - Existing raw-blit native execution remains gated by `DXMT_M12CORE_PRESENT_EXECUTE`.
   - New `M12_NATIVE_PRESENT_OWNERSHIP` log records native-owned/sequenced/fallback, planned blit/present counts, double-present prevention, and transport thinness.
   - Presenter path also records fallback/non-native ownership decisions.
4. Extend probes:
   - Python convergence probe checks gate-off fallback, raw safe native ownership, presenter fallback, and double-present prevention metadata.
   - Detection probe requires C6 build/feature/layer flags.
5. Update roadmap/audit docs.

## Acceptance criteria

- Default-off behavior plans fallback and does not claim native ownership.
- Gate-on raw present with source/drawable/support plans exactly one blit and one present.
- Gate-on presenter/non-raw path remains fallback/non-native.
- If PE already/native-presented, summary records double-present prevention and plans zero additional present operations.
- `winemetal.dll` remains transport-only for the new API.
- Static/build/probe validation passes.
- Autoreview after C6 has no actionable correctness blocker.
