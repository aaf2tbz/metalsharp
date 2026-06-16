# M12 Offline Translation Completion Roadmap

Date: 2026-06-15
Branch: `fix/m12-shader-probe-lab`

## Purpose

M12 has reached an important point: simple bounded launch smoke tests can pass, but real gameplay scenarios still expose deeper translation and runtime correctness gaps.

Current interactive findings:

```text
Elden Ring:
  launches and renders early flow
  reaches character creation
  then stays open/rendered but hangs and cannot progress past that screen

Subnautica 2:
  launches into a game window
  the process/window is active
  presents/draws can be counted
  but visible pixels are not produced correctly
  recent smoke showed 21 compute DXIL/MSL compile failures
```

Conclusion: do not continue relying primarily on repeated game launches. Build an offline and semi-offline completion gauntlet that systematically expands DXIL/D3D12/DXGI coverage before long gameplay testing.

## Frozen known-good runtime baseline

Current working current-source runtime baseline:

```text
d3d12.dll sha256 = 8cdcec40588018dafaa3cdd1cfb140c1fd7edba6f1160cd7559d61be8b946500
preserved_runtime = /Volumes/AverySSD/MetalSharp-M12-Preserved/working-current-source-runtime-elden-ring-20260615-220308
```

This baseline must remain recoverable.

Rules:

1. Never overwrite the preserved runtime snapshot.
2. Do not restage experimental DXMT builds over the working runtime unless that task is explicitly about runtime validation.
3. Any restage must preserve both:

```text
~/.metalsharp/runtime/wine/lib/dxmt_m12
<game-dir>/*.dll deployed M12 DLLs
```

4. Any rollback must restore both runtime DLLs and game-local DLLs.
5. Every test artifact must record runtime and game-local DLL hashes.

## Strategy shift

Move from:

```text
launch game -> observe one symptom -> patch runtime -> repeat
```

to:

```text
collect translation inputs -> run offline gauntlet -> classify gaps -> fix one class -> verify no regressions -> only then run scenario gameplay
```

## Roadmap overview

Phases:

1. Freeze and verify baselines.
2. Build offline corpus inventory.
3. Fetch/install compiler/runtime references.
4. Build DXIL/MSL translation gauntlet.
5. Build D3D12/DXGI behavior probe gauntlet.
6. Build visual/headless output gauntlet.
7. Fix translation classes one at a time.
8. Add cache/PSO persistence and pressure tests.
9. Return to controlled multi-game scenario testing.

Each phase has explicit acceptance criteria and no-regression gates.

---

# Phase 0 — Baseline freeze and recovery contract

## Goal

Ensure we can always return to the known-good M12 runtime and current game-local deployed DLL state.

## Tasks

- Verify preserved runtime exists:

```text
/Volumes/AverySSD/MetalSharp-M12-Preserved/working-current-source-runtime-elden-ring-20260615-220308
```

- Add/extend a restore script that restores both runtime and selected game-local DLLs.
- Add a hash checker for:

```text
runtime dxmt_m12 DLLs
Elden Ring game-local DLLs
Subnautica 2 game-local DLLs
Schedule I game-local DLLs
PEAK game-local DLLs
```

- Make the hash checker fail loudly if a test uses stale or unexpected DLLs.

## Acceptance criteria

```text
restore-known-working-current-m12-runtime.sh exists
hash verification passes for Elden Ring baseline
hash verification records Subnautica 2/Schedule I/PEAK game-local state when present
```

## No-regression gate

Run only dry-run/preflight checks, no gameplay required:

```text
/diagnostics/m12/dry-run?appid=1245620
/diagnostics/m12/dry-run?appid=1962700
/diagnostics/m12/dry-run?appid=3164500
/diagnostics/m12/dry-run?appid=3527290
```

Required:

```text
ok=true
missing=[]
DXMT_D3D12_PSO_WORKERS=1
DXMT_ASYNC_PIPELINE_COMPILE=1
d3d12.dll present
```

---

# Phase 1 — Corpus inventory and failure index

## Goal

Create a complete inventory of available offline translation inputs before adding new fixes.

## Inputs

Known game/runtime corpora:

