# Final Roadmap: Finish Unreal-native PCD3D SM6 → M12 Offline, Then Live Validation

Date: 2026-06-25

## Purpose

Finish the original goal in the strict sense we want: not just synthetic SM6 probes, and not raw standalone `.usf` guesses, but an authoritative Unreal-generated D3D/SM6 shader lane that produces real DXIL/root-binding evidence and proves the custom MetalSharp M12 stack can consume it offline before any live Subnautica 2 resume.

This roadmap extends the existing `unreal-to-m12-goal-roadmap.md` after the Phase 10-13 work already completed.

## Current proven state

MetalSharp branch:

- repo: `/Users/alexmondello/metalsharp-m12-lab`
- branch: `fix/m12-shader-probe-lab`
- latest relevant commits:
  - `2f81c59 fix: prove m12 sm6 runtime shader gates`
  - `0cb5c88 fix: prove m12 atomic64 sm66 gate`
  - `f5eb12f test: prove heap aliasing readback`
  - `9c0c6a5 fix: treat false options1 caps as present`
  - `ffb7fb5 fix: add descriptor probe runner alias`

Offline gates already green:

- descriptor table indexing runtime readback
- DXIL semantic UAV readback
- SM6.6 runtime cases including int64 and `atomic64_raw_uav`
- WaveOps runtime cases, with public caps still denied
- texture/sampler runtime path
- root constants and root/resource binding fixes
- reflection ABI
- resource views/formats
- heap aliasing readback
- Nanite-style 128 MiB / 64 KiB transient allocation
- synthetic SM5/SM6 shader corpus
- full Phase 12 contract comparison: `19/19` required probe passes
- Subnautica 2 read-only M12 dry-run: ok, no live launch performed

Important evidence roots:

- Phase 12 contract/probe proof: `/Volumes/AverySSD/MetalSharp-SM6-UE-Lab/06-results/in-progress/phase12-full-contract-probes-nomini-20260625-130401`
- Phase 13 read-only dry-run: `/Volumes/AverySSD/MetalSharp-SM6-UE-Lab/06-results/in-progress/phase13-subnautica2-backend-dryrun-20260625-131203`
- PR readiness summary: `/Volumes/AverySSD/MetalSharp-SM6-UE-Lab/06-results/in-progress/phase13-pr-readiness-summary-20260625-131755`

User decision:

- Live Subnautica 2 launch remains paused.
- User explicitly selected **Do not launch yet** after Phase 13 read-only dry-run.

## The remaining gap, precisely

We have Unreal-native generated shader environments for bounded Mac Metal commandlet/material jobs, and we have M12 offline proof for the critical SM6/M12 feature classes.

We do **not** yet have a complete reusable Unreal-native **PCD3D_SM6** generated environment/replay lane for every raw `.usf` file that failed standalone DXC.

Standalone `.usf` accounting result:

- SM6-sensitive files: `497`
- DXC attempts: `534`
- valid standalone DXIL+MSC successes: `9`
- failures: `525`
- dominant blocker: `missing_generated_uniform_buffers=504`

Interpretation:

- These are not M12 failures.
- They prove raw `.usf` source is insufficient without Unreal's generated `FShaderCompilerEnvironment`.
- To claim the Unreal SM6 side is finished how we want, we need authoritative Unreal-native PCD3D_SM6 DXIL/debug output or compiled bytecode/cache extraction, then replay/reconcile it through M12 offline.

## Definition of done

The roadmap is complete only when all of the following are true:

1. Every SM6/Nanite-sensitive Unreal shader record is accounted for by one of:
   - Unreal-native PCD3D_SM6 DXIL/debug artifact,
   - Unreal-native preprocessed/generated HLSL plus exact compile args,
   - compiled shader cache/bytecode artifact,
   - deterministic expected-denial classification that is not an M12 failure.
2. The PCD3D_SM6 corpus includes enough coverage for:
   - descriptor tables / descriptor indexing,
   - CBV/SRV/UAV/root constants,
   - texture and sampler paths,
   - int64,
   - atomic64,
   - WaveOps,
   - Nanite-adjacent buffers/allocation assumptions,
   - material/permutation-generated shader paths.
