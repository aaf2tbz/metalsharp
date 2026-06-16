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
5. **Mandatory Apple official Metal documentation integration gate before Phase 4.**
6. Build D3D12/DXGI behavior probe gauntlet.
7. Build visual/headless output gauntlet.
8. Fix translation classes one at a time.
9. Add cache/PSO persistence and pressure tests.
10. Return to controlled multi-game scenario testing.

Each phase has explicit acceptance criteria and no-regression gates.

The Apple documentation integration gate is not optional cleanup. It captures official Apple guidance that can directly improve M12 correctness, diagnostics, cache freshness, GPU-hang analysis, and performance. Complete as much of that gate as practical before starting Phase 4 probe expansion.

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

# Phase 3.5 — Apple official Metal documentation integration gate

## Goal

Incorporate as much official Apple Developer Documentation guidance as practical before Phase 4 begins. This is work we absolutely want in M12 because it comes from Apple's own Metal documentation and gives us direct leverage on the exact classes of problems we are seeing: shader/library failures, render/compute PSO creation, vertex descriptor correctness, command-buffer failures, resource hazards, cache freshness, GPU hangs, and performance pressure.

Phase 4 must not start as a broad D3D12/DXGI probe expansion until this official-guidance pass is complete enough to inform the probes and runtime diagnostics.

## Official documentation sources to anchor on

Primary Apple docs hub:

```text
https://developer.apple.com/documentation/metal
https://developer.apple.com/documentation/metalfx
https://developer.apple.com/documentation/xcode
```

Key Metal API areas from the research pass:

```text
MTLDevice
MTLLibrary
MTLFunction
MTLCompileOptions
MTLRenderPipelineDescriptor
MTLRenderPipelineState
MTLComputePipelineDescriptor
MTLComputePipelineState
MTLPipelineOption
MTLRenderPipelineReflection
MTLComputePipelineReflection
MTLBinaryArchive
MTLBinaryArchiveDescriptor
MTLArgumentEncoder
MTLHeap
MTLFence
MTLEvent
MTLSharedEvent
MTLCommandQueue
MTLCommandBuffer
MTLRenderCommandEncoder
MTLComputeCommandEncoder
MTLBlitCommandEncoder
MTLVertexDescriptor
MTLVertexAttributeDescriptor
MTLVertexBufferLayoutDescriptor
MTLHazardTrackingMode
MTLResourceOptions
MTLStorageMode
MTLCommandBufferError
MTLCommandBufferEncoderInfo
MTLCaptureManager
MTLCaptureDescriptor
MTLCaptureScope
Metal API Validation
Metal System Trace / Instruments
MetalFX spatial/temporal scaler descriptors and support checks
```

## Required workstreams

### 1. Preserve full Metal error context

Use Apple's `NSError`, `MTLLibraryError`, render/compute pipeline creation, and command-buffer error docs to make failures actionable.

Tasks:

- Audit all M12 `newLibrary`, `newFunction`, render PSO, compute PSO, binary archive, and command-buffer completion error paths.
- Log Apple error domain, numeric code, localized description, and `userInfo` where available.
- Include M12 context with every Metal failure:
  - appid/profile
  - shader hash
  - generated MSL path/hash
  - function name
  - PSO key/hash
  - root signature hash
  - render target/depth/stencil formats
  - sample count
  - vertex descriptor summary
  - binary archive state
  - command-list id / queue id / present count where relevant
- Do not collapse `MTLCommandBufferError` classes into generic failure. Distinguish timeout, page fault, out-of-memory, invalid resource, device removal, and unknown execution errors.

Acceptance:

```text
Every M12 Metal failure path emits Apple error domain/code/text plus M12 shader/PSO/queue context.
No blank Metal error lines remain in the M12 paths under test.
```

### 2. Use official pipeline/reflection docs to harden PSO diagnostics

Use Apple's `MTLRenderPipelineDescriptor`, `MTLComputePipelineDescriptor`, `MTLPipelineOption`, and reflection docs to make render/compute PSO probes more precise.

Tasks:

- Add a compact M12 PSO descriptor dump for failed and first-use render/compute PSOs.
- Record whether reflection was requested/available.
- Compare reflected Metal vertex attributes and resource bindings against M12 expectations where possible.
- Ensure PSO cache keys include descriptor-affecting state, not only shader hashes.

Acceptance:

```text
Failed PSO artifacts are sufficient to reconstruct the Metal-visible descriptor contract offline.
Reflected stage-in/vertex descriptor mismatches are classified separately from shader compile failures.
```

### 3. Turn Apple vertex-layout guidance into a dedicated M12 vertex descriptor audit

Use Apple's `MTLVertexDescriptor`, attribute descriptor, buffer layout, and stage-in documentation as the source of truth for our reflected stage-in path.

Tasks:

- Build/extend an audit that compares:
  - D3D12 input layout semantic/slot/format/offset/stride/step-rate
  - generated MSL `[[attribute(n)]]` / `[[stage_in]]`
  - reflected Metal vertex attributes
  - final `MTLVertexDescriptor`
- Keep the existing safe vertex-pulling zero-fill guard intact.
- Add specific failure buckets for missing attribute, wrong format, wrong buffer index, wrong offset, wrong stride, and wrong step function/rate.

Acceptance:

```text
Elden Ring render PSO path still has zero vertex_descriptor_missing and zero vs_ps_varying_mismatch.
New vertex descriptor audit can explain any future attribute mismatch without a game launch.
```

### 4. Resource binding, heap, and hazard audit from official docs

Use Apple's argument buffer, heap, resource usage, fence/event, storage mode, and hazard tracking docs to identify correctness risks before building Phase 4 probes.

Tasks:

- Audit M12 resource binding paths for argument-buffer/descriptor-table equivalents.
- Check whether indirect resources need explicit `useResource(s)` / `useHeap(s)` declarations.
- Audit heap/placed-resource aliasing and reuse against command-buffer completion/fence/event ordering.
- Audit storage modes and CPU/GPU synchronization for upload/readback/managed-resource paths.
- Create Phase 4 probe requirements from this audit rather than writing generic probes first.