```text
~/.metalsharp/shader-cache/m12/1245620        # Elden Ring live cache
~/.metalsharp/shader-cache/m12/1962700        # Subnautica 2 live cache
~/.metalsharp/shader-cache/m12/3164500        # Schedule I, if populated
~/.metalsharp/shader-cache/m12/3527290        # PEAK, if populated
/Volumes/AverySSD/MetalSharp-M12-CorpusLab/elden-ring-scratch/stable-20260615-192733
```

Preserved artifacts:

```text
tools/d3d12-metal-sdk/results/perf-runs
tools/d3d12-metal-sdk/results/live-captures
tools/d3d12-metal-sdk/results/bounded-launches
```

## Tasks

Create:

```text
tools/d3d12-metal-sdk/scripts/m12-corpus-inventory.py
```

It should report:

```text
number of dxbc blobs
number of msl files
number of dxil reports
number of msl error files
number of pso-render manifests
number of pso-compute manifests
unique shader hashes
unique PSO hashes
top error categories
which game/profile each artifact came from
```

Output:

```text
tools/d3d12-metal-sdk/results/m12-translation-gauntlet/corpus-inventory.json
tools/d3d12-metal-sdk/results/m12-translation-gauntlet/corpus-inventory.md
```

## Acceptance criteria

- Inventory runs without mutating live caches.
- Elden Ring and Subnautica 2 caches are indexed.
- Subnautica 2 21 compute MSL failures are discoverable and classified.

## No-regression gate

No runtime changes in this phase.

---

# Phase 2 — Reference runtime/compiler acquisition

## Goal

Install or download broad DirectX/D3D12 reference payloads so offline probes can cover more areas than current game launches.

## Targets

D3D12 Agility SDK versions:

```text
1.4.10
1.600.10
1.602.4
1.606.4
1.608.3
1.610.4
1.611.2
1.613.3
1.614.1
1.615.1
1.616.1
1.618.5
1.619.3
1.700.10-preview
1.706.4-preview
1.710.0-preview
1.711.3-preview
1.714.0-preview
1.715.0-preview
1.716.1-preview
1.717.1-preview
1.719.1-preview
1.720.0-preview
1.721.0-preview
```

DXC / shader compiler references:

```text
current bundled DXC
tools/d3d12-metal-sdk/cache/dxc/*
newly fetched DXC releases where useful
```

Other references:

```text
DirectXShaderCompiler tests/samples
Microsoft DirectX-Graphics-Samples shaders
D3D12 sample apps/probes already in tools/d3d12-metal-sdk/probes
```

## Tasks

Create or extend:

```text
tools/d3d12-metal-sdk/scripts/fetch-agility.sh
tools/d3d12-metal-sdk/scripts/fetch-dxc-matrix.sh
tools/d3d12-metal-sdk/scripts/fetch-directx-samples.sh
```

Add a manifest:

```text
tools/d3d12-metal-sdk/results/m12-translation-gauntlet/reference-payloads.json
```

## Acceptance criteria

- PEAK required Agility version 611 can be staged or recognized locally.
- Scripts are idempotent.
- Downloads are checksummed.
- No game launch is required.

## No-regression gate

Run dry-run/preflight only after staging Agility payloads:

```text
PEAK dry-run no longer reports missing Agility SDK x64 payload 611
Schedule I dry-run remains ok
Elden Ring dry-run remains ok
Subnautica 2 dry-run remains ok
```

---

# Phase 3 — DXIL/MSL translation gauntlet

## Goal

Run all available DXIL shaders through the M12 lowering/conversion path offline and classify every failure.

## Script

Create:

```text
tools/d3d12-metal-sdk/scripts/m12-translation-gauntlet.py
```

Inputs:

```text
--corpus <path>        repeatable
--profile <name>
--results-dir <path>
--allow-empty
--include-msl-errors
--include-pso-manifests
```

Outputs:

```text
translation-summary.json
translation-summary.md
dxil-lowering-failures.md
compute-shader-failures.md
pixel-shader-failures.md
vertex-shader-failures.md
unsupported-intrinsics.md
unsupported-opcodes.md
msl-compile-errors.md
```

## Classification buckets

Required failure classes:

```text
DXIL container parse
LLVM bitcode parse
DXIL to MSL lowering unsupported intrinsic
DXIL to MSL lowering unsupported opcode
MSL compile type mismatch
MSL compile address-space mismatch
MSL compile pointer cast / threadgroup issue
resource binding mismatch
threadgroup memory lowering
atomics
wave ops
UAV load/store
structured buffer
byte-address buffer
texture sample/gather/grad/lod
int/float vector cast
```

## Immediate known target

Subnautica 2 compute failures:

```text
dxil_msl_compile_failed=21
new_msl_errors=21
examples include int4 -> float4 implicit conversion errors
threadgroup pointer cast warnings/errors
```

These should be first-class gauntlet cases.

## Acceptance criteria

- Gauntlet can replay Elden Ring cache with zero current known render failures.
- Gauntlet can replay Subnautica 2 cache and reproduce/classify the 21 compute failures.
- Every failure has shader hash, source path, error category, and suggested owner area.

## No-regression gate

Before any fix:

```text
save baseline summary
```

After any fix:

```text
Elden Ring failures do not increase
Subnautica 2 total failures decrease or category changes are explained
no new MSL errors in previously passing shaders
```

---

# Phase 4 — D3D12/DXGI behavior gauntlet

## Goal

Validate runtime behavior beyond shader translation.

## Probe areas

```text
DXGI factory/adapter enumeration
swapchain creation/present
fullscreen/windowed mode behavior
render target/depth format support
resource creation
committed/placed resources
heaps
resource barriers
copy/upload paths
descriptor heaps and descriptor tables
CBV/SRV/UAV views
root signatures
fences/queues/synchronization
pipeline state cache
```

## Existing probes to leverage

```text
tools/d3d12-metal-sdk/probes/probe_dxgi_factory
tools/d3d12-metal-sdk/probes/probe_resources
tools/d3d12-metal-sdk/probes/probe_queues
tools/d3d12-metal-sdk/probes/probe_descriptors
tools/d3d12-metal-sdk/probes/probe_resource_views_formats
tools/d3d12-metal-sdk/probes/probe_present_windowed
tools/d3d12-metal-sdk/probes/probe_render_headless
tools/d3d12-metal-sdk/probes/probe_compute_pso
tools/d3d12-metal-sdk/probes/probe_graphics_pso
```

## Tasks

Create a gauntlet wrapper:

```text
tools/d3d12-metal-sdk/scripts/m12-runtime-gauntlet.sh
```

It should run probes under the exact M12 runtime and produce:

```text
runtime-gauntlet-summary.json
runtime-gauntlet-summary.md
probe-failures.md
```

## Acceptance criteria

- Runs without launching games.
- Uses the frozen/runtime-under-test hashes.
- Captures stdout/stderr/logs per probe.
- Classifies probe failures separately from game failures.

## No-regression gate

Any runtime change must pass all probes that passed before.

---

# Phase 5 — Visual/headless correctness gauntlet

## Goal

Present counters are not enough. Subnautica 2 shows a visible-pixel failure while the process/window/present path can appear active.

## Tasks

Add or improve headless/windowed render tests that produce deterministic pixels:

```text
clear color test
triangle color test
texture sample test
constant buffer color test
UAV compute write then draw test
depth test
MSAA resolve test
swapchain present readback/screenshot test
```

Output:

```text
visual-gauntlet-summary.json
visual-gauntlet-summary.md
image-diffs/
```

## Acceptance criteria

- Tests compare actual pixels, not only present count.
- Failures include expected/actual image artifacts.
- At least one compute-to-texture/display test exists before returning to Subnautica 2.

## No-regression gate

Visual tests that pass must remain bitwise or tolerance-equivalent after fixes.

---

# Phase 6 — Fix translation classes one at a time

## Goal

Do not make broad changes. Fix one classified failure class at a time with offline proof.

## Candidate first fixes

From current evidence:

1. Subnautica 2 compute MSL type mismatch:

```text
implicit conversions between int4 and float4 not permitted
```

2. Threadgroup pointer/address-space lowering:

```text
cast to threadgroup float4* from smaller integer type int
cast to threadgroup int* from smaller integer type int
```

3. Visual-output correctness for compute-fed render paths.

## Required workflow per fix

