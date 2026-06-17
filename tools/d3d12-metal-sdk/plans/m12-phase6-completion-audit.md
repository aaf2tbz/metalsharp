# M12 Phase 6 completion audit

Date: 2026-06-17

## Objective

Complete Phase 6 oracle/prewarm ingestion carefully, in two-slice batches with `autoreview` after each slice, backend dry-runs as the runtime is updated, and game launch only at the end of the phase.

## Implementation evidence

Phase 6 landed in small fallback-safe commits:

- `73d6f6a feat: build compact m12 prewarm packs`
  - Adds `tools/d3d12-metal-sdk/scripts/build-m12-prewarm-pack.py`.
  - Produces compact metadata-only pack `tools/d3d12-metal-sdk/results/m12-phase6-prewarm-pack-20260617-122648/prewarm-pack.json`.
  - Pack schema: `metalsharp.m12-prewarm-pack.v1`.
  - AC6 pack count: 64 pipelines.
  - `raw_payloads_included: false`; `offline_profile_gated: true`.
- `53e9853 feat: summarize m12 prewarm packs`
  - Adds POD-only `libm12core` prewarm-pack summary ABI.
  - Consumes compact pipeline/root/shader/layout metadata only.
- `60b5f03 feat: bridge m12 prewarm pack summaries`
  - Adds PE/unix thunk bridge for `m12core_summarize_prewarm_pack`.
  - Preserves fallback if old/missing `libm12core` is used.
- `fc9b4b9 feat: gate m12 prewarm pack diagnostics`
  - Adds AC6 profile-gated canary consumer in `d3d12_device.cpp`.
  - Gate: `SteamAppId=1888160` and `METALSHARP_M12_PREWARM_PROFILE=armored-core-vi-phase6-canary`.
  - The runtime canary uses a fixed in-source 8-record compact metadata subset to exercise the POD summary/schedule ABI without runtime JSON parsing or filesystem ownership. Runtime loading of the on-disk 64-pipeline `prewarm-pack.json` remains deferred.
- `20c7c80 feat: forward m12 prewarm profile env`
  - Forwards `METALSHARP_M12_PREWARM_PROFILE` through `m12-bounded-launch.sh`.
- `b4199b6 feat: log m12 prewarm schedule queue`
  - Emits a concrete ordered metadata-only queue line: `M12_PREWARM_PACK_SCHEDULE`.
- `e73c15f fix: allow m12 prewarm profile launch env`
  - Allows the backend `/steam/launch-game` env override to pass the canary profile string through unchanged.

## Review and test evidence

- `autoreview` was run after each slice/pair and after the backend env-forwarding fix; all final reviews returned no findings.
- Runtime build passed after compiled runtime slices:
  - `./tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`
- Backend test passed for env forwarding:
  - `cd app/src-rust && cargo test m12core_launch_overrides_map_to_dxmt_loader_env`
- Backend release rebuild/restart was required so `127.0.0.1:9277` could use `e73c15f` during corrected final validation.

## Final validation evidence

Corrected final validation artifact root:

```text
tools/d3d12-metal-sdk/results/m12-phase6-final-validation-corrected-20260617-130844/
```

Pre-launch dry-run:

```text
tools/d3d12-metal-sdk/results/m12-phase6-final-validation-corrected-20260617-130844/dry-run-9277.json
```

Result: `ok=true`, `pipeline=m12`, `missing=[]`.

Final bounded AC6 validation:

```text
tools/d3d12-metal-sdk/results/m12-phase6-final-validation-corrected-20260617-130844/bounded-launches/armored-core-vi-20260617-130844/summary.md
```

Key results:

- `launch_ok`: `True`
- `present_count`: `23`
- `drawn_present_count`: `23`
- `render_pso_failed`: `0`
- `compute_pso_failed`: `0`
- `sm50_compile_failed`: `0`
- `dxil_msl_compile_failed`: `0`
- `unix_call_failed`: `0`
- `new_dxbc`: `0`
- `new_msl`: `0`
- `new_metallib`: `0`
- `new_pso_render`: `0`
- `new_pso_compute`: `0`

Launch env evidence from `launch.json`:

```text
env_overrides_applied = [
  METALSHARP_M12CORE_DUMP_COUNTERS,
  METALSHARP_M12CORE_ENABLE,
  METALSHARP_M12CORE_REQUIRED,
  METALSHARP_M12_PREWARM_PROFILE,
]
```

Prewarm evidence from launch log `/Users/alexmondello/.metalsharp/compatdata/1888160/logs/launch-1781723340.log`:

```text
M12_PREWARM_PACK_SUMMARY profile=armored-core-vi-phase6-canary pipelines=8 stages=16 render=8 compute=0 roots=2 shaders=14 ordered=8 key=0x7d8f50a74df58e9c
M12_PREWARM_PACK_SCHEDULE profile=armored-core-vi-phase6-canary queue=metadata-only action=defer-metal-create eligible=8 first_order=0 first_pipeline=0x5a1ae55392218b7f last_order=7 last_pipeline=0x57b4ce99d2b318bb
```

## Requirement audit

| Requirement | Evidence | Status |
|---|---|---|
| Build compact oracle/prewarm packs and prove `libm12core` can consume the compact POD representation, not raw D3DMetal payloads | Pack schema is compact metadata only; summary ABI uses POD records of keys/counts/layout summaries; final validation created no DXBC/MSL/metallib/new PSO JSON artifacts. The runtime canary feeds an in-source 8-record compact subset into the ABI; runtime `prewarm-pack.json` file loading is intentionally deferred. | Complete for Phase 6 ABI/plumbing scope; runtime pack-file loading pending |
| Include pipeline keys, root structural keys, shader hashes, stage linkage, expected layout summaries, optional queue order | Slice 1 pack builder and committed pack metadata include these fields; Slice 2 summary ABI validates/counts them; Slice 6 logs ordered queue. | Complete |
| AC6 can exercise a prewarm-schedule canary for a known subset before gameplay | Corrected final launch logs canary `M12_PREWARM_PACK_SUMMARY` and `M12_PREWARM_PACK_SCHEDULE` during device startup before normal PSO pressure logs, with 8 eligible ordered records. It remains metadata-only, uses the fixed in-source canary subset, and deliberately defers both runtime pack-file loading and Metal PSO creation until later compatibility work. | Complete for Phase 6 metadata-summary/scheduling scope |
| Prewarm is offline/profile-gated | Gate is `SteamAppId=1888160` + `METALSHARP_M12_PREWARM_PROFILE=armored-core-vi-phase6-canary`; final launch shows override applied. | Complete |
| No raw D3DMetal metallibs/cache payloads committed | Phase 6 commits contain source/docs/compact metadata only; corrected validation summary reports `new_dxbc=0`, `new_msl=0`, `new_metallib=0`. | Complete |
| Fallback safe when `libm12core`/unixcall unavailable | PE thunk and summary call return false/diagnostic-only; live rendering continues without requiring prewarm summary. | Complete |
| C/POD-only ABI | `m12core` Phase 6 ABI structs are POD scalars/pointers/counts; no STL/C++ objects across PE/native boundary. | Complete |
| Dry-runs as runtime was updated | Dry-run artifacts were produced after staged runtime updates and final corrected dry-run is green. | Complete |
| Game launch only at phase end | All implementation slices used build/review/stage/dry-run only. A first end-phase launch was healthy but lacked the canary env due backend allowlist filtering; after fixing that, the corrected end-phase launch produced the required canary evidence. | Complete with noted correction |

## Residual risks / next phase

- Phase 6 intentionally does not load D3DMetal metallibs/cache payloads and does not create Metal PSOs from oracle data. The schedule line is `metadata-only action=defer-metal-create` by design.
- The runtime canary does not parse or load `prewarm-pack.json`; it embeds a fixed 8-record compact metadata subset to exercise the ABI and launch-profile plumbing. A later slice should translate/load the on-disk 64-pipeline pack into the POD ABI before using the full queue for replay/precompile work.
- AC6 PSO pressure remains high (`graphics_pso_compiled=1394` in final corrected validation), so Phase 7+ should treat this queue as schedule-shape evidence, not as proven full-pack runtime ingestion.
