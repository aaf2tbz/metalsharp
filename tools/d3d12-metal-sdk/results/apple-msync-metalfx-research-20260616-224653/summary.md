# Apple m-sync + MetalFX upscaling research note

- Output: `tools/d3d12-metal-sdk/results/apple-msync-metalfx-research-20260616-224653`
- Source manifest: `source-urls.tsv`
- Scope: focused follow-up to the Apple performance-docs pass, aimed at Wine/Metal synchronization (`m-sync`) and MetalFX upscaling implications for MetalSharp, M12/DXMT, and GPTK/D3DMetal.

## Executive decision

1. Keep `WINEMSYNC=1` as the current MetalSharp default, but add logging and a bounded override path before treating it as a measured performance knob.
2. Do not enable MetalFX during correctness validation. It can hide base-render failures by post-processing the final image.
3. Add GPTK/D3DMetal MetalFX as an opt-in profile flag only: `METALSHARP_GPTK_METALFX=1 -> D3DM_ENABLE_METALFX=1`.
4. For M12/D3D12, treat MetalFX spatial upscaling as a later presenter feature, not a shader-translation fix. Temporal MetalFX is only viable when trustworthy color, depth, motion vectors, jitter, reset, exposure, and resource synchronization are available.

## What “m-sync” means here

This note covers two related but distinct layers:

- Wine-side synchronization: MetalSharp currently injects `WINEMSYNC=1` in launch paths (`app/src-rust/src/mtsp/launcher.rs`). This likely aims at the GPTK/CrossOver-style macOS Wine synchronization backend, but this pass did not prove the shipped Wine binary consumes it; runtime logging/probing should verify actual behavior.
- Metal-side synchronization: Apple’s Metal APIs provide `MTLFence`, `MTLEvent`, and `MTLSharedEvent` for GPU pass dependencies and CPU/GPU or cross-process coordination. These should not be conflated with `WINEMSYNC`; they are useful for diagnosing command-buffer stalls and for making MetalFX resources safe.

## Apple synchronization findings

- `MTLResourceHazardTrackingModeUntracked` requires manual encoder dependencies with `MTLFence`; heap/suballocated resources commonly need explicit care.
- `MTLRenderCommandEncoder` supports stage-scoped `updateFence:afterStages:` and `waitForFence:beforeStages:`. Apple’s header notes these can allow overlap compared with whole-encoder waits.
- `MTLSharedEvent` provides a timeline value, async listener notifications, synchronous waits with timeout, and shareable handles for cross-process/device use.
- Resource usage declarations are not hazard barriers. Apple headers repeatedly note that `useResource`/heap declarations do not protect against data hazards; fences/events are the dependency mechanism.

### M12 implication

Add lightweight wait/stall instrumentation before changing sync policy:

- Launch log: effective `WINEMSYNC`, `WINEDEBUG`, backend, route, and whether a user override disabled/enabled sync.
- Runtime trace: present `frame_requested_`/`frame_presented_` wait duration in `dxmt_presenter.cpp`.
- Runtime trace: command queue/list waits, Metal fence waits, Metal event waits/signals, and command-buffer completion latency.
- Bounded-run summary: sync waits p50/p95/max and whether waits correlate with low present rate.

## Apple MetalFX findings

- MetalFX is designed to save GPU time by rendering lower-resolution content and upscaling to the final resolution.
- Apple distinguishes temporal upscaling and spatial upscaling:
  - Temporal: color + depth + motion inputs over time; better candidate for game-render integration, but requires correct motion/jitter/depth/reset semantics.
  - Spatial: color-only input; more realistic for a generic swapchain/presenter opt-in.
- Effects should be created at launch or display-resolution changes and then reused.
- `requiresSynchronousInitialization` lets callers trade creation latency for avoiding slower first-use/background compilation behavior.
- MetalFX scalers expose minimum `MTLTextureUsage` bits; callers must create compatible textures. Output textures are expected to be private storage.
- Both spatial and temporal scaler protocols expose an optional `MTLFence` for untracked resources.
- Temporal scaler details that matter for correctness: dynamic-resolution min/max, depth reversed flag, motion vector scale, jitter offsets, reset, exposure/pre-exposure, and reactive mask.