```text
1. isolate failing shader(s)
2. add minimal offline regression case
3. patch lowering/runtime
4. run translation gauntlet
5. run runtime probe gauntlet if relevant
6. run visual gauntlet if relevant
7. only then run one short game smoke
```

## Acceptance criteria

- The targeted failure decreases or disappears.
- No new failures are introduced in Elden Ring corpus.
- No new failures are introduced in passing Subnautica 2 shaders.
- Runtime hashes and source commit are recorded.

---

# Phase 7 — Cache/PSO persistence and pressure tests

## Goal

Once correctness gaps are smaller, reduce repeated first-run and over-time loading pressure.

## Known pressure areas

```text
DXMT_D3D12_FORCE_DXIL_SOURCE_COMPILE=1 is correct but expensive
PSO compile bursts can starve responsiveness
texture/upload pressure likely contributes to long-run hangs
```

## Tasks

- Add shader cache epoch/salt for the corrected M12 ABI.
- Persist refreshed DXIL metallibs and reflection safely.
- Add PSO compile pressure metrics offline/semi-offline.
- Add upload/resource allocation pressure metrics.

## Acceptance criteria

- Warm-cache run compiles less than cold-cache run.
- Cache safety can invalidate stale pre-fix artifacts.
- No correctness gates regress.

---

# Phase 8 — Controlled scenario game testing

## Goal

Return to games only after offline/probe coverage improves.

## Required scenarios

```text
Elden Ring 20s smoke
Elden Ring character creation live-hang scenario
Subnautica 2 smoke + visual-output check
Schedule I launch/init check
PEAK launch after Agility 611 staging
```

## Required evidence per run

```text
runtime hashes
game-local DLL hashes
cache counts
present/drawn counts
MSL errors/fail markers
process samples
scenario markers
visual output artifact where applicable
```

## Pass/fail definitions

Elden Ring:

```text
pass: reaches and interacts past character creation without hang
fail: live hang persists, even with zero PSO/MSL errors
```

Subnautica 2:

```text
pass: visible pixels are produced correctly enough to distinguish game content
fail: presents counted but output blank/incorrect
```

PEAK:

```text
pass: no Agility 611 prelaunch block and reaches M12 init
fail: missing Agility payload or early launcher abort
```

Schedule I:

```text
pass: reaches presents or clear game window output
fail: NSInvalidArgumentException before presents
```

---

# Tooling deliverables checklist

Create or improve:

```text
[ ] restore-known-working-current-m12-runtime.sh
[ ] verify-m12-runtime-hashes.py
[ ] m12-corpus-inventory.py
[ ] fetch-agility-matrix.sh
[ ] fetch-dxc-matrix.sh
[ ] fetch-directx-samples.sh
[ ] m12-translation-gauntlet.py
[ ] analyze-m12-translation-failures.py
[ ] m12-runtime-gauntlet.sh
[ ] m12-visual-gauntlet.sh
[ ] analyze-m12-live-hang.py
[ ] compare-m12-gauntlet-runs.py
```

Existing tools to reuse:

```text
m12-performance-run.sh
m12-live-state-capture.sh
m12-bounded-launch.sh
analyze-m12-varying-failures.py
analyze-m12-vertex-range-skips.py
offline-pso-factory.py
replay-shader-corpus.py
```

---

# Risk controls

## Do not

```text
Do not overwrite preserved runtime snapshots.
Do not restage experimental builds without preserving runtime and game-local DLLs.
Do not treat present_count as visual correctness.
Do not classify live hangs as crashes.
Do not optimize performance before classifying current correctness gaps.
Do not use game launches as the only discovery method.
```

## Always

```text
Record source commit.
Record d3d12.dll SHA.
Record runtime DLL hashes.
Record game-local DLL hashes.
Record cache counts.
Run offline gauntlet before and after fixes.
Commit docs/results summaries for every major finding.
```

---

# Immediate next phase

Start Phase 0 and Phase 1.

Recommended first implementation order:

1. `verify-m12-runtime-hashes.py`
2. `restore-known-working-current-m12-runtime.sh`
3. `m12-corpus-inventory.py`
4. `m12-translation-gauntlet.py` focused initially on Subnautica 2 compute failures
5. `analyze-m12-live-hang.py` focused on the Elden Ring character creation capture

Only after these are in place should we do more long interactive game launches.
