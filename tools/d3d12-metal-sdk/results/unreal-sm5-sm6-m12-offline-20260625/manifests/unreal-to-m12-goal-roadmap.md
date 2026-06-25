# Goal Roadmap: Unreal SM5/SM6/SM7 + D3D12 → MetalSharp M12

Objective: use Unreal Engine as a source/shader/reference oracle to harden MetalSharp M12’s custom `D3D12 → DXMT → winemetal` path for SM6/Nanite/Metal Desktop Renderer compatibility, then land the transferable work onto the current MetalSharp PR/branch with probes, tests, and offline corpus evidence.

## Non-negotiable operating model

Everything happens under:

```text
/Volumes/AverySSD/MetalSharp-SM6-UE-Lab
```

Every phase follows the same safety loop:

1. **Plan** the bounded section and disk budget.
2. **Disk guard** before doing work.
3. **Fetch/build/compile only that section**.
4. **Extract durable artifacts**: manifests, logs, indexes, reduced corpus, patches, checksums, summaries.
5. **Tarball** phase artifacts into `07-archives/`.
6. **Upload/checkpoint** to the private repo:
   - `https://github.com/aaf2tbz/metalsharp-unreal-sm6-nanite-lab`
7. **Verify remote checkpoint** and archive hashes.
8. **Clear local intermediates** before starting the next phase.

No phase may wipe local intermediates until the tarball and private git checkpoint are verified.

No live Subnautica 2 launches during this roadmap. This is offline-only until gates prove readiness.

## Private artifact policy

Because Unreal source is private/licensed:

- Prefer uploading manifests, indexes, logs, generated corpora, reduced snippets, patches, and checksums.
- Avoid uploading full Unreal source snapshots unless explicitly needed and confirmed private/legal-safe.
- Build outputs may be large; prefer `.tar.zst` archives with SHA-256 manifests.
- Use Git LFS or split archives only if needed.

## Global acceptance criteria

The roadmap is complete only when:

- SM5 shader corpus compiles through relevant Unreal/DXC/M12 paths.
- SM6 shader corpus compiles and runtime-relevant offline probes pass or accurately deny unsupported capabilities.
- Optional SM7 corpus, if available in the selected UE/DXC branch, is indexed and either compiled or explicitly classified as unsupported/not applicable.
- Unreal source sections relevant to D3D12, ShaderFormatD3D, MetalRHI, MetalShaderFormat, RHICore, Renderer/Nanite, and shaders are compiled or analyzed sufficiently to produce transfer targets.
- Unreal D3D12-related code paths are mapped to MetalSharp M12 implementation gaps.
- MetalSharp current PR/branch receives the resulting M12 changes.
- Offline probes/tests/corpus are committed or staged in MetalSharp and pass locally.
- M12 capability reporting is honest: no SM6.6/WaveOps/int64/mesh/Nanite-adjacent feature is advertised unless proven by offline runtime evidence.
- All phase artifacts are archived, uploaded, and local intermediates are cleared.

---

# Phase 0 — Lab and checkpoint readiness

## Goal

Confirm the lab, private repo, disk guard, sparse clone scripts, archive scripts, and wipe scripts are ready before touching Unreal.

## Work

- Run `08-scripts/00-check-prereqs.sh`.
- Run `08-scripts/05-disk-guard.sh`.
- Confirm private checkpoint repo is reachable via HTTPS.
- Confirm current MetalSharp PR/branch path:
  - `/Users/alexmondello/metalsharp-m12-lab`
  - branch expected: `fix/m12-shader-probe-lab`

## Artifacts

- `00-manifests/phase-0-readiness.md`
- disk/auth/tool output logs

## Exit criteria

- Private checkpoint push succeeds.
- AverySSD free space remains above guard threshold.
- No Unreal clone/build has started yet.

## Cleanup

- None beyond temporary logs.

---

# Phase 1 — Sparse Unreal source acquisition, no build

## Goal

Acquire only the Unreal source sections needed for SM5/SM6/SM7, D3D12, Metal, RHICore, and Nanite analysis.

## Work

Sparse/shallow fetch selected branch first, likely `release`, then optionally compare `5.8` and `ue6-main` later.

Initial sparse paths:

- `Engine/Source/Developer/Windows/ShaderFormatD3D/Private/`
- `Engine/Source/Runtime/Windows/D3D12RHI/Private/`
- `Engine/Source/Runtime/Apple/MetalRHI/`
- `Engine/Source/Developer/Apple/MetalShaderFormat/`
- `Engine/Source/Runtime/RHICore/`
- `Engine/Source/Runtime/Renderer/Private/Nanite/`
- `Engine/Source/Runtime/RHI/`
- `Engine/Source/Runtime/RenderCore/`
- `Engine/Shaders/Private/`
- `Engine/Shaders/Shared/`

## Sensitive areas touched

- Unreal shader compiler frontends.
- Unreal D3D12 RHI.
- Unreal Metal RHI / shader format.
- RHICore transient allocator.
- Nanite source and shaders.

## Artifacts

- source file inventory
- branch/commit manifest
- disk usage snapshot
- path-level index
- no full source archive unless explicitly approved

## Exit criteria

- Sparse checkout exists and is bounded.
- Source map identifies all relevant D3D12/Metal/Nanite/ShaderFormat files.
- Tarball contains inventories/indexes, not huge source dump.
- Private checkpoint uploaded.

## Cleanup

- Keep sparse checkout only if Phase 2 immediately follows and disk is safe.
- Otherwise tarball indexes and remove checkout.

---

# Phase 2 — Unreal shader inventory: SM5, SM6, optional SM7

## Goal

Build a complete shader inventory before compiling anything broadly.

## Work

- Enumerate `.usf`, `.ush`, `.hlsl`, shader compiler configs, platform defines, and shader model switches.
- Classify shaders into:
  - SM5 candidates
  - SM6 candidates
  - SM6 + WaveOps candidates
  - SM6 + int64/atomic64 candidates
  - Nanite candidates
  - mesh/amplification candidates
  - optional SM7 candidates, if the branch/toolchain exposes them
- Record required compiler flags/macros/includes.

## Sensitive areas touched

- Compiling all SM5 shaders: inventory prerequisite.
- Compiling all SM6 shaders: inventory prerequisite.
- Optional SM7: detect availability without assuming support.
- Nanite shader dependency map.

## Artifacts

- `04-corpus/unreal-derived/shader-inventory.json`
- `04-corpus/unreal-derived/shader-inventory.tsv`
- `04-corpus/unreal-derived/include-graph.json`
- `05-probes/unreal-shader-feature-matrix.md`

## Exit criteria

- Every discovered shader file is classified.
- SM5/SM6/optional SM7 status is explicit.
- Missing include/macro requirements are known.
- Tarball + checkpoint verified.

## Cleanup

- Remove generated temp indexes after archive/checkpoint.

---

# Phase 3 — Compile all SM5 shaders

## Goal

Compile all Unreal-derived SM5 shader candidates with the selected DXC/Unreal-compatible path, then replay the reduced corpus through MetalSharp M12 where feasible.

## Work

- Build a bounded SM5 corpus from Phase 2 inventory.
- Compile SM5 shaders with recorded target profiles and macros.
- Capture:
  - success/failure per shader
  - compiler stderr/stdout
  - DXIL/DXBC output hashes
  - reflection output where available
- Feed successful outputs into MetalSharp offline corpus replay where applicable.
- Classify failures:
  - missing Unreal macro/env only
  - DXC/tool issue
  - M12 translation issue
  - unsupported/non-shipping shader permutation

## Sensitive areas touched

- Compiling all SM5 shaders.
- M12 baseline shader translator compatibility.
- Regression protection for existing SM5 title support.

## Artifacts

- `04-corpus/unreal-derived/sm5/manifest.json`
- `06-results/.../sm5-compile-results.json`
- `06-results/.../sm5-m12-replay-results.json`
- failure classifier summary
- tarball + SHA-256

## Exit criteria

- All SM5 candidates have deterministic status.
- M12 regressions are either fixed or captured as tracked gaps.
- Private checkpoint uploaded.

## Cleanup

- Remove raw intermediates after tarball and checkpoint.
- Keep only compressed archive and summaries.

---

# Phase 4 — Compile all SM6 shaders

## Goal

Compile all Unreal-derived SM6 shader candidates and classify runtime feature requirements before M12 claims support.

## Work

- Build SM6 corpus from Phase 2 inventory.
- Compile targets across relevant profiles:
  - `cs_6_0` … `cs_6_6`
  - `vs_6_0` … `vs_6_6`
  - `ps_6_0` … `ps_6_6`
  - library profiles where Unreal uses them
- Capture wave, int64, atomic64, descriptor indexing, bindless, barycentric, mesh/amplification markers.
- Replay M12 offline translation and metallib compilation where possible.
- Separate compile success from runtime correctness.

