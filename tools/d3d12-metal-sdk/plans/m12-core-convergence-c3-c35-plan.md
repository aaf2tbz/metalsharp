# M12 Core Convergence C3 / C3.5 Plan

Date: 2026-06-17
Branch: `fix/m12-shader-probe-lab`

## Objective

Complete the refined post-C2 split:

- **C3**: native handle registry + packets use native scalar IDs.
- **C3.5**: unsupported-shape classifier + negative cache shadow-only.

Both slices remain shadow-only. They must not enable native replay, cache payload reuse, Metal object ownership transfer, or presenter ownership transfer.

## Current baseline

C2 records real PE command streams into `M12CoreCommandPacket` and validates them natively through `m12core_validate_command_packet_stream`. C2 still uses pointer-like PE scalar identifiers in packet `object_id*` fields for PSOs, root signatures, resources, descriptor handles, command signatures, and views. C3 replaces those packet identifiers with stable native-style scalar registry IDs before later C4 native replay can rely on them.

## C3 deliverables — native handle registry + packet ID migration

Implementation target:

1. Add C/POD `libm12core` ABI records for a shadow handle registry:
   - handle kinds: resource, PSO, root signature, descriptor heap, descriptor handle/view, command signature, query heap, swapchain image, unknown.
   - registration desc/result for scalar IDs.
   - lookup/lifetime desc/result for stale-ID validation.
2. Add native `libm12core` functions:
   - register scalar PE-side object keys into native registry IDs.
   - validate registry IDs for kind/generation/lifetime in shadow mode.
3. Add PE/unix thunks for these functions with append-only unixcall IDs.
4. Update `M12BuildPacketStream` / packet payload population so packet `object_id*` fields carry registry IDs, not raw PE pointer-shaped IDs, where objects exist.
5. Emit C3 metrics in a bounded log line:
   - `handle_registry_registered`.
   - `packet_native_ids`.
   - `packet_raw_ids`.
   - `stale_handle_detected=0` for valid probe streams.

Non-goals:

- No native replay.
- No cache payload binding/reuse.
- No Metal/COM/object lifetime ownership transfer.
- No registry handle is authoritative for execution yet.

## C3.5 deliverables — unsupported-shape classifier + negative cache shadow

Implementation target:

1. Add C/POD `libm12core` ABI records for packet-stream support classification:
   - classification desc/result.
   - unsupported reason bitmask/counts.
   - negative-cache key derived from shape, not payload.
2. Implement native classifier that marks streams safe/unsupported/invalid for later C4 probe-native replay.
3. Keep the classifier conservative:
   - supported initial shapes: direct draws, direct indexed draws, dispatch, clears, render-target setup, root/pipeline/binding setup, resource barriers.
   - unsupported initial shapes: indirect execution, copies/resolves/query/write-immediate, unknown packet kind, invalid/stale handles, missing required native IDs.
4. Emit C3.5 metrics in a bounded log line:
   - `unsupported_shape_seen`.
   - `negative_cache_shadow_written`.
   - `negative_cache_corrupt=0`.
   - `safe_for_probe_replay`.
5. Add or extend native probe coverage for valid/supported, unsupported, invalid, and stale-handle cases.

Non-goals:

- Negative cache is shadow-only metadata. It must not skip execution, alter cache lookup, or suppress PE fallback.
- C4 remains the first slice that may enable gated native replay for probe streams.

## File plan

Likely code/doc edits:

- `vendor/dxmt/src/m12core/m12core.h`
  - append build/features and POD structs/enums/functions.
- `vendor/dxmt/src/m12core/m12core.cpp`
  - implement registry hashing/validation and support classifier.
- `vendor/dxmt/src/winemetal/winemetal.h`
  - add PE-visible WMT functions.
- `vendor/dxmt/src/winemetal/winemetal_thunks.h`
  - add packed unixcall payloads.
- `vendor/dxmt/src/winemetal/winemetal_thunks.c`
  - add PE thunk functions and unixcall names.
- `vendor/dxmt/src/winemetal/unix/winemetal_unix.c`
  - dlsym new functions and append unixcall handlers.
- `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp`
  - register command packet object identities and classify packet streams.
- `tools/d3d12-metal-sdk/scripts/probe-m12-convergence-c1.py`
  - extend native ABI probe for C3/C3.5 additive functions.
- `tools/d3d12-metal-sdk/probes/probe_m12_detection/probe_m12_detection.cpp`
  - update build/features.
- `tools/d3d12-metal-sdk/plans/m12-core-convergence-flow.md`
  - record refined C3/C3.5 order and completion status.
- `tools/d3d12-metal-sdk/plans/m12-core-convergence-c3-c35-audit.md`
  - final completion audit.

## Validation plan

Run before acceptance:

```bash
clang-format --dry-run --Werror <touched C/C++ files>
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
# Backend dry-run matrix before any launch.
```

Acceptance requires a prompt-to-artifact audit proving:

- C3 packets use native registry IDs where object identities exist.
- C3 stale-handle checks are covered by native probe evidence.
- C3.5 unsupported classifier distinguishes safe/unsupported/invalid/stale streams.
- Negative-cache shadow metrics are emitted without changing execution/cache behavior.
- PE fallback and default-off native execution gates remain unchanged.