Acceptance:

```text
A resource-hazard checklist exists and maps each checklist item to a current M12 code path or explicit gap.
Phase 4 probe list includes Apple-doc-backed tests for resource use declarations, heap aliasing, barriers, and synchronization.
```

### 5. Command-buffer hang and OOM diagnostics

Use Apple's `MTLCommandBuffer.error`, `MTLCommandBufferError`, encoder info, and Metal System Trace docs to improve Elden Ring hang and Schedule-like OOM analysis without shifting focus back to DX11.

Tasks:

- Add or extend M12 command-buffer completion logging with status, error domain/code, encoder info, command queue label, command buffer label, and last command-list id.
- Add labels to key Metal objects in debug builds where feasible:
  - command queues
  - command buffers
  - render/compute/blit encoders
  - swapchain textures
  - high-pressure buffers/textures
  - render/compute pipeline states
- Build `analyze-m12-live-hang.py` around this evidence:
  - final present count
  - last submitted/completed command buffer
  - repeated command-list patterns
  - waits/fences/events seen near the stall
  - no-progress windows

Acceptance:

```text
Elden Ring live-hang capture can be summarized as a command-buffer/queue/wait state, not only as a process still running.
Command-buffer OOM/page-fault/timeout cases are classified distinctly when Apple reports them.
```

### 6. Binary archive and PSO cache freshness plan

Use Apple's `MTLBinaryArchive` documentation to make M12 PSO/cache freshness more principled.

Tasks:

- Define a binary archive/cache key contract that includes:
  - GPU/device identity
  - OS/SDK/Metal version where available
  - M12 translator version or source commit
  - generated MSL hash
  - function constants
  - render/compute descriptor state
  - vertex descriptor state
  - attachment formats/sample count
- Add stale-cache detection for changed translator output and descriptor-affecting changes.
- Ensure force-DXIL-source-compile and reflected descriptor defaults cannot silently reuse incompatible artifacts.

Acceptance:

```text
M12 cache freshness rules are documented and enforced by at least one offline hash/cache verifier.
Known clean Subnautica/Elden paths do not require manual cache deletion to pick up translator fixes.
```

### 7. Capture and validation workflow

Use Apple's Metal API Validation, GPU capture, and Metal System Trace docs to define repeatable validation hooks.

Tasks:

- Document how to run a bounded M12 launch with Metal API Validation enabled.
- Add optional env/debug hooks for programmatic capture if feasible:
  - capture next frame
  - capture on PSO failure
  - capture on command-buffer error
  - capture around present count N
- Add an Instruments/Metal System Trace recipe for Elden Ring hang and Subnautica render validation.
- Make validation artifacts record whether API validation/capture/trace was enabled.

Acceptance:

```text
A developer can reproduce the same M12 validation setup and know which Apple tooling was active.
Capture/trace requests are explicit and off by default.
```

### 8. MetalFX capability/descriptor checks

Use official MetalFX docs only for D3D12/M12-relevant paths. Do not reopen DX11-focused work.

Tasks:

- Audit M12 MetalFX usage for capability checks and descriptor validation.
- Gate MetalFX construction on official support checks.
- Record descriptor dimensions/formats and reset/resizing behavior in diagnostics.
- Keep MetalFX disabled where it would obscure M12 correctness validation.

Acceptance:

```text
M12 validation can clearly state whether MetalFX was enabled, supported, and descriptor-valid.
Visual correctness tests can run with MetalFX disabled to avoid hiding base-render failures.
```

## Deliverables before Phase 4 starts

Required artifacts:

```text
tools/d3d12-metal-sdk/plans/m12-apple-metal-docs-integration-plan.md
tools/d3d12-metal-sdk/scripts/analyze-m12-live-hang.py
tools/d3d12-metal-sdk/scripts/audit-m12-metal-errors.py              # or equivalent checker
tools/d3d12-metal-sdk/scripts/audit-m12-vertex-descriptors.py        # or extension of existing inspector
tools/d3d12-metal-sdk/scripts/verify-m12-cache-freshness.py          # or equivalent verifier
```

Required evidence:

```text
Subnautica 2 bounded smoke remains at dxil_msl_compile_failed=0.
Elden Ring bounded smoke remains at render_pso_failed=0 and vertex_descriptor_missing=0.
M12 runtime/game-local DLL hashes are recorded for every validation run.
No live Elden Ring cache mutation from offline tools.
```

## Stop rules

- Do not use this phase to restart PEAK/Schedule/DX11 work.
- Do not start broad Phase 4 D3D12/DXGI probes until the Apple-doc-backed diagnostic and audit requirements above have been mapped into concrete probe requirements.
- Do not treat present/drawn counts as visual correctness.
- Do not mutate the live Elden Ring cache from offline tools.
- If an Apple doc recommendation conflicts with current M12 behavior, record the conflict and either fix it or add an explicit deferred-risk entry before Phase 4.

---

# Phase 4 — D3D12/DXGI behavior gauntlet — ACTIVE

## Goal

Validate runtime behavior beyond shader translation with no-game probes, using the Phase 3.5 Apple-doc-backed requirements as the probe map. Phase 4 is not permission to stage broad runtime tracing builds; it starts from the restored known-good M12 runtime and adds bounded probe evidence.

## Probe areas

```text
DXGI factory/adapter enumeration
swapchain creation/present
fullscreen/windowed mode behavior
render target/depth format support
resource creation
committed/placed resources
heaps and aliasing
resource barriers
copy/upload paths
descriptor heaps and descriptor tables
CBV/SRV/UAV views
root signatures
fences/queues/synchronization
pipeline state cache
Metal command-buffer error/status evidence
Metal resource-use declarations: useResource(s), useHeap(s), indirect resources
Metal storage-mode and hazard-tracking assumptions
Metal vertex descriptor reconstruction evidence
Metal binary archive/cache freshness inputs
```

## Existing probes to leverage

