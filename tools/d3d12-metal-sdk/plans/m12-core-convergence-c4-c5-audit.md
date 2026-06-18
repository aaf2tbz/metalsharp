# M12 Core Convergence C4 / C5 Completion Audit

Date: 2026-06-17
Branch: `fix/m12-shader-probe-lab`

## Objective restatement

Complete:

- **C4**: gated probe-native replay executor for classifier-safe mini/probe packet streams, with whole-list PE fallback on unsupported/invalid/stale shapes.
- **C5**: native scalar render-pass/encoder ownership planning plus root/binding cache metadata for native-executed packet streams, with cache payload reuse still disabled and resource-layout validation still required.

Also update the stale roadmap footer.

## Completion matrix

| Requirement | Evidence | Status |
|---|---|---|
| Roadmap footer updated | `m12-core-convergence-flow.md` now states C0 through C3.5 are complete and C4/C5 were next; this audit updates it again to C6 after completion. | PASS |
| C4 plan written before implementation | `m12-core-convergence-c4-c5-plan.md` records objectives, constraints, deliverables, validation plan, and completion criteria. | PASS |
| C4 ABI is C/POD/scalar | `m12core.h` adds `M12CoreReplayPacketExecuteDesc/Summary` and enum flags/status/fallbacks; packet array is passed as an existing POD pointer pattern, no C++/COM/Metal ownership crosses ABI. | PASS |
| C4 native executor exists | `m12core_execute_replay_packet_stream` validates packets, consults classifier support, returns gate-disabled fallback by default, returns unsupported-shape fallback for unsafe streams, and scalar-executes safe probe packets. | PASS |
| C4 bridge exists | `WMTM12CoreExecuteReplayPacketStream` added with unixcall `165`; `winemetal_unix.c`, `winemetal_thunks.c/.h`, and `winemetal.h` are wired append-only. | PASS |
| C4 PE fallback/default-off preserved | Gate-off command replay emitted `M12_PROBE_REPLAY_EXECUTE gate=0 native_executed=0 whole_list_fallback=1 fallback=1`; PE command replay completed normally. | PASS |
| C4 gate-on unsupported fallback works | Gate-on real command replay emitted `M12_PROBE_REPLAY_EXECUTE gate=1 native_executed=0 whole_list_fallback=1 fallback=4 ... validation=0x3`; unsupported real stream did not execute natively. | PASS |
| C4 safe probe execution covered | Native convergence probe checks `probe_replay_gate_on_executes_safe=true`, with `executed_packet_count=2`, `binding_packet_count=1`, `draw_packet_count=1`. | PASS |
| C5 ABI is C/POD/scalar | `m12core.h` adds `M12CoreEncoderOwnershipDesc/Summary` and enum flags/status; no ownership-bearing objects cross ABI. | PASS |
| C5 native planner exists | `m12core_plan_encoder_ownership` plans scalar encoder open/close counts, render/compute/blit class counts, root-binding cache metadata, and layout-validation requirements. | PASS |
| C5 bridge exists | `WMTM12CorePlanEncoderOwnership` added with unixcall `166`; thunk/unix tables append after C4. | PASS |
| C5 runtime metrics emit | Command replay emitted `M12_ENCODER_OWNERSHIP_PLAN ... cache_payload_reuse=0 layout_validation_required=1 ...`; unsupported real stream remains not native-owned. | PASS |
| C5 safe probe cache/encoder covered | Native convergence probe checks `encoder_ownership_plan=true`, `root_binding_cache_metadata=true`, and `encoder_layout_validation_and_no_payload_reuse=true`. | PASS |
| Build/feature surface updated | `M12CORE_BUILD_ID_HIGH = 0x00000017`; native feature flags `0x0fffffff`; layer feature flags `0x000000000007ffff`; build string `convergence-c4-c5 replay`. | PASS |
| Backend dry-run completed before launches | Backend dry-run matrix passed; no game visual launch was run. | PASS |
| Generated results not intended for commit | Evidence files are under `tools/d3d12-metal-sdk/results/` and should remain unstaged. | PASS |

## Validation commands run