## Sensitive areas touched

- Compiling all SM6 shaders.
- WaveOps support.
- int64 / atomic64 support.
- descriptor indexing / bindless-like patterns.
- Nanite prerequisite shader support.
- capability reporting honesty.

## Artifacts

- `04-corpus/unreal-derived/sm6/manifest.json`
- `06-results/.../sm6-compile-results.json`
- `06-results/.../sm6-m12-replay-results.json`
- `05-probes/sm6-runtime-proof-requirements.md`
- feature denial/proof matrix

## Exit criteria

- All SM6 candidates have deterministic compile/replay status.
- M12 unsupported features are not advertised.
- All M12 translation failures are reduced to actionable repros.
- Tarball + private checkpoint verified.

## Cleanup

- Remove raw intermediate shader outputs after archive/checkpoint.

---

# Phase 5 — Optional SM7 shader discovery/compile

## Goal

If the chosen Unreal/DXC branch exposes SM7 or SM7-like targets, inventory and optionally compile them without making M12 support claims.

## Work

- Search Unreal/DXC for SM7 profile names, flags, defines, and feature checks.
- If available, compile small representative corpus.
- Mark support status as:
  - unavailable
  - experimental only
  - compile-only
  - blocked
  - not relevant to current MetalSharp PR

## Sensitive areas touched

- Optional SM7 shaders.
- Future-facing capability-denial policy.

## Artifacts

- `04-corpus/unreal-derived/sm7/manifest.json` if applicable
- `06-results/.../sm7-discovery-results.md`
- `06-results/.../sm7-compile-results.json` if applicable

## Exit criteria

- SM7 availability is known, not guessed.
- No M12 capability is advertised from SM7 discovery alone.
- Tarball + checkpoint verified.

## Cleanup

- Remove SM7 temp outputs after archive/checkpoint.

---

# Phase 6 — Compile Unreal source code sections, no full engine yet

## Goal

Compile or validate the smallest source-code sections needed to understand Unreal’s D3D12, ShaderFormatD3D, MetalRHI, MetalShaderFormat, RHICore, and Nanite behavior.

## Work

- Identify minimal Unreal build targets for developer shader tools/RHI components.
- Prefer building tools/modules rather than full editor first.
- Capture compile commands, module dependencies, and generated headers requirements.
- If direct module compilation is impractical, produce source-derived indexes and static maps instead.

## Sensitive areas touched

- Compiling the source code.
- ShaderFormatD3D implementation.
- Metal shader format implementation.
- RHI and RHICore allocator internals.

## Artifacts

- module dependency graph
- compile command database where available
- build logs
- source-to-M12 transfer map
- tarball + hashes

## Exit criteria

- Minimal compilable Unreal source sections are either built or explicitly classified as requiring full engine build infrastructure.
- Transfer map identifies exact MetalSharp files/functions likely needing changes.
- Private checkpoint verified.

## Cleanup

- Delete intermediate build products unless needed for immediate Phase 7 and disk is safe.

---

# Phase 7 — Compile Unreal Engine in bounded sections

## Goal

Build Unreal Engine only as much as needed to generate authoritative shader/compiler/RHI outputs and confirm source assumptions.

## Work

- Choose target after Phase 6, likely a minimal commandlet/tool/editor target rather than broad all-target build.
- Set explicit phase disk budget before build.
- Run build under AverySSD only.
- Capture logs and generated artifacts needed for shader corpus and source mapping.
- Stop and archive if disk usage approaches guard threshold.

## Sensitive areas touched

- Compiling Unreal Engine.
- Generated shader compiler tooling.
- Generated headers / build graph.
- Unreal platform configuration.

## Artifacts

- build target manifest
- exact command lines
- build logs
- generated shader compiler metadata
- selected generated files needed for analysis
- tarball + hashes

## Exit criteria

- Selected Unreal build target succeeds, or failure is reduced to a specific dependency/budget blocker.
- Useful artifacts are archived and checkpointed.
- Disk is restored before next phase.

## Cleanup

- Remove `Intermediate`, broad object files, derived data, and build products after archive/checkpoint.
- Keep only minimal generated metadata if needed.

---

# Phase 8 — Compile Unreal DirectX 12 related components and map behavior

## Goal

Focus specifically on Unreal’s D3D12 RHI, ShaderFormatD3D, DXIL/SM6 feature checks, and Nanite D3D12 assumptions.