```text
tools/d3d12-metal-sdk/probes/probe_dxgi_factory
tools/d3d12-metal-sdk/probes/probe_resources
tools/d3d12-metal-sdk/probes/probe_queues
tools/d3d12-metal-sdk/probes/probe_descriptors
tools/d3d12-metal-sdk/probes/probe_resource_views_formats
tools/d3d12-metal-sdk/probes/probe_command_replay
tools/d3d12-metal-sdk/probes/probe_barriers_render_pass
tools/d3d12-metal-sdk/probes/probe_present_windowed
tools/d3d12-metal-sdk/probes/probe_render_headless
tools/d3d12-metal-sdk/probes/probe_compute_pso
tools/d3d12-metal-sdk/probes/probe_graphics_pso
```

## Phase 4 gauntlet wrapper

Created/active:

```text
tools/d3d12-metal-sdk/scripts/m12-runtime-gauntlet.sh
```

Default behavior:

```text
probe_set=phase4-core
no game launch
no runtime staging
full known-good M12 hash gate before probes
runs: Winemetal ABI, loader, agility, caps, DXGI, resources, queues, descriptors, command replay, barriers/render-pass, resource/view/format probes
skips by default: shader corpus, SM 6.6, wave ops, reflection ABI, PSO matrix, mini probes, headless/windowed visual probes
```

Output:

```text
tools/d3d12-metal-sdk/results/m12-runtime-gauntlet/<timestamp>/runtime-gauntlet-summary.json
tools/d3d12-metal-sdk/results/m12-runtime-gauntlet/<timestamp>/runtime-gauntlet-summary.md
tools/d3d12-metal-sdk/results/m12-runtime-gauntlet/<timestamp>/probe-failures.md
tools/d3d12-metal-sdk/results/m12-runtime-gauntlet/<timestamp>/run-probes.stdout
tools/d3d12-metal-sdk/results/m12-runtime-gauntlet/<timestamp>/run-probes.stderr
```

Optional probe sets:

```text
--probe-set phase4-core  # no-game runtime behavior core, default
--probe-set phase4-pso   # adds graphics/compute PSO probes
--probe-set all          # broad existing probe matrix; still no games, but may include windowed/headless probes
```

## Acceptance criteria

- Runs without launching games.
- Uses the frozen/runtime-under-test hashes.
- Captures stdout/stderr/logs per probe.
- Classifies probe failures separately from game failures.
- Produces a baseline Phase 4 result directory before any runtime change.

## No-regression gate

Any runtime change must pass every Phase 4 probe that passed on the known-good baseline, using the full-runtime hash gate to prove exactly what runtime was tested.

## Runtime instrumentation rule

Runtime-side probes may be added only as tiny isolated patches with full-runtime hash gates and rollback coverage. Do not repeat the Phase 3.5 broad diagnostic relink/stage failure pattern.

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

## Phase 0 implementation result — 2026-06-15

Implemented:

```text
tools/d3d12-metal-sdk/scripts/verify-m12-runtime-hashes.py
tools/d3d12-metal-sdk/scripts/restore-known-working-current-m12-runtime.sh
```

Phase 0 initially caught stale/mismatched game-local M12 DLLs in:

```text
/Volumes/AverySSD/SteamLibrary/steamapps/common/Schedule I
/Volumes/AverySSD/SteamLibrary/steamapps/common/PEAK
```

Those mismatched pre-normalization DLLs were preserved at:

```text
/Volumes/AverySSD/MetalSharp-M12-Preserved/phase0-pre-normalize-schedule-peak-20260615-222454
```

Then Schedule I and PEAK were normalized from the frozen working runtime:

```text
/Volumes/AverySSD/MetalSharp-M12-Preserved/working-current-source-runtime-elden-ring-20260615-220308
```

Phase 0 verification artifact:

```text
tools/d3d12-metal-sdk/results/m12-phase0-baseline-20260615-222455/runtime-hashes.md
tools/d3d12-metal-sdk/results/m12-phase0-baseline-20260615-222455/dry-run-summary.md
```

All checked runtime/game-local DLLs now match the known working baseline, including:

```text
d3d12.dll=8cdcec40588018dafaa3cdd1cfb140c1fd7edba6f1160cd7559d61be8b946500
```

Dry-run passed for all four appids without gameplay launches:

```text
1245620  Elden Ring    ok=true missing=[] d3d12_present=true workers=1 async=1
1962700  Subnautica 2  ok=true missing=[] d3d12_present=true workers=1 async=1
3164500  Schedule I    ok=true missing=[] d3d12_present=true workers=1 async=1
3527290  PEAK          ok=true missing=[] d3d12_present=true workers=1 async=1
```

Phase 0 is complete.

## Phase 1 implementation result — 2026-06-15

Implemented read-only corpus inventory:

```text
tools/d3d12-metal-sdk/scripts/m12-corpus-inventory.py
```

Phase 1 inventory artifact:

```text
tools/d3d12-metal-sdk/results/m12-translation-gauntlet/phase1-20260615-222706/corpus-inventory.md
tools/d3d12-metal-sdk/results/m12-translation-gauntlet/phase1-20260615-222706/corpus-inventory.json
```

Corpora indexed:

```text
elden-ring-live
subnautica-2-live
schedule-1-live
peak-live
elden-ring-scratch
perf-runs
live-captures
bounded-launches
```

Summary:

```text
elden-ring-live:    files=8044  dxbc=1590  msl=1587  metallib=238   pso_render=1215 pso_compute=2   msl_errors=0  fail_markers=0 unique_shaders=1589
elden-ring-scratch: files=10671 dxbc=1584  msl=1581  metallib=1586  pso_render=1172 pso_compute=0   msl_errors=0  fail_markers=0 unique_shaders=1583
subnautica-2-live:  files=4546  dxbc=775   msl=774   metallib=0     pso_render=1    pso_compute=673 msl_errors=99 fail_markers=0 unique_shaders=775
schedule-1-live:    files=4     no shader corpus yet
peak-live:          files=0     no shader corpus yet
```

Subnautica 2 MSL error categories:

```text
msl_vector_type_conversion: 36
msl_compile_error_other:   33
msl_threadgroup_pointer:   20
msl_address_space:          8
msl_type_conversion:        1
msl_undeclared_identifier:  1
```

