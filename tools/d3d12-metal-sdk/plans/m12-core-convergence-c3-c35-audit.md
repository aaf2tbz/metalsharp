# M12 Core Convergence C3 / C3.5 Completion Audit

Date: 2026-06-17
Branch: `fix/m12-shader-probe-lab`

## Objective restatement

Complete the refined post-C2 slices:

- **C3**: native handle registry + packet ID migration.
- **C3.5**: unsupported-shape classifier + negative cache shadow-only.

The work must update the saved roadmap/plan first, preserve PE fallback, keep native execution and cache payload reuse disabled, and finish with a prompt-to-artifact audit.

## Completion matrix

| Requirement | Evidence | Status |
|---|---|---|
| Saved plan updated before implementation | `m12-core-convergence-flow.md` splits C3 and C3.5; `m12-core-convergence-c3-c35-plan.md` records objective, deliverables, file plan, and validation plan. | PASS |
| C3 native handle registry ABI is POD/scalar | `m12core.h` adds `M12CoreHandleRegistryDesc/Result` and `M12CoreHandleValidationDesc/Result`; no STL/C++/COM/Metal objects cross ABI. | PASS |
| C3 native handle registry functions exist | `m12core_register_handle` and `m12core_validate_handle` implemented in `m12core.cpp`; PE/unix bridge added with unixcalls `162` and `163`. | PASS |
| C3 packets use native scalar IDs where object identities exist | `d3d12_command_queue.cpp` registers PSOs, root signatures, descriptor heaps/handles, command signatures, resources, and relevant views through `WMTM12CoreRegisterHandle`; packet `object_id*` fields receive registry IDs when core is enabled. | PASS |
| C3 stale-handle/lifetime probe exists | Native convergence probe checks `handle_register_ok`, `handle_validate_ok`, and `handle_stale_detected`; latest result `ok=true`. | PASS |
| C3 metrics emitted | Command replay probe emitted `M12_HANDLE_REGISTRY_SHADOW handle_registry_registered=8 packet_native_ids=8 packet_raw_ids=0 stale_handle_detected=0`. | PASS |
| C3.5 classifier ABI is POD/scalar | `m12core.h` adds `M12CorePacketSupportDesc/Summary` and reason/status enums. Pointer-bearing packet array is passed separately through established `WMTConstMemoryPointer` thunk pattern. | PASS |
| C3.5 native classifier exists | `m12core_classify_packet_support` implemented in `m12core.cpp`; PE/unix bridge added with unixcall `164`. | PASS |
| C3.5 distinguishes safe/unsupported/stale shapes | Native convergence probe checks `shape_safe`, `shape_unsupported_copy_negative`, and `shape_stale_or_missing`; latest result `ok=true`. Review follow-up fixed false-positive native-ID requirements for non-object/empty state packets, made `unsupported_shape_count` count each unsupported packet at most once, removed fragile descriptor-heap packet pointer arithmetic, and cleared shadow descriptor-heap IDs on zero-count unbinds, preserved separate graphics/compute PSO packet identities, and narrowed indirect unsupported reason reporting. | PASS |
| C3.5 negative cache remains shadow-only | `M12_PACKET_SHAPE_SHADOW` logs negative-cache key only; no execution/cache lookup path consumes it. PE replay remains authoritative. | PASS |
| C3.5 metrics emitted | Command replay probe emitted `M12_PACKET_SHAPE_SHADOW unsupported_shape_seen=1 negative_cache_shadow_written=1 negative_cache_corrupt=0 safe_for_probe_replay=0`. | PASS |
| Build/feature surface updated | `M12CORE_BUILD_ID_HIGH = 0x00000016`; native features `0x01ffffff`; detection probe passes with layer features `0x000000000000ffff`. | PASS |
| Backend dry-run completed before any launch | Backend dry-run matrix passed; no game launch was run. | PASS |
| PE fallback/native execution gate preserved | C3/C3.5 only add shadow validation/logging. `DXMT_M12CORE_REPLAY_EXECUTE` remains default-off and no native replay/cache reuse path is enabled. | PASS |

## Validation commands run

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

## Evidence artifacts generated locally

Generated results are local validation evidence and are not intended for commit.

- Native ABI probe: `tools/d3d12-metal-sdk/results/m12-convergence-c1-probe-20260617-224838.json`
  - `ok=true`
  - `build_id_high=0x00000016`
  - `feature_flags=0x01ffffff`
  - `handle_register_ok=true`
  - `handle_validate_ok=true`
  - `handle_stale_detected=true`
  - `shape_safe=true`
  - `shape_unsupported_copy_negative=true`
  - `shape_stale_or_missing=true`
- Detection probe: `tools/d3d12-metal-sdk/results/m12-convergence-c3-c35-validation/probe-m12-detection-metalsharp.json`
  - `pass=true`
  - `m12core_build_id_high=0x00000016`
  - `m12core_feature_flags=0x01ffffff`
  - `feature_flags=0x000000000000ffff`
  - `build_string=MetalSharp DXMT M12 convergence-c3-c35 shadow abi=1`
- Command replay probe: `tools/d3d12-metal-sdk/results/probe-command-replay-metalsharp.json`
  - `pass=true`
  - Runtime log emitted `M12_HANDLE_REGISTRY_SHADOW handle_registry_registered=8 packet_native_ids=8 packet_raw_ids=0 stale_handle_detected=0 ...`
  - Runtime log emitted `M12_PACKET_SHAPE_SHADOW unsupported_shape_seen=1 negative_cache_shadow_written=1 negative_cache_corrupt=0 safe_for_probe_replay=0 ...`
  - Existing PE replay path completed normally.
- Backend dry-run matrix: `tools/d3d12-metal-sdk/results/m12-convergence-c3-c35-dryrun-20260617-224905/summary.json`
  - `ok=true` for Elden Ring, Subnautica 2, Armored Core VI, Schedule I, and PEAK.

## Non-goals / residual work

- No native replay was enabled.
- No cache payload reuse or cache-first warm start was enabled.
- C3 registry IDs are scalar shadow identities; they are not ownership-bearing handles.
- C3.5 negative-cache keys are emitted only as diagnostics and are not consumed by execution/cache policy yet.
- C4 remains the first slice that may use this metadata for gated probe-native replay.

## Verdict

C3 and C3.5 are complete as shadow-only convergence slices. The roadmap has been updated, packets now carry native scalar registry IDs where object identities exist, stale-handle and shape-classification probes pass, unsupported/negative-cache shadow metrics emit, backend dry-run is green, and PE fallback/default-off native execution gates remain intact.