3. All valid DXIL artifacts are converted through Apple Metal Shader Converter where applicable and/or replayed through M12 offline.
4. Root signature / reflection / descriptor binding metadata reconciles with M12's binding model.
5. Offline M12 contract gates still pass after any changes.
6. Public capability reporting remains honest:
   - SM6.6/WaveOps/int64/atomic64 stay denied unless reporting policy is intentionally changed and backed by runtime readback proof.
7. Only after the above, live Subnautica 2 resume may be requested again.
8. Goal completion is not claimed until live validation and final audit pass.

---

# Phase 14 — Build an authoritative Unreal PCD3D_SM6 capture lane, no game launch

## Goal

Make Unreal itself generate the D3D/SM6 shader compiler environment offline, instead of compiling raw `.usf` files with guessed DXC args.

## Inputs

- Unreal checkout: `/Volumes/AverySSD/MetalSharp-SM6-UE-Lab/02-unreal-full/release/UnrealEngine`
- Existing Mac commandlet proof:
  - `UnrealEditor-Cmd -run=CompileShadersTestBed -targetplatform=Mac ... -NullRHI`
- Existing material debug dump proof.
- Existing standalone SM6 batch failure inventory.

## Work

1. Confirm which D3D shader formats are visible to the current host/toolchain:
   - `PCD3D_SM5`
   - `PCD3D_SM6`
   - any `SF_PCD3D_*` aliases exposed by Unreal `release`.
2. Try offline commandlet discovery only:
   - list shader formats,
   - list target platforms,
   - list ShaderFormatD3D module availability,
   - no game launch.
3. Attempt the smallest PCD3D_SM6 commandlet compile:
   - `UnrealEditor-Cmd`
   - `-NullRHI`
   - `-unattended`
   - debug dump CVars enabled,
   - minimal global shader or testbed target.
4. If PCD3D_SM6 is unavailable from Mac-host commandlets, classify why:
   - module not built,
   - Windows target platform disabled on host,
   - ShaderFormatD3D unavailable,
   - DXC/Agility dependency missing,
   - commandlet cannot load target platform.
5. Pick the fallback lane if needed:
   - build missing Unreal shader-format/target-platform modules,
   - use Unreal's Windows shader compiler tooling under Wine if feasible,
   - use a Windows-native offline Unreal commandlet on a separate machine if required,
   - or extract compiled shader bytecode/cache artifacts instead of source-compiling.

## Artifacts

- `06-results/.../phase14-pcd3d-sm6-commandlet-discovery/`
- `shader-format-discovery.json`
- `target-platform-discovery.json`
- `pcd3d-sm6-commandlet-smoke.json`
- exact command lines and stderr/stdout
- failure classification if unavailable

## Exit criteria

- We either have one real Unreal-native PCD3D_SM6 debug/DXIL output, or a precise blocker that selects the fallback lane.
- No live game process launched.
- Archive + private checkpoint complete.

---

# Phase 15 — Capture generated shader environments for failed `.usf` classes

## Goal

Turn the `missing_generated_uniform_buffers`/generated-environment failures into authoritative Unreal-native generated artifacts.

## Work

Use the Phase 14 lane to capture a targeted corpus first, then broaden:

1. Seed corpus from known blockers:
   - `Common.ush` generated uniform buffer failures,
   - Nanite candidate-cluster shaders,
   - LWC/DoubleFloat generated context failures,
   - material/vertex factory permutations,
   - `UpdateDescriptorHandle.usf` as a known good D3D/SM6 descriptor handle control.
2. For each shader job, capture:
   - original source file,
   - resolved virtual includes,
   - generated `/Engine/Generated/*` includes,
   - preprocessed HLSL,
   - rewritten HLSL if Unreal emits it,
   - `DebugCompileArgs.txt`,
   - `DebugCompile.in`,
   - shader platform and target profile,
   - entrypoint,
   - permutation/material/vertex-factory identifiers,
   - DXIL/DXBC output if produced,
   - root signature/reflection metadata if available.