Representative Subnautica 2 failure hashes/categories:

```text
239a65c38a4992bd msl_vector_type_conversion
2973397b09b5fbbb msl_type_conversion / atomic buffer pointer issue
d49a91efb562a061 msl_compile_error_other / threadgroup char* assigned float
833c8509da39368a msl_address_space
09f767d72f068de1 msl_threadgroup_pointer
ce673c6b5ed6cb14 msl_threadgroup_pointer
```

Interpretation:

- Elden Ring render-oriented corpus is currently clean by file-level MSL/fail-marker inventory.
- Elden Ring character creation live hang likely requires command/progress/hot-loop analysis rather than obvious MSL compile-failure triage.
- Subnautica 2 is the strongest offline translation completion target because it has a broad compute corpus and many classified MSL failures.
- Phase 2 should fetch/reference Agility/DXC payloads, but Phase 3 can already start by building a translation gauntlet around Subnautica 2 compute failures.

Phase 1 is complete.

## Phase 2 implementation result — 2026-06-15

Implemented reference payload tooling:

```text
tools/d3d12-metal-sdk/scripts/fetch-agility-matrix.sh
tools/d3d12-metal-sdk/scripts/fetch-dxc-matrix.sh
tools/d3d12-metal-sdk/scripts/m12-reference-payload-inventory.py
```

Agility SDK retail matrix fetched and mirrored into both SDK output and user runtime redist cache:

```text
tools/d3d12-metal-sdk/out/agility/<version>
~/.metalsharp/runtime/redist/agility/<version>
```

Fetched retail Agility versions:

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
```

Phase 2 artifacts:

```text
tools/d3d12-metal-sdk/results/m12-reference-payloads/agility-20260615-222921/reference-payloads.md  # first 1.611.2 fetch
tools/d3d12-metal-sdk/results/m12-reference-payloads/agility-20260615-222943/reference-payloads.md  # all retail
tools/d3d12-metal-sdk/results/m12-reference-payloads/dxc-20260615-223122/reference-payloads.md
tools/d3d12-metal-sdk/results/m12-reference-payloads/phase2-20260615-223043/reference-payload-inventory.md
```

Important result:

- PEAK's previous prelaunch blocker was resolved at dry-run level.
- Before Phase 2, PEAK launch returned:

```text
Agility SDK x64 payload not found for version 611
```

- After fetching/mirroring `1.611.2`, PEAK dry-run reports:

```text
ok=true
missing=[]
```

DXC matrix:

```text
v1.9.2602 / dxc_2026_02_20.zip
archive sha256=a1e89031421cf3c1fca6627766ab3020ca4f962ac7e2caa7fab2b33a8436151e
```

Reference payload inventory confirmed local DXC binaries:

```text
dxc.exe
dxcompiler.dll
dxil.dll
```

Dry-run verification after Phase 2, no gameplay launches:

```text
Elden Ring    ok=true missing=[]
Subnautica 2  ok=true missing=[]
Schedule I    ok=true missing=[]
PEAK          ok=true missing=[]
```

Phase 2 is complete for known retail Agility and pinned DXC. Preview Agility payloads and DirectX sample-source acquisition remain optional future expansion, not required before starting Phase 3.

## Four-game baseline expansion — Obj-C nil-array guard — 2026-06-15

After Phase 2 made PEAK/Schedule I staging valid, both games still failed before useful reports with:

```text
NSInvalidArgumentException: -[__NSPlaceholderArray initWithObjects:count:]: attempt to insert nil object from objects[1]
```

Root class:

- Winemetal was constructing Objective-C `NSArray` objects directly from raw C handle arrays for linked functions and binary archives.
- If any C-side handle was `0`, Objective-C raised before DXMT could return a normal failed PSO/status.
- This was an Obj-C bridge robustness issue, not a payload staging issue.

Runtime change:

- Added `winemetal_array_from_handles(...)` in `vendor/dxmt/src/winemetal/unix/winemetal_unix.c`.
- It filters nil handles, logs label/index/count, and returns nil if all entries are nil.
- Replaced raw `[NSArray arrayWithObjects:... count:...]` calls in compute/render/mesh/tile PSO paths.
- Added `@try/@catch` around compute and render PSO creation, matching the existing mesh exception handling style.

Preserved pre-change runtime:

```text
/Volumes/AverySSD/MetalSharp-M12-Preserved/pre-objc-array-guard-20260615-224259
```

New staged runtime hash:

```text
d3d12.dll sha256=c8786bd25a4be2d6893967b3c0e2d5b19763579e1ee3c8408129dd854ab57ec2
```

Four-game bounded smoke artifacts on the patched runtime:

```text
tools/d3d12-metal-sdk/results/perf-runs/elden-ring-smoke-20260615-224539/perf-analysis.md
tools/d3d12-metal-sdk/results/perf-runs/subnautica-2-smoke-20260615-224700/perf-analysis.md
tools/d3d12-metal-sdk/results/perf-runs/peak-smoke-20260615-224331/perf-analysis.md
tools/d3d12-metal-sdk/results/perf-runs/schedule-1-smoke-20260615-224423/perf-analysis.md
```

Results:

```text
Elden Ring:
  drawn/present=28/28
  render_pso_failed=0
  dxil_msl_compile_failed=0
  vertex_descriptor_missing=0
  vs_ps_varying_mismatch=0
  unsafe_draw_skips=0

Subnautica 2:
  drawn/present=6/7
  render_pso_failed=0
  dxil_msl_compile_failed=40
  vertex_descriptor_missing=0
  vs_ps_varying_mismatch=0
  unsafe_draw_skips=0

PEAK:
  no Obj-C nil-array abort after guard
  drawn/present=0/0
  later failure: Wine page fault after D3D11/DXMT init

Schedule I:
  no Obj-C nil-array abort after guard
  drawn/present=0/0
  later failure: frame-0 PSO/device error
  `MTLCommandBufferErrorDomain Code=8 Insufficient Memory`
