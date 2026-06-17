# M12 Remaining Issues Completion Plan

Last updated: 2026-06-16

## Scope

This plan covers the remaining M12 work after the offline CBV/SRV and true legacy DXBC hardening pass. It deliberately avoids new live game validation unless explicitly approved, because recent AC6 live runs correlated with WindowServer watchdog reports.

Primary goals:

1. Finish true legacy DXBC (`SHEX`/`SHDR`) hardening without regressing the clean DXIL-in-DXBC path.
2. Resolve AC6 final-output black/frozen symptoms using safer readback-first diagnostics.
3. Continue Subnautica 2 visual correctness after shader-lowering stability is preserved.
4. Keep full-runtime hash-gated validation and rollback safety intact.

## Current evidence baseline

### Offline true-DXBC corpus

Latest classified guarded run:

- Manifest: `tools/d3d12-metal-sdk/results/offline-corpora/cbv-srv-true-dxbc-pssig-diag-final/manifest.json`
- Summary:
  - `legacy_airconv_count`: `80`
  - `legacy_airconv_successes`: `68`
  - `legacy_airconv_failures`: `12`
  - `legacy_unhandled_opcode_count`: `5`
  - `legacy_failure_classes`:
    - `unsupported-pixel-signature-semantic`: `2`
    - `unsupported-legacy-double-op`: `6`
    - `unsupported-sm51-cbuffer-descriptor-index`: `2`
    - `requires-hs-ds-pair`: `2`
  - `legacy_opcode_names`:
    - `DMOV`: `1`
    - `DTOF`: `1`
    - `DFMA`: `1`
    - `DEQ`: `1`
    - `DDIV-or-double-reciprocal-family`: `2`
  - DXIL path remains clean:
    - `airconv_failures`: `0`
    - `metal_failures`: `0`
    - `audit_failures`: `0`

Earlier reference points:

- Initial true-DXBC pass: `49/80`
- After sparse/feedback + `check_access_fully_mapped`: `67/80`
- After guarded SM5.1 cbuffer operand support: `68/80`

### Implemented hardening

- `vendor/dxmt/src/airconv/dxbc_instructions.cpp`
  - WDDM1.3 sparse/feedback sample/load/gather opcodes route through existing IR and write the feedback-status destination as fully mapped (`~0u`) for the current non-sparse backend path.
  - `CHECK_ACCESS_FULLY_MAPPED` compares its status source operand against fully mapped (`~0u`) and emits the normal integer boolean result, avoiding unconditional true on stale/nonmapped status.
  - SM5.1 cbuffer source operands are accepted only when descriptor-array index is immediate zero.
  - Dynamic/nonzero SM5.1 cbuffer descriptor-array index is a hard reject to avoid silent CBV misbinding.
  - Unhandled DXBC opcodes are printed to stderr so failure indexing captures them before abort.
- `vendor/dxmt/src/d3d12/d3d12_command_queue.cpp`
  - Final snapshot / AC6 producer readback diagnostics now force command-buffer sync before reading/logging diagnostic buffers.
  - Generic texture readback diagnostics now use an explicit 4-byte uncompressed color-format whitelist before queuing Metal texture-to-buffer copies.
  - AC6 producer diagnostic now logs bounded, diagnostic-gated candidate final-size offscreen producer PSOs/resources when the exact old producer hash pair does not match; the final-size predicate now covers both the earlier 1472x956 title target class and observed 1920x1080 final resources.
  - AC6 CBV readback is clamped to backing buffer bounds, and mask-prime diagnostic restores render encoder state if its clear encoder cannot be created.
- `tools/d3d12-metal-sdk/scripts/build-offline-cbv-srv-corpus.py`
  - True-DXBC residuals now include `failure_class` and `opcode_name`, and summary class/opcode counters.

Latest reviewed build hashes after these source changes:

- `vendor/dxmt/build-metalsharp-x64/src/d3d12/d3d12.dll`: `4dcc067e3241f8efb5ab8dc0f3aa30534608ad432d61bb01a315f1e330ccb73e`
- `vendor/dxmt/build-metalsharp-x64/src/airconv/darwin/airconv`: `fcf09cb675312ebb3f1e23cbe209987cf56ecaa9a2e2dbc15271bae16e5b45c1`

Final autoreview for the current DXBC + diagnostic-sync changes returned no actionable findings.

## Remaining blocker classes and fix plan

### 1. Legacy DXBC double-precision opcodes

Current failures:

- `dxilconv-test-dxbc2dxil-double1.dxbc` opcode `210`
- `dxilconv-test-dxbc2dxil-double2.dxbc` opcode `210`
- `dxilconv-test-dxbc2dxil-double3.dxbc` opcode `195`
- `dxilconv-test-dxbc2dxil-double4.dxbc` opcode `211`
- `dxilconv-test-dxbc2dxil-double5.dxbc` opcode `201`
- `dxilconv-test-dxbc2dxil-double6.dxbc` opcode `199`

Likely opcode family:

- `195`: `DEQ`
- `199`: `DMOV`
- `201`: `DTOF`
- `210`: double division / reciprocal-class operation
- `211`: `DFMA`

Plan:

1. ✅ Add explicit opcode-name classification for these opcodes in the corpus failure index, so residual reports are semantic instead of numeric.
2. Inspect existing AIR/LLVM type support for `double` in `air_operations.hpp`, `dxbc_converter*.cpp`, and MSL/AIR output paths.
3. Choose one of two implementation strategies:
   - Preferred: true double IR support for read, move, compare, convert-to-float, div, and fma where Metal/AIR supports it on the target.
   - Conservative fallback: hard-reject legacy double ops with a named diagnostic instead of anonymous `unhandled dxbc instruction` aborts, if true double support is too broad for M12.
4. Add synthetic focused legacy-DXBC fixtures for each double opcode class before broadening beyond the imported DXC corpus.
5. Gate success with a true-DXBC corpus run. Required evidence:
   - double residual count decreases if true support is implemented, or
   - residuals become named/intentional if explicitly deferred.
   - DXIL path remains `airconv_failures=0`, `metal_failures=0`, `audit_failures=0`.

Do not fake double ops as float unless the source fixture proves the result is unused or the conversion is semantically equivalent.

### 2. SM5.1 cbuffer descriptor-array indexing

Current failures:

- `dxilconv-test-dxbc2dxil-cbuffer1.51.dxbc`
- `dxilconv-test-dxbc2dxil-cbuffer3.51.dxbc`

Current behavior:

- Immediate-zero descriptor-array index is supported.
- Dynamic/nonzero descriptor-array index is rejected with: `unsupported SM5.1 dynamic/nonzero cbuffer descriptor index`.

Why not complete yet:

- The legacy backend currently binds constant buffers by `rangeid`; accepting nonzero/dynamic descriptor-array indices would silently bind the wrong CBV.

Plan:

1. Trace `SrcOperandConstantBuffer.rangeindex` through `LoadOperand` / `cb_range_map` / argument binding.
2. Define how SM5.1 `cbuffer[descriptor_index][row]` maps onto Metal buffer arguments:
   - either descriptor-array buffer table support, or
   - expanded per-range argument mapping with dynamic selection.
3. Implement descriptor-indexed CBV loads only after binding metadata can select the correct CBV.
4. Add a focused fixture with at least two CBVs in the same SM5.1 range and a nonzero descriptor index; verify row values come from the selected CBV.
5. Gate with corpus plus MSL audit; success requires these two failures to pass without introducing `*64` CBV row stride regressions.

### 3. Pixel shader signature gaps

Current failures:

- `dxilconv-test-dxbc2dxil-input2.dxbc`
- `dxilconv-test-dxbc2dxil-output3.dxbc`

Current evidence:

- Both abort in `handle_signature_ps(...)` in `vendor/dxmt/src/airconv/dxbc_signature.cpp`.
- They are not CBV/SRV lowering failures.

Plan:

1. ✅ Add stderr diagnostics around each remaining `handle_signature_ps` assert to identify the exact unsupported declaration/semantic before abort.
2. ✅ Compare the DXC source fixtures and parsed signatures to determine the missing cases:
   - `input2.hlsl`: `SV_InnerCoverage` input
   - `output3.hlsl`: `SV_StencilRef` output
3. Implement only semantics with a clear Metal mapping.
4. For semantics without Metal mapping, replace generic asserts with named unsupported diagnostics and keep them indexed as intentional residuals.
5. Gate with true-DXBC corpus; success is either conversion pass or named unsupported classification.

### 4. Hull/domain shader independent conversion limitation

Current failures:

- `dxilconv-test-regression_tests-fork_instanceid_with_modifiers.dxbc`
- `dxilconv-test-regression_tests-fork_instanceid_with_modifiers.DX12.dxbc`

Current behavior:

- `airconv` exits with: `Hull and domain shader cannot be independently converted.`

Plan:

1. Keep these out of the generic true-DXBC pass/fail score unless the corpus runner can provide paired HS/DS inputs.
2. ✅ Extend the corpus manifest to classify them as `requires-hs-ds-pair` rather than generic legacy failures.
3. If tessellation becomes an M12 target, add paired conversion support and a dedicated tessellation corpus gate.

### 5. AC6 final black / frozen output

Current evidence:

- Final shader/backbuffer/presenter can output visible nonblack when forced/primed.
- Controlled approved readback-only runs were captured under strict full-runtime hash gates:
  - `tools/d3d12-metal-sdk/results/perf-runs/armored-core-vi-smoke-20260616-155224/`
  - `tools/d3d12-metal-sdk/results/perf-runs/armored-core-vi-smoke-20260616-155446/`
  - `tools/d3d12-metal-sdk/results/perf-runs/armored-core-vi-smoke-20260616-160556/`
  - `tools/d3d12-metal-sdk/results/perf-runs/armored-core-vi-smoke-20260616-161755/`
  - summaries:
    - `tools/d3d12-metal-sdk/results/ac6-readback-diagnostic-summary-20260616-155446.md`
    - `tools/d3d12-metal-sdk/results/ac6-producer-chain-readback-summary-20260616-161755.md`
  - candidate run `160556` found final resources at `1920x1080`, so the candidate predicate was widened after the run.
  - widened/gated run `161755` produced bounded candidate and exact producer logs without lingering game/backend processes or fresh WindowServer reports.
- AC6 final `t1` is zero/black; `t0` is alpha-only.
- Final `t1` producer shaders are:
  - VS `ca33abe9a2d27ce9` = `TitleTexVs`
  - PS `58539be4844b1dd9` = `ChromaticAberrationAndNoisePs`
- CBV row stride fix is proven offline and regenerated AC6 MSL uses `*16`, but AC6 RGB remains zero.
- Producer readback diagnostics now force sync when enabled.
- Latest readback-backed narrowing:
  - exact final `t1` producer fires under the widened 1920x1080 predicate;
  - `M12 AC6 producer cbv readback queued=1` is present;
  - producer state/geometry looks valid;
  - producer output RTV remains zero;
  - producer `srv0` input is alpha-only in early frames and fully zero in later frames;
  - producer `srv0` resource (`res=0x13a6bbd0`, `tex_id=60`) is cleared black and then written by upstream `DrawIndexedInstanced` with VS `42dbf5610021bd23` and PS `6aaa91c23c794ed8`.