## Work

- Build or extract D3D12-related modules/tools from Unreal.
- Map:
  - D3D12 feature checks
  - shader model gates
  - WaveOps gates
  - int64/atomic64 gates
  - transient allocator assumptions
  - resource heap/aliasing behavior
  - descriptor heap and bindless assumptions
  - mesh/amplification shader checks
- Translate each into an M12 probe requirement.

## Sensitive areas touched

- Compiling anything Unreal Engine DirectX12 related.
- D3D12 feature reporting.
- UE SM6 compatibility expectations.
- Nanite transient allocator failure seen in Subnautica 2.

## Artifacts

- `05-probes/unreal-d3d12-to-m12-probe-map.md`
- `05-probes/nanite-transient-allocation-spec.md`
- `05-probes/sm6-wave-int64-capability-policy.md`
- D3D12 source/build logs
- tarball + checkpoint

## Exit criteria

- Every relevant Unreal D3D12 expectation is mapped to:
  - existing M12 support
  - new M12 probe
  - required M12 implementation change
  - honest denial policy
- Private checkpoint verified.

## Cleanup

- Clear D3D12 build intermediates after archive/checkpoint.

---

# Phase 9 — MetalSharp M12 transfer design

## Goal

Convert Unreal findings into a concrete MetalSharp M12 implementation plan on the current PR/branch.

## Work

Map Unreal findings into MetalSharp areas:

- `vendor/dxmt/src/d3d12/`
  - device caps
  - resources/heaps
  - descriptor heaps
  - command lists/queues
  - pipeline state
  - root signatures
- `vendor/dxmt/src/airconv/dxil/`
  - DXIL parsing/lowering
  - SM6 semantics
  - wave/int64/atomic lowering
- `vendor/dxmt/src/winemetal/`
  - Metal bridge/runtime behavior
  - winemetal exported behavior
- `vendor/dxmt/src/m12core/`
  - internal M12 runtime helpers
- `tools/d3d12-metal-sdk/probes/`
  - offline probes
- `tools/d3d12-metal-sdk/scripts/`
  - corpus replay and gate scripts

## Sensitive areas touched

- Transfer into MetalSharp M12 custom graphics driver.
- Current PR integration.
- No live game testing yet.

## Artifacts

- `09-notes/m12-transfer-design.md`
- file/function change plan
- risk matrix
- probe/test acceptance plan
- tarball + checkpoint

## Exit criteria

- Implementation plan is specific enough to code without guessing.
- Scope is split into reviewable commits.
- Private checkpoint verified.

## Cleanup

- None except temporary analysis outputs.

---

# Phase 10 — Implement M12 feature/capability changes on current PR

## Goal

Apply the Unreal-derived changes to the current MetalSharp PR/branch without widening launch shape.

## Work

Expected implementation classes:

1. **Capability honesty**
   - Downgrade/gate SM6.6, WaveOps, int64/atomic64, mesh/amplification claims until probes prove runtime behavior.

2. **SM6 shader support**
   - Fix DXIL parsing/lowering gaps discovered by SM6 corpus.
   - Preserve already fixed RawBufferLoad `.f32` behavior.

3. **Wave/int64/atomic behavior**
   - Implement or deny accurately.
   - Add runtime readback proof.

4. **Nanite/transient allocation behavior**
   - Add probe for 128 MiB / 64 KiB-aligned candidate cluster buffer pattern.
   - Fix heap/resource allocation path or deny feature path honestly.

5. **Descriptor/bindless behavior**
   - Add or repair descriptor indexing probes and implementation gaps.

6. **Metal/winemetal bridge behavior**
   - Ensure changes preserve final narrow launch shape:
     - `d3d12.dll`
     - `winemetal.dll`
     - `winemetal.so`
     - no `mscompatdb`
     - no `libm12core.dylib`
     - `m12core` internal to `winemetal.so`
     - Wine overrides only `winemetal,d3d12=n,b`

## Sensitive areas touched

- M12 custom D3D12 driver.
- DXMT internals.
- winemetal bridge.
- Current PR.

## Artifacts

- code changes on current PR branch
- patch summaries
- build logs
- probe results
- tarball + checkpoint

## Exit criteria

- MetalSharp builds.
- Offline probes pass or fail only with explicit expected-denial classifications.
- No broad/unplanned launch-shape changes.
- Private checkpoint verified.

## Cleanup