## Current project state

- `WINEMSYNC=1` is already injected in multiple launch paths and env-preview path in `app/src-rust/src/mtsp/launcher.rs`.
- M12/D3D12 presenter currently uses custom `present_blit_` / `present_scale_` render pipelines in `vendor/dxmt/src/dxmt/dxmt_presenter.cpp`.
- DXMT/winemetal already has MetalFX bridge APIs in `vendor/dxmt/src/winemetal/unix/winemetal_unix.c`.
- DXMT D3D11 already has:
  - opt-in spatial swapchain path via `DXMT_METALFX_SPATIAL_SWAPCHAIN`.
  - temporal scaler support in an extension path with scaler caching and support checks.
- GPTK/D3DMetal inventory found `D3DM_ENABLE_METALFX`; keep it opt-in/profile-only.

## Implementation plan

### Phase S0 — sync observability only

- Add a launch-log line for effective sync env: `WINEMSYNC`, any future `METALSHARP_WINE_MSYNC`, and route backend.
- Add bounded-run parsing for sync-mode and wait summaries.
- Add present wait timing around `frame_presented_.wait(frame_requested_)`.
- Do not change default behavior in this phase.

### Phase S1 — controlled sync override

- Add one explicit opt-out for experiments, e.g. `METALSHARP_DISABLE_WINEMSYNC=1`, which omits `WINEMSYNC` or sets it to `0` in all launch paths.
- Gate through profile/harness allowlist, not global config.
- Compare AC6/Elden/Subnautica with 120s bounded runs only after instrumentation exists.

### Phase F0 — GPTK/D3DMetal MetalFX opt-in

- Add `METALSHARP_GPTK_METALFX=1 -> D3DM_ENABLE_METALFX=1` only for GPTK/D3DMetal nodes.
- Record the flag in launch logs and perf summaries.
- Do not combine with correctness claims; use separate performance-only profiles.

### Phase F1 — M12 spatial scaler prototype

- Add M12 capability logging for `supportsFXSpatialScaler()` and `supportsFXTemporalScaler()`.
- Prototype a spatial-scaler presenter path behind `METALSHARP_M12_METALFX_SPATIAL=1` / `DXMT_D3D12_METALFX_SPATIAL=1`.
- Require descriptor-compatible formats, dimensions, private output texture, required usage bits, and fence handling.
- Fall back to the existing `present_scale_` shader path on any unsupported case.

### Phase F2 — M12 temporal scaler only if data is trustworthy

- Do not add generic temporal upscaling to D3D12 presenter without a real source for depth, motion vectors, jitter, reset, and exposure.
- If a title/API path exposes those inputs, use Apple’s descriptor fields exactly and log all semantic flags.
- Treat temporal MetalFX as a separate visual-quality feature, not as part of M12 correctness validation.

## Validation matrix

| Feature | Default | First validation | Success signal |
|---|---:|---|---|
| `WINEMSYNC=1` observability | on | no behavior change | logs show effective sync env and wait counters |
| `METALSHARP_DISABLE_WINEMSYNC=1` | off | bounded A/B run | equal correctness, lower wait/present stalls if beneficial |
| `METALSHARP_GPTK_METALFX=1` | off | GPTK/D3DMetal performance profile | no launch regression; FPS/frame pacing improvement if any |
| `METALSHARP_M12_METALFX_SPATIAL=1` | off | probe + one bounded game | fallback-safe, no blank output, present scaling works |
| M12 temporal MetalFX | off | only with trustworthy motion/depth source | no ghosting/incorrect history; quality/perf win |

## Risks / non-goals

- MetalFX can mask black-output or bad-binding bugs; keep disabled for AC6/Elden/Subnautica correctness gates.
- High-resolution temporal inputs are not automatically available in translated D3D12 games.
- `D3DM_ENABLE_METALFX` belongs to GPTK/D3DMetal and should not be forwarded to M12/DXMT.
- `WINEMSYNC` is a Wine/runtime knob; Metal fences/events are GPU synchronization primitives. Measure them separately.