```

Classification update:

- PEAK and Schedule I should now be included in the wide-gap baseline.
- They are not yet visual/perf targets, but they are no longer blocked by Agility staging or Obj-C bridge abort.
- Next wide-gap fixes:
  1. PEAK D3D11/Wine page fault after swapchain/device init.
  2. Schedule I frame-0 PSO/device error / Metal command-buffer OOM.
  3. Subnautica 2 compute DXIL→MSL failures.
  4. Elden Ring character-creation live hang.

## Phase 3 implementation result — Subnautica 2 compute MSL closure — 2026-06-15

Implemented the first Phase 3 offline DXIL/MSL gauntlet and translation fixes.

Tooling added/extended:

```text
tools/d3d12-metal-sdk/scripts/m12-translation-gauntlet.py
vendor/dxmt/src/airconv/airconv_cli.cpp --emit-msl
```

`airconv --emit-msl` now unwraps DXBC containers, extracts the DXIL chunk, parses DXIL bitcode, runs `MSLLowering`, and emits generated MSL to a scratch path. This gives Phase 3 a no-game no-cache-mutation regeneration loop.

Initial read-only corpus result:

```text
Subnautica 2 live cache:
  dxbc=775
  msl=774
  dxil_reports=774
  pso_json=674
  msl_errors=99
```

Original Subnautica 2 MSL failure classes:

```text
msl_vector_type_conversion=36
msl_threadgroup_assignment=23
msl_threadgroup_pointer=20
msl_compile_error_other=9
msl_address_space=8
msl_undeclared_identifier=1
msl_vector_scalar_conversion=1
msl_type_conversion=1
```

Translation fixes made in `vendor/dxmt/src/airconv/dxil/msl_lowering.cpp`:

- Skip stores through resolved non-pointer variables instead of emitting pointer stores through integer/math values.
- Honor predeclared target type for unsupported call fallbacks, preventing float defaults from being assigned into `threadgroup char*` variables.
- Infer threadgroup address space through referenced typed values inside pointer expressions such as `v389 + offset`.
- Coerce vector-looking operands to target vector type when tracked source type is unknown.
- Prefer outer vector constructor type when choosing zero vectors for bool coercion; avoid nested `tex.read()` causing `int4(...) != float4(0)`.
- Coerce future/self operand references to typed zero to avoid undeclared forward references.
- Coerce atomic buffer offsets through scalar/vector `.x` and integer casts before pointer arithmetic.
- Default resource/pointer values used as texture coordinates to zero instead of casting `device char*` to uint.

Scratch-only validation, no game launches and no live cache mutation:

```text
tools/d3d12-metal-sdk/results/m12-translation-gauntlet/phase3-patched-99-20260615-231519/summary.md
```

Intermediate result before final vector-zero fix:

```text
total=99
ok=83
fail=16
remaining=vector_type_conversion
```

Final Phase 3 Subnautica 2 result:

```text
tools/d3d12-metal-sdk/results/m12-translation-gauntlet/phase3-patched-99-20260615-231641/summary.md

total=99
ok=99
fail=0
emitfail=0
```

Runtime state:

```text
pre-change runtime preserved at:
/Volumes/AverySSD/MetalSharp-M12-Preserved/pre-phase3-msl-lowering-20260615-231721

