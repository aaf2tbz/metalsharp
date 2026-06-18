# M12 Core Convergence C8 Plan — Expand Native Replay Coverage While Thinning PE

Date: 2026-06-18
Branch: `fix/m12-shader-probe-lab`

## Objective

Complete C8 by making native replay coverage/thin-PE decisions explicit and measurable:

- Classify more command packet classes as native-replay-covered or PE-fallback-required.
- Keep PE COM ABI/object facade intact.
- Reduce PE renderer-policy ownership by moving replay coverage policy into `libm12core` scalar planning.
- Preserve whole-list PE fallback for unsupported/copy/query/indirect/stale/missing-native-ID shapes.

## Non-goals

- No arbitrary real-game native replay execution.
- No visual/game launch.
- No removal of PE fallback.
- No C++/COM/Metal objects across `libm12core` ABI.

## Implementation plan

1. Add append-only `libm12core` C/POD ABI:
   - feature `M12CORE_FEATURE_EXPANDED_NATIVE_REPLAY_COVERAGE`.
   - `M12CoreReplayCoverageDesc/Summary`.
   - `m12core_plan_replay_coverage`.
2. Wire unixcall `169 WMTM12CorePlanReplayCoverage`.
3. Integrate PE command stream diagnostics:
   - emit `M12_REPLAY_COVERAGE_THIN_PE` with native-covered, PE-fallback, unsupported, policy-native, and COM-facade-preserved counters.
   - consume C3.5 shape classifier and C4/C5 replay/encoder summaries when available.
4. Extend probes for safe graphics/compute/barrier coverage and copy/unsupported fallback.
5. Update roadmap/audit docs.

## Acceptance criteria

- Safe graphics/compute/barrier/binding packet streams show native replay coverage and policy migration counts.
- Copy/unsupported/invalid/stale/missing-ID streams remain PE fallback.
- Summary records COM facade preservation and thin transport/policy migration.
- Build/probe/command replay validation passes.
- Autoreview after C8 has no actionable correctness blocker.