- Source now includes an opt-in upstream readback diagnostic for that upstream PSO:
  - captures upstream state, vertex-visible CBV0, pixel SRVs 0-3, and RTV readback;
  - keeps destructive mask-prime behavior gated behind `DXMT_D3D12_AC6_PRODUCER_DIAGNOSTIC` as well as `DXMT_D3D12_AC6_PRIME_FINAL_MASK`;
  - reviewed clean by autoreview;
  - rebuilt `d3d12.dll` hash: `9256c012e68ecce8caca5d1e2aea5514ba4a63bdf99d79f0ed812711fc3a07b1`;
  - staged to M12 runtime/canonical Wine runtime/prefix/game-local surfaces without launching AC6;
  - strict explicit hash gate passed for M12 runtime + AC6 game-local:
    - `tools/d3d12-metal-sdk/results/verify-m12-runtime-ac6-upstream-diagnostic-explicit-20260616-163347.json`
    - `tools/d3d12-metal-sdk/results/verify-m12-runtime-ac6-upstream-diagnostic-explicit-20260616-163347.md`
  - full-surface core translation hash manifest passed:
    - `tools/d3d12-metal-sdk/results/full-surface-hashes-ac6-upstream-diagnostic-20260616-163347.json`
    - `tools/d3d12-metal-sdk/results/full-surface-hashes-ac6-upstream-diagnostic-20260616-163347.md`.
- Approved run `tools/d3d12-metal-sdk/results/perf-runs/armored-core-vi-smoke-20260616-163818/` launched successfully with strict hashes and no fresh WindowServer report, but did not emit upstream diagnostic logs because the installed backend did not receive per-launch diagnostic env overrides.
- Approved run `tools/d3d12-metal-sdk/results/perf-runs/armored-core-vi-smoke-20260616-175100/` used the rebuilt backend/request `envOverrides` path successfully and emitted upstream diagnostics:
  - summary: `tools/d3d12-metal-sdk/results/ac6-upstream-diagnostic-summary-20260616-175100.md`
  - strict hashes passed and no fresh WindowServer report appeared;
  - upstream PSO `VS 42dbf5610021bd23` / `PS 6aaa91c23c794ed8` fired 57 times;
  - upstream state is not obviously suppressing writes: 1920x1080 viewport/scissor, depth/stencil disabled, write mask `0xf`; blend is enabled (`src0=5`, `dst0=6`);
  - upstream RTV remains all zero;
  - descriptor-table probe reports upstream `vs_cbv0 desc=null` and SRVs null even though draw summary reports `vs_cb=1`, so the next diagnostic must inspect root CBV/root constants/direct bindings;
  - final `t0` remains alpha-only, final `t1` zero, swapchain zero;
  - the run also shows `dxil_msl_compile_failed=3` with ambiguous Metal `ctz` errors, which became a separate offline lowering target.
- Offline DXIL bit-scan lowering fix is now implemented and review-clean:
  - explicit integer casts for `countbits`, `reverse_bits`, `firstbit*` operands;
  - corrected DXIL `firstbit` semantics (`-1` for zero/no differing sign bit; high-bit index as `31 - clz(...)`);
  - fixed the MSL emitter assignment heuristic so comparison expressions containing `==` are still assigned to their result value instead of emitted as bare statements;
  - build passed for `src/airconv/darwin/airconv` and `src/d3d12/d3d12.dll`;
  - new built hashes:
    - `d3d12.dll`: `9d303582ec525270dc927172138fc8cd1b64d8a2073b5c8d7d6c5c68f7816169`
    - `airconv`: `6ebf7b30b18e31372a846beae5d47cd6d85d6af8df2622e76cc0ad9d12367d53`
  - focused autoreview returned no findings;
  - read-only offline validation regenerated/compiled the three AC6 `ctz` failure shaders without mutating live shader cache:
    - `tools/d3d12-metal-sdk/results/ac6-ctz-semantic-fix-20260616-181308/summary.md`.
  - staged `d3d12.dll` to required M12/canonical/prefix/AC6 game-local surfaces and re-verified:
    - `tools/d3d12-metal-sdk/results/verify-m12-runtime-ac6-root-ctz-explicit-20260616-181701.json`
    - `tools/d3d12-metal-sdk/results/verify-m12-runtime-ac6-root-ctz-explicit-20260616-181701.md`
    - `tools/d3d12-metal-sdk/results/full-surface-hashes-ac6-root-ctz-20260616-181701.json`
    - `tools/d3d12-metal-sdk/results/full-surface-hashes-ac6-root-ctz-20260616-181701.md`.