new staged d3d12.dll sha256:
341ddc23256a0f94e17a20aa74331dc0271927ee434fb473ca041f4341c5b117
```

Phase 3 is complete for the known Subnautica 2 MSL error corpus. Next validation should be a controlled bounded Subnautica 2 run with the patched runtime to confirm runtime-side `dxil_msl_compile_failed` drops, followed by a four-game no-regression smoke.

## Phase 3.5 implementation result — Apple Metal docs diagnostic gate foundation — 2026-06-16

Implemented the first Apple-doc-backed Phase 3.5 diagnostic/audit foundation. These tools are read-only for live shader caches and write reports only under result directories.

New plan:

```text
tools/d3d12-metal-sdk/plans/m12-apple-metal-docs-integration-plan.md
```

New tools:

```text
tools/d3d12-metal-sdk/scripts/audit-m12-metal-errors.py
tools/d3d12-metal-sdk/scripts/audit-m12-vertex-descriptors.py
tools/d3d12-metal-sdk/scripts/verify-m12-cache-freshness.py
tools/d3d12-metal-sdk/scripts/analyze-m12-live-hang.py
```

Default Phase 3.5 inputs:

```text
~/.metalsharp/shader-cache/m12/1245620        # Elden Ring live cache
~/.metalsharp/shader-cache/m12/1962700        # Subnautica 2 live cache
~/.metalsharp/shader-cache/m12/1888160        # Armored Core VI live cache
/Volumes/AverySSD/MetalSharp-M12-CorpusLab/elden-ring-scratch/stable-20260615-192733
```

Generated evidence:

```text
tools/d3d12-metal-sdk/results/phase3.5-apple-metal-docs/metal-errors/metal-errors.md
tools/d3d12-metal-sdk/results/phase3.5-apple-metal-docs/vertex-descriptors/vertex-descriptors.md
tools/d3d12-metal-sdk/results/phase3.5-apple-metal-docs/cache-freshness/cache-freshness.md
tools/d3d12-metal-sdk/results/phase3.5-apple-metal-docs/live-hang/live-hang-analysis.md
```

Metal error audit result:

```text
events=921
render_pso_failure=720
metal_library_compile_error=189
msl_ctz_ambiguous=12
```

Important interpretation:

- Armored Core VI's four current shader failures are now explicitly classifiable as `msl_ctz_ambiguous` instead of generic MSL compile failures.
- The audit preserves Apple `NSError` domain/code/text where present and identifies places where runtime logs still need richer M12 context.
- Older Subnautica 2 `.msl.err.txt` artifacts remain visible in the live cache even though the current runtime smoke reached zero DXIL/MSL compile failures. Cache freshness tooling must distinguish active from stale errors.

Vertex descriptor audit result:

```text
elden-ring-live:       render_psos=1216  missing_vertex_msl=23  ok_or_vertex_pulling=1193
subnautica-2-live:     render_psos=1     ok_or_vertex_pulling=1
armored-core-vi-live:  render_psos=483   missing_vertex_msl=10  ok_or_vertex_pulling=473
elden-ring-scratch:    render_psos=1172  missing_vertex_msl=22  ok_or_vertex_pulling=1150
```

Important interpretation:

- Current Elden Ring and AC6 render PSO manifests are mostly internally sane from available offline evidence.
- `missing_vertex_msl` means a render PSO manifest references a vertex shader hash whose `.msl` file is absent from the cache input; it is not the same as runtime `vertex_descriptor_missing`.
- Future runtime `vertex_descriptor_missing` or `vs_ps_varying_mismatch` should be correlated with this audit plus reflected Metal attributes and final `MTLVertexDescriptor` dumps.

Cache freshness audit result:

```text
runtime d3d12.dll sha256=2612e228a5efa9d65f6923b3ed1cc50b1c6ce40abb2c0043c51d32ec5b60dd7c
elden-ring-live:       shaders=1619 metallib_older_than_msl=235 dxbc_without_msl=2
subnautica-2-live:     shaders=776  has_msl_error=99 active_msl_error=39 stale_msl_error_older_than_msl=60
armored-core-vi-live:  shaders=556  has_msl_error=4 active_msl_error=4 dxbc_without_msl=1
elden-ring-scratch:    shaders=1611 dxbc_without_msl=2
```

Required cache key contract recorded from Apple's `MTLBinaryArchive`/pipeline descriptor model:

```text
device_identity
os_version
metal_family_or_sdk
translator_commit_or_epoch
dxbc_sha256
generated_msl_sha256
entry_point
function_constants
render_or_compute_descriptor_state
vertex_descriptor_state
attachment_formats
sample_count
root_signature_hash
```

Elden Ring live-hang analyzer result:

```text
capture=tools/d3d12-metal-sdk/results/live-captures/elden-ring-character-creation-hung-20260615-221626
categories=present_progress_observed, submission_without_completion_evidence
max_present_number=960
command/progress lines found=80
Apple command-buffer errors near capture=0
pipeline/translation errors near capture=0
```

Important interpretation:

- The captured Elden Ring character-creation issue still looks like a live no-progress/hang, not a shader compile or PSO creation failure.
- Current capture artifacts do not include enough command-buffer completion/status/encoder/fence data to reduce the hang to a queue/wait state.
- Runtime logging gaps to close:

```text
command_buffer_label
command_buffer_status
command_buffer_error_domain_code_userInfo
encoder_info
queue_id
command_list_id
last_submitted_serial
last_completed_serial
present_count_at_submit
fence_event_wait_state
```

Phase 4 probe requirements generated by this gate:

1. Command-buffer diagnostics probe with Apple domain/code/userInfo/encoder info.
2. Resource declaration/hazard probe covering indirect resources, heaps, and aliasing.
3. Heap/storage synchronization probe for upload/readback/fence/event ordering.
4. Vertex descriptor reconstruction probe with sparse/multi-slot/per-instance attributes.
5. Binary archive/cache freshness probe for descriptor-affecting state changes.
6. Capture/validation record probe that records Metal API Validation/GPU capture/System Trace status.

Remaining Phase 3.5 runtime work before broad Phase 4:

- Add richer runtime Metal failure context around library/function/PSO creation and command-buffer completion.
- Add compact failed/first-use PSO descriptor dumps if missing from current runtime paths.
- Add final `MTLVertexDescriptor`/reflected attribute dump linkage to PSO artifacts.
- Add cache-key enforcement beyond the offline verifier.
- Define explicit capture/API validation env hooks, off by default.

## Phase 3.5 runtime diagnostic staging rollback — 2026-06-16

Attempted to stage an experimental Phase 3.5 runtime diagnostic build that added Winemetal `NSError` descriptor logging and command-buffer serial/status traces.

Bad diagnostic staged hashes:

```text
d3d12.dll    eac6959befe45ce1ce75e2fac9ca75527f432b0304ace296299c05f7f752c6cf
winemetal.so 5ec36f518fd99b20f0590f13c5f8938a19019baf32448e7973143335647d1ec6
```

Observed regression symptoms:

```text
Subnautica 2: UE reported "Failed to choose a D3D12 Adapter" / user-visible "DX12 not supported on your system" style failure.
Armored Core VI: user observed winedbg crash behavior, unlike the previous successful AC6 smoke.
```

The Subnautica failed run artifact did use `dxmt_m12` and the bad diagnostic hash, but produced no useful render validation:

```text
tools/d3d12-metal-sdk/results/perf-runs/subnautica-2-smoke-20260616-004928
runtime d3d12.dll=eac6959befe45ce1ce75e2fac9ca75527f432b0304ace296299c05f7f752c6cf
present/drawn=0/0
launch log: Failed to choose a D3D12 Adapter
```

Rollback performed from:

```text
/Volumes/AverySSD/MetalSharp-M12-Preserved/pre-phase35-runtime-diagnostics-20260616-004852
```

Restored known-good hashes:

```text
d3d12.dll    2612e228a5efa9d65f6923b3ed1cc50b1c6ce40abb2c0043c51d32ec5b60dd7c
winemetal.so 167d16f1280ce4f78f842576758c46cdc6db59c37c2e20aa3b7060fba7f49d58
dxgi_dxmt.dll 659ea3c4dddf658038eab67f26e71497ba11a4787e41c636766222ac2d8b028d
winemetal.dll 7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85
```

Important correction: rollback must restore all of these, not only root `d3d12.dll`/`dxgi.dll`:

```text
~/.metalsharp/runtime/wine/lib/dxmt_m12
~/.metalsharp/runtime/wine/lib/wine/x86_64-windows/winemetal.dll
~/.metalsharp/runtime/wine/lib/wine/x86_64-unix/winemetal.so
Elden Ring game-local M12 DLL set
Subnautica 2 root M12 DLL set
Subnautica 2 Engine/Binaries/Win64 M12 DLL set
Armored Core VI game-local M12 DLL set
```

Post-rollback dry-run verified all three active targets without game launch:

```text
Elden Ring appid=1245620:    ok=true missing=[] pipeline=m12 d3d12=2612e228... winemetal.so=167d16...
Subnautica 2 appid=1962700:  ok=true missing=[] pipeline=m12 d3d12=2612e228... winemetal.so=167d16...
Armored Core VI appid=1888160: ok=true missing=[] pipeline=m12 d3d12=2612e228... winemetal.so=167d16...
```

Action taken to prevent repeat ambiguity:

```text
m12-bounded-launch.sh now supports --expect-d3d12-sha and --expect-winemetal-so-sha.
m12-performance-run.sh forwards those gates.
The gate verifies source runtime before launch and deployed d3d12.dll destinations from launch.json after launch.
```

Decision:

- Do not re-stage runtime diagnostics as a broad d3d12/winemetal rebuild during Phase 3.5.
- Keep Phase 3.5 completion focused on offline analyzers, strict hash gates, Apple-doc-backed probe requirements, and future tiny runtime patches only after isolated build/runtime proof.
- Any future runtime diagnostic patch must first run behind strict expected-hash gates and must restore **all** runtime/game-local DLL surfaces on rollback.

## Phase 3.5 diagnostic runtime regression postmortem refinement — 2026-06-16

Further investigation of the bad diagnostic staging showed:

```text
Known-good Subnautica 2 launch-1781588605:
  D3D12CreateDevice created device with FL 45056
  LogD3D12RHI: Creating D3D12 RHI with Max Feature Level SM6
  later smoke reached presents/draws

