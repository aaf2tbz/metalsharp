# M12 Core Convergence C7 Plan — Cache-First Warm Start for Shaders/PSOs

Date: 2026-06-17
Branch: `fix/m12-shader-probe-lab`

## Objective

Complete C7 by moving cache-first warm-start decisions into `libm12core` while keeping compatibility and invalidation strict:

- Decide cache lookup/reuse before shader translation / PSO creation work is queued.
- Reuse/skip only compatible entries with invalidation proof.
- Let prewarm scheduling skip already-satisfied shader/PSO work.
- Record skipped-work counters and fallback/invalidated counters.

## Non-goals

- Do not commit cache payloads, metallibs, DXBC blobs, or generated result JSON.
- Do not bypass resource-layout validation.
- Do not remove PE fallback translation/creation paths.
- Do not change game visual behavior or run visual confirmation.

## Implementation plan

1. Add append-only `libm12core` C/POD ABI:
   - feature `M12CORE_FEATURE_CACHE_FIRST_WARM_START`.
   - `M12CoreCacheWarmStartDesc`.
   - `M12CoreCacheWarmStartSummary`.
   - `m12core_plan_cache_warm_start`.
2. Wire through winemetal as unixcall `168` with scalar fields only.
3. Hook PE diagnostics into the existing prewarm/cache path:
   - emit `M12_CACHE_WARM_START` with requested/hit/skipped/fallback counters.
   - require compatibility key and invalidation proof before skip decisions.
4. Extend probes:
   - native convergence probe covers compatible hit skips, invalidation fallback, force-source fallback, and prewarm skip counters.
   - detection probe requires build/feature/layer C7 flags.
5. Update roadmap/audit docs.

## Acceptance criteria

- Compatible shader+pipeline hits with invalidation proof produce skipped shader/PSO/prewarm counts.
- Missing invalidation proof produces zero skipped work and fallback work.
- Force-source/cache-disabled mode produces zero skipped work and fallback work.
- Prewarm skip counters are emitted in PE diagnostics.
- Static/build/probe validation passes.
- Autoreview after C7 has no actionable correctness blocker before commit.