- Source now includes a root-binding-aware upstream diagnostic for the same upstream PSO:
  - logs upstream root signature parameters, root CBV/root constants/root descriptor state;
  - queues a readback from the first vertex-visible root CBV when present;
  - reviewed clean by autoreview;
  - superseded diagnostic+ctz `d3d12.dll` hash is now `9d303582ec525270dc927172138fc8cd1b64d8a2073b5c8d7d6c5c68f7816169` (earlier root-only hash was `38a2328d5e917d71e49be01f083a74b452b2c9b4a75ec8418d819fb52a3a659a`).
- Goldberg-backed AC6 root diagnostic run completed:
  - run dir: `tools/d3d12-metal-sdk/results/perf-runs/armored-core-vi-root-ctz-goldberg-20260616-181945/`
  - summary: `tools/d3d12-metal-sdk/results/ac6-root-ctz-goldberg-summary-20260616-181945.md`
  - no fresh WindowServer reports;
  - `launch_ok=True`, `present_count=22`, `drawn_present_count=22`;
  - `dxil_msl_compile_failed=0`, `render_pso_failed=0`;
  - no new MSL/metallib artifacts, so this live run did not intentionally refresh live shader source/metallib caches;
  - Goldberg did not resolve black output: upstream RTV for `tex_id=60` remains all zero.
- New AC6 root-cause narrowing from Goldberg run:
  - upstream PSO is confirmed by manifest `~/.metalsharp/shader-cache/m12/1888160/pso-render-4242bbe67d38d967.json` as VS `42dbf5610021bd23` / PS `6aaa91c23c794ed8`;
  - OM state permits color writes (`depth_enable=0`, `stencil_enable=0`, `blend0=1`, `write_mask0=0xf`), so depth/stencil/write mask are not suppressing the zero RTV;
  - VS `cb0` is a root CBV, not a descriptor-table CBV; readback has small nonzero rows 0/1 and zeros afterward;
  - PS has root CBV `b1` plus two PS descriptor tables; current hard-coded SRV probe reports SRV 0-3 null, which is insufficient because it does not walk the PS root descriptor-table ranges.
- Harness/backend fix is now implemented and build-validated:
  - `/steam/launch-game` accepts a whitelisted `envOverrides` JSON object and translates request controls into game-process `DXMT_*` variables.
  - `m12-bounded-launch.sh` now sends the same bounded diagnostic/worker/async/source-compile controls in the request body instead of relying on `env curl`.
  - Diagnostic capture safety wins over live-present overrides, including inherited diagnostic capture.
  - `cargo build` for `app/src-rust` passes.
  - Autoreview of the env override path is clean.
- DXBC sparse-feedback hardening is now corrected:
  - sparse sample/load/gather feedback destinations are written to fully-mapped status (`~0u`) instead of remaining stale;
  - `CHECK_ACCESS_FULLY_MAPPED` now compares its status input to `~0u` instead of returning unconditional true;
  - `ninja -C vendor/dxmt/build-metalsharp-x64 src/airconv/darwin/airconv` passes;
  - focused autoreview is clean;
  - DXIL-only CBV/SRV corpus is clean at `tools/d3d12-metal-sdk/results/offline-corpora/cbv-srv-feedback-status-check-20260616-170941/manifest.json`, but reports `ok=false` because no true SHEX/SHDR samples were included;
  - full true-DXBC corpus reruns with public legacy samples timed out before manifest write and still need a longer/async validation run.