Bad diagnostic Subnautica 2 launch-1781592577:
  DXGIFactory unknown-interface query occurred
  immediately failed with: LogD3D12RHI: Error: Failed to choose a D3D12 Adapter
  no useful presents/draws
```

The bad diagnostic runtime was not a legacy/DX11 pipeline selection mistake. It loaded `dxmt_m12`, but with a bad staged runtime set:

```text
bad d3d12.dll     eac6959befe45ce1ce75e2fac9ca75527f432b0304ace296299c05f7f752c6cf
bad dxgi_dxmt.dll 7776ff3c98182558c1182a26465affe7fe8861f5513f690b2f8290a8ebca2431
bad winemetal.so  5ec36f518fd99b20f0590f13c5f8938a19019baf32448e7973143335647d1ec6
```

Only bad `d3d12.dll` contained the experimental `command_buffer_*` trace strings, but the failure happened before command-buffer execution. Therefore, do **not** assume the command-buffer trace itself executed and caused the failure. The safer conclusion is:

```text
A broad runtime diagnostic rebuild/relink changed the D3D12/DXGI runtime surface enough to break UE adapter selection.
```

Practical lessons:

1. Do not stage broad runtime diagnostics while trying to finish Phase 3.5.
2. Do not rely on checking only `d3d12.dll`; mixed-runtime states involving `dxgi_dxmt.dll`, `winemetal.dll`, and `winemetal.so` can still invalidate a run.
3. Runtime diagnostics must be isolated and bisected:
   - one source file / one behavior class at a time,
   - no broad relink unless unavoidable,
   - strict full-runtime hash gate before and after launch,
   - immediate rollback of all runtime and game-local DLL surfaces on first adapter-selection or winedbg regression.

Hash gate revision:

```text
m12-bounded-launch.sh supports:
  --expect-d3d12-sha
  --expect-dxgi-sha
  --expect-dxgi-dxmt-sha
  --expect-winemetal-dll-sha
  --expect-winemetal-so-sha

