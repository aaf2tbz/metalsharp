# M12 Core Convergence C4 / C5 Plan

Date: 2026-06-17
Branch: `fix/m12-shader-probe-lab`

## Objective

Complete the next two convergence slices after C3/C3.5:

- **C4**: probe-native replay executor for classifier-safe mini/probe packet streams, gated by `DXMT_M12CORE_REPLAY_EXECUTE=1`, with whole-list PE fallback for unsupported/invalid/stale streams.
- **C5**: native render-pass/encoder ownership planning plus root/binding cache metadata for native-executed packet streams, without cache payload reuse and without bypassing resource-layout validation.

## Constraints

- Keep PE execution as the authoritative fallback at every seam.
- Native replay gate remains default-off.
- Do not pass C++/COM/Metal ownership-bearing objects across `libm12core`; C ABI remains POD/scalar only.
- C4/C5 may produce native scalar replay/encoder plans and diagnostics, but must not enable broad game native replay or cache payload reuse.
- Backend dry-run must precede any visual/game launch; no game launch is required for these slices.

## Deliverables

### C4 — Probe-native replay executor

1. Add `M12CORE_FEATURE_PROBE_REPLAY_EXECUTOR` and bump `M12CORE_BUILD_ID_HIGH`.
2. Add POD ABI structs for packet-stream replay execution:
   - `M12CoreReplayPacketExecuteDesc`.
   - `M12CoreReplayPacketExecuteSummary`.
   - replay execute status/fallback/flag enums.
3. Add `m12core_execute_replay_packet_stream()`:
   - validates packet stream via existing packet validation/classifier paths;
   - returns gate-disabled fallback when `DXMT_M12CORE_REPLAY_EXECUTE` is not represented in desc flags;
   - returns whole-list fallback for invalid/unsupported/stale/missing-ID shapes;
   - produces deterministic native scalar execution counts for safe probe streams.
4. Add winemetal PE/unix bridge thunk and unixcall for the new ABI.
5. Extend PE logging with `M12_PROBE_REPLAY_EXECUTE` metrics while preserving PE fallback.
6. Extend the native convergence probe with safe/unsupported/gate-off C4 checks.

### C5 — Encoder ownership + root/binding cache metadata

1. Add `M12CORE_FEATURE_ENCODER_OWNERSHIP_PLANNING` and `M12CORE_FEATURE_ROOT_BINDING_CACHE_METADATA`.
2. Add POD ABI structs for native encoder/cache planning:
   - `M12CoreEncoderOwnershipDesc`.
   - `M12CoreEncoderOwnershipSummary`.
3. Add `m12core_plan_encoder_ownership()`:
   - requires validated/safe packet streams;
   - plans render/compute/blit encoder class counts and open/close operation counts;
   - emits root/binding cache metadata keys;
   - marks resource-layout validation required;
   - keeps cache payload reuse disabled.
4. Add winemetal PE/unix bridge thunk and unixcall for C5 planning.
5. Extend PE logging with `M12_ENCODER_OWNERSHIP_PLAN` metrics.
6. Extend the native convergence probe with C5 cache/validation checks.

## Validation plan

- `clang-format --dry-run --Werror` for touched C/C++ files.
- `python3 -m py_compile tools/d3d12-metal-sdk/scripts/probe-m12-convergence-c1.py`.
- `bash -n tools/d3d12-metal-sdk/scripts/m12-dev.sh`.
- `git diff --check`.
- `./tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime`.
- `./tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime`.
- `./tools/d3d12-metal-sdk/scripts/m12-dev.sh convergence-probe`.
- `./tools/d3d12-metal-sdk/scripts/build-probes.sh`.
- Direct detection probe against staged `dxmt_m12` runtime.
- Command replay probe, gate-off and gate-on (`DXMT_M12CORE_REPLAY_EXECUTE=1`), verifying fallback remains intact.
- Backend dry-run matrix for Elden Ring, Subnautica 2, AC6, Schedule I, and PEAK.

## Completion criteria

C4/C5 are complete when source/docs/probes are committed and pushed, native probes cover both slices, runtime command replay logs the new C4/C5 metrics, backend dry-run is green, PE fallback/default-off behavior is preserved, and no generated result payloads are staged.
