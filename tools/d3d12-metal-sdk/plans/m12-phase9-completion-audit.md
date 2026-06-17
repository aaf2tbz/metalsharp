# M12/libm12core Phase 9 Completion Audit

## Scope completed in this goal

- Added native-core present planning/classification (`M12CORE_FEATURE_PRESENT_PLANNING`) behind unixcall `153`.
- Added native-core replay planning/classification (`M12CORE_FEATURE_REPLAY_PLANNING`) behind unixcall `154`.
- Kept actual D3D12 command replay, Metal encoder lifetime, drawable acquisition, command-buffer commits, synchronization, and resource hazards on the existing PE/DXMT execution path.
- Exposed the Phase 9 feature surface through `IMetalSharpM12TranslationLayerInfo` with `M12CORE_BUILD_ID_HIGH = 0x0000000f`.
- Staged the rebuilt runtime and verified backend dry-run readiness for every target game profile.

## Ground rules for completing Phase 9 fully and safely

1. Keep `libm12core` ABI C/POD-only: no STL, C++ objects, COM interfaces, Objective-C/Metal objects, exceptions, virtuals, or ownership semantics cross the PE/native boundary.
2. Move ownership in monotonic slices: detect/plan -> key/classify -> validate -> shadow execute -> gated execute -> default execute. Never jump directly to full execution.
3. Every slice must have a PE/DXMT fallback when `libm12core` is disabled, missing, older, or returns invalid output.
4. Execution migration must be behind an opt-in env/profile gate until bounded launches prove it per game.
5. Dry-run comes before launch for every game. No launch occurred during this goal.
6. Preserve live shader caches unless explicitly validating source-compile fixes.
7. Do not commit raw D3DMetal cache payloads, extracted metallibs, DXBC blobs, or large corpora.
8. D3DMetal remains oracle-only until ABI/resource-layout compatibility is proven.
9. Any future execution migration must prove encoder lifetime, render-pass boundaries, drawable ownership, synchronization, and hazards independently before becoming default.

## Why this is a positive/certain Phase 9 stopping point

- The new native-core work is additive diagnostics/planning only; it cannot alter command execution or presentation behavior.
- Autoreview found no actionable issues in ABI safety, unixcall ordering, fallback behavior, or execution-semantics isolation.
- Runtime build and probe build passed after formatting.
- Backend dry-run confirms all required DLLs/sidecars/env are present for each game profile.

## Validation evidence

- Runtime build: `./tools/d3d12-metal-sdk/scripts/m12-dev.sh build-runtime` passed.
- Probe build: `./tools/d3d12-metal-sdk/scripts/build-probes.sh` passed.
- Formatting: `clang-format --dry-run --Werror ...` passed for touched C/C++/Obj-C files.
- Autoreview: clean (`[]`) for Phase 9 replay-planning diff.
- Runtime staged: `./tools/d3d12-metal-sdk/scripts/m12-dev.sh stage-runtime` -> `tools/d3d12-metal-sdk/results/stage-runtime-metalsharp.json`.
- Dry-run matrix: `tools/d3d12-metal-sdk/results/m12-phase9-final-dryrun-20260617-154028/summary.json`.

## Game dry-run matrix

| Profile | AppID | Result | Artifact |
|---|---:|---|---|
| elden-ring | 1245620 | PASS (`ok=true`, `pipeline=m12`, `missing=[]`) | `tools/d3d12-metal-sdk/results/m12-phase9-final-dryrun-20260617-154028/dry-run-elden-ring-1245620.json` |
| subnautica2 | 1962700 | PASS (`ok=true`, `pipeline=m12`, `missing=[]`) | `tools/d3d12-metal-sdk/results/m12-phase9-final-dryrun-20260617-154028/dry-run-subnautica2-1962700.json` |
| armored-core-vi | 1888160 | PASS (`ok=true`, `pipeline=m12`, `missing=[]`) | `tools/d3d12-metal-sdk/results/m12-phase9-final-dryrun-20260617-154028/dry-run-armored-core-vi-1888160.json` |
| schedule-1 | 3164500 | PASS (`ok=true`, `pipeline=m12`, `missing=[]`) | `tools/d3d12-metal-sdk/results/m12-phase9-final-dryrun-20260617-154028/dry-run-schedule-1-3164500.json` |
| peak | 3527290 | PASS (`ok=true`, `pipeline=m12`, `missing=[]`) | `tools/d3d12-metal-sdk/results/m12-phase9-final-dryrun-20260617-154028/dry-run-peak-3527290.json` |

## Current commits

- `0fec647 feat: plan m12 presents in native core`
- `b4754d4 feat: plan m12 command replays in native core`
- Audit written at HEAD `b4754d4`

## Deferred full execution migration checklist

These are the remaining slices for actual core-owned execution, and each requires its own review/build/dry-run/bounded-launch gate:

1. Define opaque native handles for core-owned command/presenter objects without leaking PE C++/COM/Metal handles across ABI.
2. Shadow-validate command-stream descriptors in `libm12core` while PE still executes.
3. Shadow-validate render-pass boundary decisions against PE encoder decisions.
4. Add opt-in gated native presenter/blit execution for one narrow path with PE fallback before acquire/commit.
5. Add opt-in gated native command replay for one mini/probe path only, then bounded game launch one title at a time.
6. Promote only after logs show no unixcall failures, no validation drift, no missing resources, and no visual/regression failures.

## Completion status

- Phase 9 planning/classification ownership is complete and staged.
- All five game profiles are staged and dry-run verified.
- No game launch was performed in this goal.
- Next step after goal completion: wait for explicit user instruction for bounded launches one by one.