m12-performance-run.sh forwards all of these.
```

The gate now verifies source runtime before launch and deployed game-local DLLs from `launch.json` after launch for all expected Windows DLLs. It also verifies both source and `lib/wine` `winemetal.so` before launch.

Current known-good active M12 hash set:

```text
d3d12.dll      2612e228a5efa9d65f6923b3ed1cc50b1c6ce40abb2c0043c51d32ec5b60dd7c
dxgi.dll       dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24
dxgi_dxmt.dll  659ea3c4dddf658038eab67f26e71497ba11a4787e41c636766222ac2d8b028d
winemetal.dll  7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85
winemetal.so   167d16f1280ce4f78f842576758c46cdc6db59c37c2e20aa3b7060fba7f49d58
```

Use the full set for future Elden Ring/Subnautica 2/Armored Core VI validation until an intentional runtime update is proven.

## Phase 3.5 completion audit — 2026-06-16

Phase 3.5 safe/offline work is complete and documented in:

```text
tools/d3d12-metal-sdk/plans/m12-phase3.5-completion-audit.md
```

Final refreshed Phase 3.5 reports:

```text
tools/d3d12-metal-sdk/results/phase3.5-apple-metal-docs/final-20260616-011003/metal-errors/metal-errors.md
tools/d3d12-metal-sdk/results/phase3.5-apple-metal-docs/final-20260616-011003/vertex-descriptors/vertex-descriptors.md
tools/d3d12-metal-sdk/results/phase3.5-apple-metal-docs/final-20260616-011003/cache-freshness/cache-freshness.md
tools/d3d12-metal-sdk/results/phase3.5-apple-metal-docs/final-20260616-011003/live-hang/live-hang-analysis.md
```

Decision:

```text
Phase 4 may begin as Apple-doc-backed probe planning/implementation.
Do not stage broad runtime diagnostic rebuilds as part of Phase 3.5 closure.
Runtime diagnostics are deferred to isolated, full-runtime-hash-gated patches.
```

## Phase 4 implementation start — runtime gauntlet wrapper and baseline — 2026-06-16

Phase 4 has started as Apple-doc-backed no-game probe work.

Added wrapper:

```text
tools/d3d12-metal-sdk/scripts/m12-runtime-gauntlet.sh
```

Corrected probe runner control:

```text
tools/d3d12-metal-sdk/scripts/run-probes.sh --no-dxil-semantics
```

Baseline run:

```text
tools/d3d12-metal-sdk/results/m12-runtime-gauntlet/20260616-011839/runtime-gauntlet-summary.md
```

Runtime hash gate used:

```text
d3d12.dll      2612e228a5efa9d65f6923b3ed1cc50b1c6ce40abb2c0043c51d32ec5b60dd7c
dxgi.dll       dc800838673b2e2236f775889a7c464ba72403a92a926b8073d742b28563ef24
dxgi_dxmt.dll  659ea3c4dddf658038eab67f26e71497ba11a4787e41c636766222ac2d8b028d
winemetal.dll  7f8cc745406440b3b262588d4fb397c0f028593916b613c638226d460327fa85
winemetal.so   167d16f1280ce4f78f842576758c46cdc6db59c37c2e20aa3b7060fba7f49d58
```

Baseline result:

```text
probe_json_count=12
passed=9 probe result JSONs plus Winemetal ABI
failed=2
run_probes_exit=0
```

Passing Phase 4 core probes:

```text
winemetal-abi
probe-loader
probe-agility-ue5
probe-resources
probe-queues
probe-descriptors
probe-command-replay
probe-barriers-render-pass
probe-resource-views-formats
```

Known Phase 4 baseline failures:

```text
probe-device-caps-metalsharp.json
probe-dxgi-factory-metalsharp.json
```

Device caps failure details:

```text
D3D12CreateDevice succeeds
feature_level max=12_1
shader_model highest=6_5
shader_model_6_6_or_better=false
CreateReservedResource hr=0x00000000, so reserved_resources_unsupported=false
CreateStateObject hr=0x80004001, so state_objects_unsupported=true
```

DXGI failure details:

```text
CreateDXGIFactory/CreateDXGIFactory1/CreateDXGIFactory2 succeed
IDXGIFactory through IDXGIFactory6 supported
IDXGIFactory7 unsupported: 0x80004002
EnumAdapters/EnumAdapters1/EnumAdapterByGpuPreference succeed
EnumAdapterByLuid returns DXGI_ERROR_NOT_FOUND
factory_versions_supported=false
adapter_stable=false
```

Interpretation:

- Phase 4 has a hash-gated no-game baseline.
- Baseline failures are now explicit runtime-surface targets, separate from game failures.
- Do not treat these as regressions from the Phase 3.5 rollback; they are current known-good runtime probe findings.
- Next safe Phase 4 work should inspect whether these probe expectations are too strict vs desired M12 policy, or whether DXGI/device capability reporting should be corrected.

## Phase 4 baseline refinement — 2026-06-16

After correcting probe policy strictness, Phase 4 core now has one hard baseline failure.

Updated probes:

```text
tools/d3d12-metal-sdk/probes/probe_device_caps/probe_device_caps.cpp
tools/d3d12-metal-sdk/probes/probe_dxgi_factory/probe_dxgi_factory.cpp
```

Changes:

- `probe_device_caps` no longer fails merely because WaveOps are reported consistently or reserved resources are implemented. It still fails if hard minimums fail or unsafe advanced capabilities are advertised.
- `probe_dxgi_factory` no longer requires optional `IDXGIFactory7` support for core pass. It records Factory7 separately.

Refined baseline:

```text
tools/d3d12-metal-sdk/results/m12-runtime-gauntlet/20260616-012333/runtime-gauntlet-summary.md
```

Result:

```text
probe_json_count=12
failure_count=1
remaining_failure=probe-dxgi-factory-metalsharp.json
```

Remaining hard DXGI finding:

```text
CreateDXGIFactory/CreateDXGIFactory1/CreateDXGIFactory2 succeed
IDXGIFactory through IDXGIFactory6 supported
IDXGIFactory7 unsupported but optional for core pass
EnumAdapters succeeds
EnumAdapters1 succeeds
EnumAdapters1 repeat succeeds
EnumAdapterByGpuPreference succeeds
EnumAdapterByLuid returns DXGI_ERROR_NOT_FOUND for the LUID returned by EnumAdapters1
adapter_stable=false
```

Why this matters:

- This is directly relevant to UE/D3D12 adapter-selection behavior.
- It is not proof of the Phase 3.5 diagnostic rebuild regression, but it is in the same risk family: DXGI adapter identity must be internally stable.
- Next Phase 4 runtime-source work should fix `EnumAdapterByLuid`/adapter identity consistency, then stage only that tiny DXGI change with full-runtime hash gates and rerun the Phase 4 core gauntlet before any game launch.

## Phase 4 DXGI LUID evidence and source candidate — 2026-06-16

Enhanced `probe_dxgi_factory` to print the actual adapter LUIDs.

Evidence run:

```text
tools/d3d12-metal-sdk/results/m12-runtime-gauntlet/20260616-012502/runtime-gauntlet-summary.md
```

Observed values:

```text
EnumAdapterByLuid=0x887a0002
adapter_stable=false
EnumAdapters1 LUID high=-1190985728 low=16777216
EnumAdapters1 repeat LUID high=-1190985728 low=16777216
EnumAdapterByGpuPreference LUID high=-1190985728 low=16777216
EnumAdapterByLuid output LUID high=0 low=0
```

Interpretation:

- Adapter identity is stable across `EnumAdapters1` and `EnumAdapterByGpuPreference`.
- The hard failure is specifically `EnumAdapterByLuid` failing to resolve that same LUID.
- This is a good Phase 4 target because UE/D3D12 adapter selection paths commonly rely on stable DXGI adapter identity.

Tiny runtime-source candidate added, not staged yet:

```text
vendor/dxmt/src/dxgi/dxgi_factory.cpp
```

Candidate behavior:

```text
If EnumAdapterByLuid cannot find an exact match on a single-adapter system and the requested LUID is non-zero, return adapter 0 as a fallback and log the requested/candidate LUIDs.
```

Validation rule before game launch:

1. Build/stage only this DXGI candidate, not a broad runtime diagnostic rebuild.
2. Preserve current known-good runtime and game-local DLL surfaces first.
3. Run full-runtime hash-gated Phase 4 core gauntlet.
4. Require `probe-dxgi-factory-metalsharp.json pass=true` before considering any game launch.