```bash
clang-format -i vendor/dxmt/src/m12core/m12core.cpp vendor/dxmt/src/m12core/m12core.h vendor/dxmt/src/d3d12/d3d12_command_queue.cpp vendor/dxmt/src/d3d12/d3d12_device.cpp vendor/dxmt/src/d3d12/d3d12_device.hpp vendor/dxmt/src/winemetal/winemetal.h vendor/dxmt/src/winemetal/winemetal_thunks.c vendor/dxmt/src/winemetal/winemetal_thunks.h vendor/dxmt/src/winemetal/unix/winemetal_unix.c tools/d3d12-metal-sdk/probes/probe_m12_detection/probe_m12_detection.cpp
clang-format --dry-run --Werror vendor/dxmt/src/d3d12/d3d12_command_queue.cpp vendor/dxmt/src/d3d12/d3d12_device.cpp vendor/dxmt/src/d3d12/d3d12_device.hpp vendor/dxmt/src/m12core/m12core.cpp vendor/dxmt/src/m12core/m12core.h vendor/dxmt/src/winemetal/winemetal.h vendor/dxmt/src/winemetal/winemetal_thunks.c vendor/dxmt/src/winemetal/winemetal_thunks.h vendor/dxmt/src/winemetal/unix/winemetal_unix.c tools/d3d12-metal-sdk/probes/probe_m12_detection/probe_m12_detection.cpp
python3 -m py_compile tools/d3d12-metal-sdk/scripts/probe-m12-convergence-c1.py
bash -n tools/d3d12-metal-sdk/scripts/m12-dev.sh
git diff --check
./tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime
./tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime
./tools/d3d12-metal-sdk/scripts/m12-dev.sh convergence-probe
./tools/d3d12-metal-sdk/scripts/build-probes.sh
DXMT_RUNTIME="$HOME/.metalsharp/runtime/wine/lib/dxmt_m12" \
DXMT_M12CORE_ENABLE=1 \
DXMT_M12CORE_PATH="$HOME/.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-unix/libm12core.dylib" \
./tools/d3d12-metal-sdk/scripts/m12-dev.sh probes -- --command-replay-only
DXMT_RUNTIME="$HOME/.metalsharp/runtime/wine/lib/dxmt_m12" \
DXMT_M12CORE_ENABLE=1 \
DXMT_M12CORE_REPLAY_EXECUTE=1 \
DXMT_M12CORE_PATH="$HOME/.metalsharp/runtime/wine/lib/dxmt_m12/x86_64-unix/libm12core.dylib" \
./tools/d3d12-metal-sdk/scripts/m12-dev.sh probes -- --command-replay-only
# Direct probe_m12_detection.exe under staged dxmt_m12 runtime.
# Backend dry-run matrix for appids 1245620, 1962700, 1888160, 3164500, 3527290.
```

## Evidence artifacts generated locally

- Native convergence probe: `tools/d3d12-metal-sdk/results/m12-convergence-c1-probe-20260617-232822.json`
  - `ok=true`
  - `build_id_high=0x00000017`
  - `feature_flags=0x0fffffff`
  - `probe_replay_gate_off_fallback=true`
  - `probe_replay_gate_on_executes_safe=true`
  - `probe_replay_unsupported_fallback=true`
  - `encoder_ownership_plan=true`
  - `root_binding_cache_metadata=true`
  - `encoder_layout_validation_and_no_payload_reuse=true`
- Detection probe: `tools/d3d12-metal-sdk/results/probe-m12-detection-metalsharp.json`
  - `pass=true`
  - `m12core_build_id_high=0x00000017`
  - `m12core_feature_flags=0x0fffffff`
  - `feature_flags=0x000000000007ffff`
  - `build_string=MetalSharp DXMT M12 convergence-c4-c5 replay abi=1`
- Command replay gate-off log: `/tmp/m12-c4-c5-command-replay-gateoff.out`
  - `M12_PROBE_REPLAY_EXECUTE gate=0 native_executed=0 whole_list_fallback=1 fallback=1`
  - `M12_ENCODER_OWNERSHIP_PLAN ... cache_payload_reuse=0 layout_validation_required=1`
- Command replay gate-on log: `/tmp/m12-c4-c5-command-replay-gateon2.out`
  - `M12_REPLAY_EXECUTE_PLAN ... gate=1 fallback=3` from legacy C5 planner (indirect unsupported).
  - `M12_PACKET_SHAPE_SHADOW ... reasons=0x3 ... safe_for_probe_replay=0`
  - `M12_PROBE_REPLAY_EXECUTE gate=1 native_executed=0 whole_list_fallback=1 fallback=4 validation=0x3`
  - `M12_ENCODER_OWNERSHIP_PLAN native_encoder_owned=0 ... cache_payload_reuse=0 layout_validation_required=1`
- Backend dry-run matrix: `tools/d3d12-metal-sdk/results/m12-convergence-c4-c5-dryrun-20260617-232910/summary.json`
  - `ok=true` for Elden Ring, Subnautica 2, Armored Core VI, Schedule I, and PEAK.

## Non-goals / residual work

- No game visual launch was run.
- Native replay remains gated and limited to scalar-safe probe packet streams.
- Real command replay stream with indirect/copy shape correctly falls back whole-list.
- No cache payload reuse was enabled.
- No raw cache payloads, metallibs, DXBC blobs, or generated result payloads should be committed.
- C6 remains the next slice: native presenter ownership + thin transport boundary.

## Verdict

C4 and C5 are complete as gated scalar-native convergence slices. The native core now owns safe probe packet replay decisions and scalar encoder/root-binding cache metadata for native-executed streams, while PE execution remains authoritative fallback and native execution/cache reuse remain constrained by gates and validation.