Plan before any live run:

1. Keep diagnostics disabled by default.
2. Build/stage full runtime surface only under strict hash gates.
3. For a short approved diagnostic capture, keep:
   - `DXMT_D3D12_LIVE_PRESENT=0`
   - `DXMT_D3D12_AUTOPRESENT_SWAPCHAIN=0`
   - `DXMT_D3D12_REASSERT_WINDOW_HANDOFF=0`
4. Enable only the needed readback diagnostics:
   - `DXMT_D3D12_AC6_PRODUCER_DIAGNOSTIC=1`
   - optionally final snapshot/readback diagnostics.
5. The exact producer diagnostic trigger now fires; candidate logging proved the active 1920x1080 resource chain.
6. Next root-cause target is the upstream draw that writes producer `srv0` (`res=0x13a6bbd0`, `tex_id=60`): VS `42dbf5610021bd23`, PS `6aaa91c23c794ed8`, indexed draw count `60`.
7. Required evidence for the next approved live diagnostic increment:
   - upstream draw PSO state, RTV/DSV state, and vertex/index summary;
   - upstream CBV/SRV descriptors and readbacks;
   - RTV readback immediately after that draw;
   - whether depth/stencil/blend/write-mask state can suppress RGB writes.
8. If upstream inputs are nonzero but RTV remains zero, inspect shader lowering/state translation for `42dbf5610021bd23` / `6aaa91c23c794ed8`.
9. If upstream inputs are already zero, repeat one pass farther upstream instead of changing final composite or `ChromaticAberrationAndNoisePs`.
10. Treat post-load freeze/input as a separate state/input/window issue after final-output producer state is known.

### 6. Subnautica 2 visual correctness

Current evidence:

- Atomic predecl typing fix removed DXIL/Metal compile failures and unsafe skips.
- Raised window remains black.

Plan:

1. Reuse the offline corpus to keep atomics/CBV/SRV lowering stable.
2. Avoid live runs until explicitly approved after WindowServer crash risk.
3. When approved, use short full-runtime hash-gated capture with live presentation disabled by default.
4. Prefer readbacks and deterministic probes over visual window state.
5. Classify whether Subnautica’s black output is:
   - final composite input zero,
   - producer render target zero,
   - PSO/shader compile/cache issue,
   - barrier/resource state issue,
   - or swapchain/window handoff.

## Validation gates

For every source change in this plan:

1. Rebuild the affected target.
   - `ninja -C vendor/dxmt/build-metalsharp-x64 src/airconv/darwin/airconv`
   - plus `src/d3d12/d3d12.dll` when runtime sources changed.
2. Run the offline CBV/SRV/true-DXBC corpus without mutating live shader caches.
3. Confirm DXIL path remains clean:
   - `airconv_failures=0`
   - `metal_failures=0`
   - `audit_failures=0`
4. Confirm true-DXBC residuals either decrease or are better classified with intentional named unsupported reasons.
5. Run `autoreview` on changed source/scripts and fix actionable findings.
6. For any game validation, use full-runtime strict hash gates across the entire M12 translation surface, not just `d3d12.dll`.

## Definition of done

This remaining-issues thread is complete when:

- True-DXBC residuals are either converted or explicitly classified as intentional unsupported classes.
- ✅ No current true-DXBC failure is an anonymous/unindexed abort in `cbv-srv-true-dxbc-pssig-diag-final`; every residual has a `failure_class`, and PS signature residuals name `SV_InnerCoverage` / `SV_StencilRef` in stderr.
- DXIL CBV/SRV path remains clean under corpus replay and CBV stride audit.
- AC6 has a readback-backed root cause for producer RGB zero, or a validated fix, without relying on present/draw counts.
- Subnautica 2 has a classified black-output stage or a validated fix.
- All runtime/game validation uses full-surface hash gates and rollback-safe staging.
- Temporary diagnostic changes are either committed as gated tooling or reverted.
