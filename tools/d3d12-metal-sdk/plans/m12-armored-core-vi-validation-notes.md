# M12 Armored Core VI Validation Notes

Date: 2026-06-16
Branch: `fix/m12-shader-probe-lab`
Game: `ARMORED CORE VI FIRES OF RUBICON`
AppID: `1888160`

## Purpose

Armored Core VI is another FromSoftware D3D12 title and is now part of the M12 validation set alongside Elden Ring and Subnautica 2.

The user prepared the install by:

- downloading Armored Core VI,
- updating the executable setup so `start_protected_game.exe` launches the correct game path,
- saving an M12 bottle with Agility staged.

## Runtime setup

Dry-run succeeded for appid `1888160` with pipeline `m12`:

```text
ok=true
missing=[]
pipeline=m12
```

Validated source/game-local runtime hash:

```text
d3d12.dll sha256 = 2612e228a5efa9d65f6923b3ed1cc50b1c6ce40abb2c0043c51d32ec5b60dd7c
```

Game-local DLLs were already staged from the current validated Elden Ring/Subnautica M12 runtime set before launch.

## Harness additions

Added AC6 as a bounded M12 perf profile:

```text
tools/d3d12-metal-sdk/profiles/m12-perf/armored-core-vi.json
```

Added bounded-launch aliases:

```text
armored-core-vi
armoredcore6
ac6
```

Game directory:

```text
/Volumes/AverySSD/SteamLibrary/steamapps/common/ARMORED CORE VI FIRES OF RUBICON/Game
```

## First bounded M12 launch

Command shape:

```text
tools/d3d12-metal-sdk/scripts/m12-performance-run.sh \
  --profile armored-core-vi \
  --scenario smoke \
  --seconds 90 \
  --sample-interval-ms 250
```

Artifacts:

```text
tools/d3d12-metal-sdk/results/perf-runs/armored-core-vi-smoke-20260616-000500/perf-analysis.md
tools/d3d12-metal-sdk/results/perf-runs/armored-core-vi-smoke-20260616-000500/bounded-summary.md
tools/d3d12-metal-sdk/results/m12-translation-gauntlet/ac6-20260616-000734/translation-gauntlet.md
```

Summary:

```text
launch_ok=true
drawn/present=33/33
render_pso_failed=0
compute_pso_failed=0
dxil_msl_compile_failed=4
vertex_descriptor_missing=0
vs_ps_varying_mismatch=0
unsafe_draw_skips=0
new_msl_errors=4
new_fail_markers=0
graphics_pso_compiled=585
compute_pso_compiled=0
new_pso_render=462
new_shader_artifacts=1555
```

Cache inventory after first run:

```text
dxbc=538
msl=537
metallib=2
dxil_reports=537
msl_errors=4
fail_markers=0
pso_json=483
```

## Current AC6 translation blocker

The first AC6-specific blocker is not render PSO creation, vertex descriptors, or unsafe draw skipping. It is four pixel-shader MSL compile failures.

Failing shader hashes:

```text
41fd2491d01e720e
9617811bca5fd267
b3ef05b049200b01
e4d48771de31b32e
```

All four have `.dxbc`, `.msl`, and `.dxil_report.txt` artifacts in:

```text
~/.metalsharp/shader-cache/m12/1888160
```

Hard error class:

```text
error: call to 'ctz' is ambiguous
```

Recurring warning classes also present:

```text
warning: implicit conversion of out of range value from 'float' to 'int4' is undefined
warning: | has lower precedence than !=; != will be evaluated first
```

The warning classes should be cleaned up, but the immediate compile blocker is ambiguous `ctz(...)` overload resolution in generated MSL. This should be handled in the offline DXIL/MSL lowering path before more AC6 gameplay iteration.

## Recommended next steps

1. Add an AC6 focused offline repro set around the four failing shaders.
2. Fix DXIL/MSL lowering for ambiguous `ctz(...)` by emitting an explicitly typed argument/cast that selects the intended Metal overload.
3. Re-run scratch-only repro/gauntlet for the four AC6 failures.
4. Re-run AC6 bounded smoke and require:

```text
dxil_msl_compile_failed=0
new_msl_errors=0
render_pso_failed=0
vertex_descriptor_missing=0
```

5. Only after the translation blocker is closed, use AC6 as a FromSoftware wide-gap comparison against Elden Ring.

## Constraints

- Do not use this work to reopen DX11/Schedule/PEAK triage.
- Keep focus on M12 FromSoftware/Subnautica validation.
- Do not treat drawn/present counts as visual correctness.
- Preserve current validated M12 runtime hashes in every validation artifact.
