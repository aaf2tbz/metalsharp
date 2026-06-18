# M12 Core Convergence C0/C1 Completion Audit

## Objective audited

Complete Phase C0 and Phase C1, save the unified roadmap, and continue working from it using the same slice-gated workflow.

## Deliverables and evidence checklist

| Requirement | Evidence | Status |
|---|---|---|
| Save an interleaved Phase 9/10/11 roadmap | `tools/d3d12-metal-sdk/plans/m12-core-convergence-flow.md` | PASS |
| Roadmap must distinguish Phase 9 foundation KEEP from full ownership pending | `m12-core-convergence-flow.md` states Phase 9 visual acceptance is KEEP but full command replay / presenter ownership remains pending. `m12-libm12core-unified-loader-roadmap.md` now points to the convergence flow as authoritative. | PASS |
| Define one flow metric instead of disconnected phase tracking | `m12-core-convergence-flow.md` defines Core Convergence Score with weights and hard caps. | PASS |
| Define C0 and C1 slice markers | `m12-core-convergence-flow.md` records C0 through C10; C0 and C1 include tasks, validation, and done criteria. | PASS |
| Record PE responsibility inventory for C1/P10.S0 | `tools/d3d12-metal-sdk/plans/m12-core-convergence-pe-inventory.md` | PASS |
| Add C1 POD command packet ABI | `vendor/dxmt/src/m12core/m12core.h` adds `M12CoreCommandPacketHeader`, `M12CoreCommandPacket`, `M12CoreCommandPacketStreamDesc`, `M12CoreCommandPacketStreamSummary`, packet kind/flag/status enums, and `m12core_validate_command_packet_stream`. | PASS |
| Add C1 cache compatibility key schema | `vendor/dxmt/src/m12core/m12core.h` adds `M12CoreCacheCompatibilityDesc`, `M12CoreCacheCompatibilityKey`, cache artifact/flag/status enums, and `m12core_make_cache_compatibility_key`. | PASS |
| Preserve C/POD/scalar ABI | New ABI structs contain scalar integers and a `const M12CoreCommandPacket *` array pointer only; no STL, COM, C++ objects, Objective-C/Metal handles, exceptions, virtuals, or ownership-bearing objects cross the public ABI. | PASS |
| Feature/build surface reflects C1 | `M12CORE_BUILD_ID_HIGH = 0x00000014`; new feature bits `M12CORE_FEATURE_COMMAND_PACKET_STREAM` and `M12CORE_FEATURE_CACHE_COMPATIBILITY_KEYS`. | PASS |
| Native packet parser validates valid, unsupported, and invalid streams | `tools/d3d12-metal-sdk/scripts/probe-m12-convergence-c1.py`; latest result `tools/d3d12-metal-sdk/results/m12-convergence-c1-probe-20260617-182812.json` has `ok=true`, valid stream status `0`, unsupported stream status `2`, invalid stream status `1`. | PASS |
| Packet counters avoid category double-counting | `m12core_validate_command_packet_stream` tracks per-packet counted categories before applying flag-based fallback classification. Probe valid stream uses both kind and category flags and still reports graphics `2`, compute `1`, present `1`, barrier `1`. | PASS |
| Cache key detects missing dimensions and invalid artifact kind | C1 probe `missing_cache_key.status=2`, `missing_flags=8`; `unknown_cache_key.status=1`. | PASS |
| Cache key changes when compatibility dimensions change | C1 probe `cache_key.compatibility_key` differs from `changed_cache_key.compatibility_key`; invalidation key changes too. | PASS |
| Provide stable developer command for C1 probe | `tools/d3d12-metal-sdk/scripts/m12-dev.sh convergence-probe` | PASS |
| Probe works on Apple Silicon with x86_64 staged runtime | `probe-m12-convergence-c1.py` re-execs through `arch -x86_64 /usr/bin/python3` when launched from arm64 Python and loads staged `libm12core.dylib`. | PASS |
| Runtime build gate | `./tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime` passed after C1 changes. | PASS |
| Runtime stage gate | `./tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime` passed; output `tools/d3d12-metal-sdk/results/stage-runtime-metalsharp.json` remains local/unstaged evidence. | PASS |
| C1 native probe gate | `./tools/d3d12-metal-sdk/scripts/m12-dev.sh convergence-probe` passed; latest JSON evidence `tools/d3d12-metal-sdk/results/m12-convergence-c1-probe-20260617-182812.json`. | PASS |
| Windows probe build gate after public constant change | `./tools/d3d12-metal-sdk/scripts/build-probes.sh` passed. | PASS |
| Detection probe sees build high `0x00000014` | `tools/d3d12-metal-sdk/results/m12-convergence-c1-validation/probe-m12-detection-metalsharp.json` has `pass=true`, `m12core_build_id_high=0x00000014`, `m12core_feature_flags=0x001fffff`. | PASS |
| Backend dry-run matrix remains green | `tools/d3d12-metal-sdk/results/m12-convergence-c1-dryrun-20260617-181819/summary.json` has `ok=true` for Elden Ring, Subnautica 2, AC6, Schedule I, and PEAK. | PASS |
| Formatting / syntax checks | `clang-format --dry-run --Werror ...`, `python3 -m py_compile ...`, `bash -n tools/d3d12-metal-sdk/scripts/m12-dev.sh`, and `git diff --check` passed. | PASS |
| No game launch required for C0/C1 | No bounded game launch was run for C0/C1; validation remained build/probe/dry-run only. | PASS |
| Do not commit generated result payloads | Result JSON/log/scaffold artifacts remain local evidence; C0/C1 commit should include source/docs/scripts/probe constants only. | PASS |

## Validation commands run

```bash
clang-format -i vendor/dxmt/src/m12core/m12core.h \
  vendor/dxmt/src/m12core/m12core.cpp \
  tools/d3d12-metal-sdk/probes/probe_m12_detection/probe_m12_detection.cpp
python3 -m py_compile tools/d3d12-metal-sdk/scripts/probe-m12-convergence-c1.py
bash -n tools/d3d12-metal-sdk/scripts/m12-dev.sh
clang-format --dry-run --Werror vendor/dxmt/src/m12core/m12core.h \
  vendor/dxmt/src/m12core/m12core.cpp \
  tools/d3d12-metal-sdk/probes/probe_m12_detection/probe_m12_detection.cpp
./tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime
./tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime
./tools/d3d12-metal-sdk/scripts/m12-dev.sh convergence-probe
./tools/d3d12-metal-sdk/scripts/build-probes.sh
# Direct probe_m12_detection.exe under staged dxmt_m12 runtime
# Backend dry-run matrix for appids 1245620, 1962700, 1888160, 3164500, 3527290
python3 -m py_compile tools/d3d12-metal-sdk/scripts/probe-m12-convergence-c1.py
bash -n tools/d3d12-metal-sdk/scripts/m12-dev.sh
git diff --check
```

## Review follow-up

Autoreview found one actionable packet-summary counter issue and one minor artifact-kind range consistency issue. Both were fixed:

- Packet category counters now avoid double-counting when a packet kind and category flag both classify the same packet.
- Cache compatibility keys now reject out-of-range artifact kinds, not only `UNKNOWN`.
- The C1 probe now includes kind+category-flag packets so the counter behavior is covered.

## Completion decision

C0 and C1 are complete.

The roadmap is saved, the Phase 9/10/11 flow is interleaved under a single convergence metric, C1 has landed as a low-risk additive C/POD ABI and schema foundation, validation passed without launching games, and generated result artifacts remain local evidence only.
