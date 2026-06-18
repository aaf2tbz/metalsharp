# M12 Core Convergence Flow

## Purpose

This is the authoritative interleaved plan for completing the remaining M12 `libm12core` ownership work without treating Phase 9, Phase 10, and Phase 11 as long disconnected efforts.

The flow advances three tracks together:

- **P9**: deeper `libm12core` command replay / presenter ownership.
- **P10**: thinning PE DXMT DLLs into Windows ABI shims and transport layers.
- **P11**: cache-first warm start with strict compatibility and invalidation.

The current Phase 9 foundation is **KEEP** because AC6 and Elden Ring visual output were user-confirmed with no regression on the already-running runtime. That KEEP decision does **not** mean full original Phase 9 ownership is complete; the remaining convergence slices below complete that ownership safely.

## Global invariants

1. PE fallback remains available at every seam.
2. `libm12core` ABI remains C/POD/scalar only: no STL, COM, Objective-C, Metal objects, exceptions, virtuals, or ownership-bearing C++ objects cross the boundary.
3. Native execution gates default off until explicitly enabled for a validation slice.
4. Backend dry-run comes before any game launch.
5. Visual launch requires explicit user approval for the specific game/mode.
6. No raw D3DMetal cache payloads, extracted metallibs, DXBC blobs, or large corpora are committed.
7. Gate-on behavior must be comparable to gate-off fallback behavior.
8. Every slice is accepted only after a prompt-to-artifact audit maps requirements to concrete files, commands, and outputs.

## Single flow metric: Core Convergence Score

Track one score from 0 to 100.

| Component | Weight |
|---|---:|
| Native command/present ownership coverage | 30 |
| PE thinness / renderer-policy removal | 20 |
| Cache-first warm-start effectiveness | 25 |
| Fallback safety / parity | 15 |
| Validation coverage / reproducibility | 10 |

Hard caps:

- Any ABI violation caps score at 40.
- Any gate-off regression caps score at 50.
- Any native unixcall crash/failure in bounded run caps score at 60.
- Any visual regression in AC6/Elden Ring caps score at 70.
- No cache invalidation proof caps score at 85.
- No final audit caps score at 95.

## Slice C0 — Reconcile roadmap and baseline

Touches: **P9 / P10 / P11**

Status: complete when this plan and the C0/C1 evidence audit are committed.

Tasks:

- Save this unified convergence plan.
- Correct the Phase 9 status: foundation/visual acceptance is KEEP, but full command replay / presenter ownership remains pending.
- Record the current AC6 and Elden Ring visual-good evidence from Phase 9 Slice 7.
- Define the score model and evidence format.

Validation:

- Docs-only.
- No generated result artifacts committed.

Done when:

- One authoritative interleaved plan exists.
- Phase 9, Phase 10, and Phase 11 are no longer tracked as disconnected efforts.

## Slice C1 — POD command packet ABI + cache key schema + PE inventory

Touches: **P9.S1 / P10.S0 / P11.S0**

Status: complete when the C1 ABI/schema/inventory artifacts are committed and validated.

Tasks:

1. Define a C/POD command packet stream ABI in `libm12core`:
   - `M12CoreCommandPacketHeader`.
   - `M12CoreCommandPacket`.
   - `M12CoreCommandPacketStreamDesc`.
   - `M12CoreCommandPacketStreamSummary`.
   - `m12core_validate_command_packet_stream`.
2. Define a cache compatibility key schema in `libm12core`:
   - `M12CoreCacheCompatibilityDesc`.
   - `M12CoreCacheCompatibilityKey`.
   - `m12core_make_cache_compatibility_key`.
3. Add a PE responsibility inventory that classifies remaining PE-side work as:
   - Windows ABI / COM surface.
   - command serialization.
   - PE/native bridge transport.
   - renderer policy to migrate.
   - cache policy to migrate.
   - legacy fallback.
4. Add a synthetic native convergence probe for packet parsing and cache-key validation.

Validation:

- Runtime build passes.
- Formatting passes for touched C/C++/script files.
- Native convergence probe passes and emits JSON evidence.
- Probe build passes if public constants changed.
- No game launch required.

Done when:

- Packets and cache keys are scalar/POD.
- Packet parser rejects invalid streams and classifies valid synthetic graphics/compute/copy/present shapes.
- Cache key generation detects missing compatibility dimensions and changes when ABI/translator/profile/device dimensions change.
- PE thinning inventory exists and gives C2 concrete migration targets.

## Slice C2 — Shadow packet recording + shadow cache index

Touches: **P9.S1 / P11.S1-S2 shadow**

Tasks:

- PE records real command streams into POD packets.
- `libm12core` parses/validates packets shadow-only.
- Cache index records shader/PSO/root metadata shadow-only.
- No cache reuse yet.

Required metrics:

- `packet_streams_seen`.
- `packet_streams_valid`.
- `unsupported_packet_reason` counts.
- `cache_index_written`.
- `cache_index_corrupt=0`.

## Slice C3 — Native handle registry + cache identity binding

Touches: **P9.S2 / P11.S0-S1**

Tasks:

- Add native-side scalar handle IDs for resources, PSOs, root signatures, descriptor/binding plans, and swapchain images.
- Packets reference scalar native IDs.
- Cache entries bind to IDs only after compatibility validation.
- Add stale-handle and lifetime probes.

## Slice C4 — Probe-native replay executor + negative cache

Touches: **P9.S3 / P11.S5**

Tasks:

- Enable native replay for mini/probe packet streams behind `DXMT_M12CORE_REPLAY_EXECUTE=1`.
- Support clears, RT/DSV setup, barriers, viewport/scissor, PSO/root/bindings, draw/draw-indexed where fully supported.
- Add negative-cache entries for unsupported packet shapes.
- Whole-list fallback on unsupported packets.

## Slice C5 — Render-pass/encoder ownership + root/binding cache

Touches: **P9.S4 / P11.S3**

Tasks:

- Native core owns encoder open/close for native-executed lists.
- Native core validates render-pass compatibility.
- Cache root/binding plans.
- Cache hits never bypass resource-layout validation.

## Slice C6 — Native presenter ownership + thin transport boundary

Touches: **P9.S5 / P10.S3**

Tasks:

- Expand native present beyond narrow raw blit where safe.
- `winemetal.dll` becomes transport-oriented.
- `winemetal.so` owns native present sequencing.
- Prevent double-present and double-blit.

## Slice C7 — Cache-first warm start for shaders/PSOs

Touches: **P11.S1-S2/S4 + P9 metrics**

Tasks:

- Enable cache lookup before shader translation / PSO creation.
- Reuse only compatible entries.
- Prewarm queue skips already-satisfied work.
- Record skipped-work counters.

## Slice C8 — Expand native replay coverage while thinning PE

Touches: **P9.S6 / P10.S1-S2**

Tasks:

- Move more command classes to native packets.
- Reduce PE renderer policy.
- Keep PE COM ABI/object facade.
- Move DXGI/swapchain behavior native where proven.

## Slice C9 — Thin PE DLL checkpoint

Touches: **P10 finalization**

Tasks:

- Confirm final roles:
  - `d3d12.dll`: COM wrappers + command serialization.
  - `dxgi.dll`: bootstrap/wrapper.
  - `dxgi_dxmt.dll`: DXGI wrappers + bridge.
  - `winemetal.dll`: thunk transport.
  - `winemetal.so`: native loader.
  - `libm12core`: runtime/renderer/cache owner.
- Remove or quarantine obsolete PE renderer policy.

## Slice C10 — Final multi-game convergence validation

Touches: **P9/P10/P11 final**

Order:

1. AC6.
2. Elden Ring.
3. Subnautica 2 only after its separate compute/unsafe-draw investigation is ready.
4. PEAK/Schedule I later if M12 testing becomes relevant.

For each approved game:

- Dry-run.
- Gate-off bounded run.
- Gate-on bounded run.
- Cache-cold run.
- Cache-warm run.
- Visual confirmation when required.

Final proof requires measured native execution coverage, PE fallback parity, real warm-cache skipped work, invalidation proof, no visual regression, no unixcall failures, and no committed raw cache payloads.

## Current next slice

C0 is the saved roadmap/status reconciliation. C1 is the first implementation slice and must land before C2 attempts real command stream recording.
