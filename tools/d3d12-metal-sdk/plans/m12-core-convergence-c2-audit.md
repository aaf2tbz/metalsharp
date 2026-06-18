# M12 Core Convergence C2 Completion Audit

Date: 2026-06-17
Branch: `fix/m12-shader-probe-lab`

## Scope

Slice C2: **Shadow packet recording + shadow cache index**.

C2 is shadow-only. It does not move replay execution, Metal ownership, cache payload reuse, resource ownership, or presenter ownership into `libm12core`.

## Completion matrix

| Requirement | Evidence | Status |
|---|---|---|
| PE records real command streams into POD packets | `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp` parses each real `CmdHeader` stream in `ExecuteCommandLists` into `M12CoreCommandPacket` records via `M12BuildPacketStream`. Packet fields are scalar/POD only. | PASS |
| Native core validates packets shadow-only | Added PE/unix bridge calls `WMTM12CoreValidateCommandPacketStream` / unixcall `160` to call `m12core_validate_command_packet_stream`; C2 logs `M12_PACKET_STREAM_SHADOW`. | PASS |
| Shadow cache index records metadata only | Added PE/unix bridge `WMTM12CoreMakeCacheCompatibilityKey` / unixcall `161`; C2 logs `M12_CACHE_INDEX_SHADOW` from scalar stream/cache compatibility keys only. No cache payload lookup/reuse is enabled. | PASS |
| C2 feature/build surface is visible | `M12CORE_BUILD_ID_HIGH = 0x00000015`; feature flags include `M12CORE_FEATURE_COMMAND_PACKET_SHADOW_RECORDING` and `M12CORE_FEATURE_CACHE_INDEX_SHADOW`. Detection probe shows feature flags `0x007fffff`. | PASS |
| PE fallback remains authoritative | C2 only logs/validates after PE replay. No native replay gate is enabled and no command execution path switches to native packets. | PASS |
| Required metrics emitted | Command replay probe emitted: `packet_streams_seen=1`, `packet_streams_valid=1`, `unsupported_packet_reason=0`, `cache_index_written=1`, `cache_index_corrupt=0`. | PASS |

## Validation evidence

Commands run:

```bash
clang-format -i vendor/dxmt/src/d3d12/d3d12_command_queue.cpp vendor/dxmt/src/d3d12/d3d12_device.cpp vendor/dxmt/src/d3d12/d3d12_device.hpp vendor/dxmt/src/m12core/m12core.cpp vendor/dxmt/src/m12core/m12core.h vendor/dxmt/src/winemetal/winemetal.h vendor/dxmt/src/winemetal/winemetal_thunks.c vendor/dxmt/src/winemetal/winemetal_thunks.h vendor/dxmt/src/winemetal/unix/winemetal_unix.c tools/d3d12-metal-sdk/probes/probe_m12_detection/probe_m12_detection.cpp
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
# Direct probe_m12_detection.exe under staged dxmt_m12 runtime.
# Backend dry-run matrix for appids 1245620, 1962700, 1888160, 3164500, 3527290.
```

Artifacts generated locally (not intended for commit):

- Native ABI probe: `tools/d3d12-metal-sdk/results/m12-convergence-c1-probe-20260617-185347.json`
  - `ok=true`
  - `build_id_high=0x00000015`
  - `feature_flags=0x007fffff`
- Detection probe: `tools/d3d12-metal-sdk/results/m12-convergence-c2-validation/probe-m12-detection-metalsharp.json`
  - `pass=true`
  - `m12core_build_id_high=0x00000015`
  - `m12core_feature_flags=0x007fffff`
  - `feature_flags=0x0000000000003fff`
- Command replay probe: `tools/d3d12-metal-sdk/results/probe-command-replay-metalsharp.json`
  - `pass=true`
  - Runtime log line emitted `M12_PACKET_STREAM_SHADOW packet_streams_seen=1 packet_streams_valid=1 unsupported_packet_reason=0 ...`
  - Runtime log line emitted `M12_CACHE_INDEX_SHADOW cache_index_written=1 cache_index_corrupt=0 ...`
- Backend dry-run matrix: `tools/d3d12-metal-sdk/results/m12-convergence-c2-dryrun-20260617-184524/summary.json`
  - `ok=true` for Elden Ring, Subnautica 2, Armored Core VI, Schedule I, and PEAK.

## Non-goals / residual work

- No cache payload reuse yet.
- No native command replay yet.
- Packet object IDs are shadow scalar identifiers derived from PE command records; C3 must replace them with native handle-registry IDs before native replay/cache binding.
- Subnautica 2 runtime PSO/draw-skip investigation remains separate from C2.

## Verdict

C2 is complete as a shadow-only convergence slice. It adds real PE command-stream packet recording, native packet validation, scalar cache compatibility index shadowing, surfaced C2 feature/build bits, and validation evidence while preserving PE fallback and default-off native execution gates.