3. Build a manifest that links each raw `.usf` failure to its generated Unreal-native job artifact.
4. Separate real compiler failures from missing-environment failures.

## Artifact schema

Each record should include:

```json
{
  "source": "Engine/Shaders/Private/...usf",
  "unreal_shader_platform": "PCD3D_SM6",
  "target_profile": "cs_6_6",
  "entrypoint": "MainCS",
  "permutation_id": "...",
  "material_or_global_shader": "...",
  "debug_compile_args": ".../DebugCompileArgs.txt",
  "preprocessed_hlsl": "...",
  "generated_includes": ["/Engine/Generated/GeneratedUniformBuffers.ush"],
  "dxil": "...",
  "dxil_sha256": "...",
  "root_signature": "...",
  "reflection": "...",
  "classification": "pcd3d_sm6_unreal_native_success|expected_denial|real_compile_failure"
}
```

## Exit criteria

- At least one representative shader from every dominant failure class has an Unreal-native generated environment artifact or a precise expected-denial.
- The `missing_generated_uniform_buffers` class is reduced from mystery blocker to captured generated include data.
- Archive + checkpoint complete.

---

# Phase 16 — PCD3D_SM6 DXIL → MSC/M12 replay and binding reconciliation

## Goal

Take every valid Unreal-native D3D/SM6 artifact from Phase 15 and prove it through the downstream offline pipeline.

## Work

For each valid DXIL artifact:

1. Run Apple Metal Shader Converter:
   - `metal-shaderconverter --validateAll`
   - capture `.metallib`
   - capture reflection JSON
   - capture converter diagnostics.
2. Run M12 offline replay/classification:
   - DXIL parser/lowering path,
   - generated MSL/metallib where applicable,
   - compute/graphics PSO creation,
   - binding manifest audit,
   - root signature audit.
3. Reconcile reflection/binding:
   - CBV/SRV/UAV register spaces,
   - descriptor tables,
   - root constants,
   - static samplers,
   - texture/sampler pairs,
   - UAV typed/raw/structured access.
4. Any failure becomes one of:
   - M12 bug to fix,
   - Apple MSC limitation to classify,
   - Unreal feature unsupported and honestly denied,
   - bad/incomplete capture to rerun.

## Artifacts

- `pcd3d-sm6-dxil-msc-results.json`
- `pcd3d-sm6-m12-replay-results.json`
- `pcd3d-sm6-binding-reconciliation.json`
- reduced repros for each M12 failure

## Exit criteria

- All valid PCD3D_SM6 DXIL artifacts are either:
  - MSC+M12 pass,
  - fixed in M12 and then pass,
  - expected-denial with honest caps.
- No raw `.usf` failure remains classified merely as missing generated environment unless Phase 14/15 proved the environment lane unavailable.

---

# Phase 17 — Expand from targeted corpus to full SM6/Nanite accounting

## Goal

Scale from representative blockers to the full discovered SM6/Nanite-sensitive corpus.

## Work

1. Revisit the 497-file SM6-sensitive inventory.
2. For each record, assign an authoritative lane:
   - Unreal-native PCD3D_SM6 compile/debug dump,
   - material/permutation debug dump,
   - compiled shader cache/bytecode extraction,
   - include-only dependency with reverse-include top-level owner,
   - no-entrypoint dependency unit,
   - deterministic expected-denial.
3. Re-run batch summaries with new categories:
   - `unreal_native_pcd3d_sm6_success`,
   - `unreal_native_generated_env_captured`,
   - `bytecode_extracted`,
   - `dependency_include_only`,
   - `expected_denial`,
   - `m12_replay_pass`,
   - `m12_replay_failure_fixed`,
   - `m12_replay_failure_open`.
4. Require every valid shader artifact to link to DXIL/MSC/M12 replay evidence.

## Exit criteria