- Clear build intermediates after successful archive/checkpoint unless needed for next immediate validation.

---

# Phase 11 — Probes, tests, and offline corpus integration

## Goal

Make the work reproducible and reviewable through offline gates before any live game launch resumes.

## Work

Add/update probes for:

- SM5 corpus compile/replay.
- SM6 corpus compile/replay.
- Optional SM7 discovery/denial.
- DXIL semantic UAV readback.
- WaveOps runtime correctness.
- int64 / atomic64 runtime correctness.
- descriptor indexing / bindless behavior.
- Nanite-style transient allocation.
- resource heap aliasing / committed/placed resource behavior.
- Unreal D3D12 capability policy.

Add/update scripts for:

- corpus generation
- corpus replay
- failure classification
- phase gate summary
- archive/checkpoint manifests

## Sensitive areas touched

- Probes.
- Tests.
- Offline corpus.
- PR readiness.

## Artifacts

- updated `tools/d3d12-metal-sdk/probes/`
- updated `tools/d3d12-metal-sdk/scripts/`
- updated corpus manifests
- `06-results/.../final-offline-gate-summary.json`
- tarball + checkpoint

## Exit criteria

- Offline gate run is deterministic.
- Failures are either fixed or expected-denial with honest capability reporting.
- Private checkpoint verified.

## Cleanup

- Remove large corpus intermediates after archive/checkpoint.

---

# Phase 12 — PR hardening and review readiness

## Goal

Prepare the MetalSharp PR for review with narrow, evidence-backed changes.

## Work

- Run formatter/build/tests appropriate for touched code.
- Run offline M12 gates.
- Update PR summary/checklist.
- Add evidence links to private checkpoint manifests where appropriate.
- Run structured review/autoreview before finalizing.

## Sensitive areas touched

- Current PR.
- Reviewability.
- Regression prevention.

## Artifacts

- test logs
- final offline gate report
- PR summary draft
- review findings/fixes
- final tarball/checkpoint

## Exit criteria

- PR branch contains implementation + probes/tests/corpus manifests.
- Tests pass or expected-denial statuses are documented.
- No unarchived large phase output remains on AverySSD.
- Private checkpoint verified.

## Cleanup

- Clear all nonessential Unreal build/checkouts/intermediates.
- Keep only lab manifests, scripts, notes, archives, and private checkpoint mirror.

---

# Phase 13 — Resume live validation only after offline approval

## Goal

Only after offline gates are green, resume live Subnautica 2 validation to prove user-visible color render output.

## Work

- Reconfirm launch shape.
- Run dry-run pipeline diagnostics only first.
- If approved, use the known launch path:
  - `/steam/launch-game`
  - `appid: 1962700`
  - `launchMethod: m12`
- Do not use screenshots by assistant.
- Validate logs and user-visible output according to the active goal requirements.

## Sensitive areas touched

- Live game launch.
- Final proof goal.

## Exit criteria

- No loading/render failures.
- User-visible non-black/color render output is verified.
- Only then can the paused active goal be considered for completion audit.

---

# Phase-level command skeleton

Each phase should roughly use:

```bash
LAB=/Volumes/AverySSD/MetalSharp-SM6-UE-Lab
PRIVATE_GIT_REMOTE=https://github.com/aaf2tbz/metalsharp-unreal-sm6-nanite-lab.git

"$LAB/08-scripts/05-disk-guard.sh"

# phase-specific fetch/build/compile work here

"$LAB/08-scripts/04-archive-results.sh" "$PHASE_OUTPUT" "$PHASE_LABEL"

PRIVATE_GIT_REMOTE="$PRIVATE_GIT_REMOTE" \
  "$LAB/08-scripts/06-private-git-checkpoint.sh" "$PHASE_LABEL checkpoint"

# only after archive and checkpoint verification:
CONFIRM_WIPE_PHASE=1 "$LAB/08-scripts/07-clean-phase.sh" "$PHASE_OUTPUT"
"$LAB/08-scripts/05-disk-guard.sh"
```

# First recommended start point

Start with **Phase 0 + Phase 1 only**:

- Confirm disk/auth/private checkpoint.
- Sparse-checkout Unreal `release` source-index paths only.
- Build no engine yet.
- Generate source inventory and shader inventory prerequisites.
- Archive/checkpoint/wipe or keep only if Phase 2 immediately follows and disk remains safe.

This gives us the cleanest first proof that the sectional workflow works before expensive shader or engine compilation begins.