- Full SM6/Nanite-sensitive corpus is accounted with authoritative lanes.
- The old `missing_generated_uniform_buffers=504` result is superseded by captured generated environments, include-only classifications, or precise expected-denials.
- No unresolved M12 replay failures remain in the launch-critical feature surface.

---

# Phase 18 — Final M12 implementation/fix loop from PCD3D artifacts

## Goal

Fix any real M12 issues found by PCD3D_SM6 artifacts.

## Work

Possible areas, depending on Phase 16/17 findings:

- DXIL parser record handling,
- int64 lowering,
- atomic64 lowering/reporting,
- WaveOps lowering/reporting,
- descriptor table indexing,
- root constants/root descriptor binding,
- texture/sampler/resource view lowering,
- raw/structured/typed buffer paths,
- graphics PSO/root signature alignment,
- heap/resource/barrier behavior.

Rules:

- Implement/thunk/lower features where feasible.
- Do not mark unsupported unless there is no safe lowering path.
- Keep public caps denied until proof + policy support reporting.
- Maintain narrow launch shape:
  - `d3d12.dll`,
  - `winemetal.dll`,
  - `winemetal.so`,
  - no `mscompatdb`,
  - no external `libm12core.dylib`,
  - overrides only `winemetal,d3d12=n,b`.

## Exit criteria

- Fix commits are narrow and evidence-backed.
- Targeted gates pass after each fix.
- Full Phase 12 contract gate passes again.

---

# Phase 19 — Final offline approval gate

## Goal

Produce the last no-live-launch approval package.

## Required commands/gates

1. Disk guard.
2. Build/stage runtime.
3. Build probes.
4. Required probe set:
   - device caps,
   - descriptors,
   - descriptor table indexing,
   - shaders,
   - shader corpus,
   - DXIL semantics,
   - SM6.6 capabilities,
   - WaveOps,
   - reflection ABI,
   - graphics PSO,
   - compute PSO,
   - command replay,
   - barriers/render pass,
   - resource views/formats,
   - heap aliasing,
   - Nanite transient allocation,
   - winemetal ABI,
   - runtime preflight,
   - compare-contract.
5. New PCD3D_SM6 corpus gates:
   - PCD3D_SM6 generated environment capture summary,
   - PCD3D_SM6 DXIL/MSC summary,
   - PCD3D_SM6 M12 replay summary,
   - binding/root signature reconciliation summary.
6. Subnautica 2 read-only dry-run only:
   - M12 dry-run,
   - pipeline dry-run,
   - runtime doctor.

## Exit criteria

- All required offline gates pass.
- PCD3D_SM6 corpus accounting is complete.
- No unresolved launch-critical M12 replay bugs.
- Read-only Subnautica 2 dry-run is ok.
- Archive + private checkpoint complete.
- User is asked for live launch approval.

---

# Phase 20 — Live Subnautica 2 validation, explicit approval only

## Goal

Resume live validation only after Phase 19 passes and user explicitly approves.

## Work

1. Ask user for approval.
2. If approved, call only the known backend launch path:

```http
POST /steam/launch-game
{"appid":1962700,"launchMethod":"m12"}
```

3. Collect logs/status:
   - latest launch log,
   - M12 pipeline logs,
   - shader cache sidecars,
   - process status,
   - user-visible result report from the user.
4. Do not use assistant screenshots.
5. If failure occurs:
   - stop,
   - classify from logs,
   - return to offline probe/repro loop.

## Exit criteria

- No loading/render failures.
- User confirms visible non-black/color output.
- Logs do not show a new M12 launch-shape regression.
- Final roadmap completion audit maps every original requirement to evidence.

---

# Immediate next action

Start Phase 14 only:

1. Disk guard.
2. Discover whether the current Unreal commandlet can see/load `PCD3D_SM6` offline.
3. Try one minimal `PCD3D_SM6` commandlet debug dump with `-NullRHI`.
4. Archive/checkpoint results.
5. Do **not** launch Subnautica 2.

If Phase 14 proves Mac-host PCD3D_SM6 unavailable, immediately switch to the fallback decision tree instead of forcing raw `.usf` DXC again.
