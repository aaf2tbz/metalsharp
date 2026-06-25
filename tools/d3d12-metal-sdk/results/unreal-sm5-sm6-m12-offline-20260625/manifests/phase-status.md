# Phase Status

## Completed / archived

- 2026-06-24 offline SM6/Nanite gate run linked at `06-results/completed/20260624-offline-sm6-nanite-gates`.
- GitHub auth and Epic private repo access confirmed.
- Lab scaffold created on AverySSD.

## Current blockers

- M12 capability reporting is too optimistic under `DXMT_D3D12_UE_SM6_COMPAT=1`:
  - Reports SM6.6 and WaveOps.
  - Offline probes say SM6.6 and WaveOps are not reportable until runtime cases execute and pass.
- `probe-dxil-semantics` runtime readback returns zero output for SM6 semantic cases.
- Subnautica 2 post-shader blocker is Nanite transient allocation:
  - `Transient allocator failed to allocate Buffer Nanite.MainAndPostCandidateClustersBuffer`
  - size `134217728`, stride `4`, allocation alignment `65536`.

## Planned sections

### A. Unreal source index, no build

Target sparse paths:

- `Engine/Source/Developer/Windows/ShaderFormatD3D/Private/`
- `Engine/Source/Runtime/Windows/D3D12RHI/Private/`
- `Engine/Source/Runtime/Apple/MetalRHI/`
- `Engine/Source/Developer/Apple/MetalShaderFormat/`
- `Engine/Shaders/Private/`
- `Engine/Shaders/Shared/`
- `Engine/Source/Runtime/Renderer/Private/Nanite/`
- `Engine/Source/Runtime/RHICore/`

### B. Capability honesty patch

Gate or downgrade:

- SM6.6 report
- WaveOps report
- Int64ShaderOps / atomic64 reports
- mesh/amplification shader claims

### C. Offline probes

Add/repair:

- SM6 runtime UAV readback correctness
- Wave intrinsics runtime correctness
- int64/atomic64 correctness
- descriptor indexing / bindless resource behavior
- Nanite transient allocator allocation pattern
- mesh/amplification shader denial/proof

### D. UE-derived shader corpus

Extract reduced HLSL from Unreal shader/macros, not full engine build.

### E. Optional partial UE build

Only after user approval and disk budget.

## 2026-06-25 policy correction

User clarified that the main Unreal workspace should persist and should not be sparse. The wipe cycle applies to huge generated outputs, especially compiled shader corpora, not the Unreal source checkout. Authoritative policy is now `00-manifests/unreal-sectional-build-policy-v2.md`.

## 2026-06-25 disk policy update

User approved using AverySSD down to a hard floor of 50 GiB free. Disk guard default changed from 250 GiB to 50 GiB. Continue preserving the full Unreal checkout/dependencies; clean huge generated corpora/intermediates when no longer needed.

## 2026-06-25 ShaderCompileWorker build result

`ShaderCompileWorker` is built successfully. UBA run stalled/was interrupted around action 146 and a post-reboot UBA resume did not show visible action progress. Retrying with `-NoUBA -MaxParallelActions=4` completed the remaining 68 link/metadata actions successfully. Prefer this mode for fragile Unreal build resumes.

## 2026-06-25 ShaderBuildWorker build result

`ShaderBuildWorker` built successfully with `-NoUBA -MaxParallelActions=4` in 212 actions. Continue using this mode for long Unreal Mac targets.

## 2026-06-25 UnrealEditor build result

`UnrealEditor Mac Development` built successfully after one local warning-as-error patch in `BlendProfileStandaloneFactory.cpp`. First pass reached 5717/5720; resume after patch completed 260 actions and succeeded. Built outputs include `Engine/Binaries/Mac/UnrealEditor` and `UnrealEditor-Cmd`.

## 2026-06-25 Unreal-native commandlet shader compile smoke

`CompileShadersTestBed` was validated under `UnrealEditor-Cmd` with `-NullRHI`:

- help smoke returned `rc=0` in ~78s
- real compile command: `-run=CompileShadersTestBed -targetplatform=Mac -ExcludeMaterials -unattended -nop4 -NoSplash -NullRHI`
- compile returned `rc=0` in ~28m
- observed formats: `SF_METAL_SM5`, `SF_METAL_SM6`
- shaders compiled: `17,104`
- jobs completed: `17,104 / 17,104`
- commandlet summary: `Success - 0 error(s), 133 warning(s)`
- no lingering Unreal workers after exit
- no live game launch performed

Artifact root: `06-results/completed/20260624-unreal-native-sm5-sm6-commandlet-smoke`.
Manifest: `00-manifests/phase-3-4-unreal-native-sm5-sm6-smoke.md`.

## 2026-06-25 SM5 batch 0001 standalone DXC smoke

Batch manifest: `04-corpus/unreal-derived/release-shader-batches-100-20260624-192347/sm5_baseline_candidate/sm5_baseline_candidate-batch-0001.json`.

Runner: `08-scripts/12-compile-shader-batch-with-dxc.py`.

Result: `06-results/completed/20260624-phase3-sm5-dxc-batch0001-smoke`.

- files in batch: `100`
- DXC attempts: `50`
- successful DXIL attempts: `5`
- failed DXC attempts: `45`
- classified/skipped files: `68`
- file statuses: `success=4`, `attempted_failed=28`, `include_header=46`, `no_entrypoint=22`
- main remaining standalone blocker: generated Unreal shader environment include `/Engine/Generated/GeneratedUniformBuffers.ush`

This is a standalone DXC corpus-accounting smoke, not a replacement for Unreal-native commandlet compilation or M12 replay.

## 2026-06-25 Metal Shader Converter + special render-artifact lanes

User required accurate/full conversion rather than approximated shader compilation. The lab now treats Apple Metal Shader Converter 4.x as an authoritative DXIL -> MetalLib lane and treats Unreal render artifacts explicitly:

- `.ush` = dependency include units; compile affected top-level units via Unreal-native shader environment.
- `.usf` / Unreal-owned `.hlsl` = Unreal-native preprocessing/debug dump -> DXIL -> Metal Shader Converter -> reflection/binding reconciliation.
- material graphs and Custom Material Expression HLSL = generated material HLSL/permutation extraction required; no guessed macro acceptance.
- texture packages and raw texture sources = texture format/mip/sRGB/compression/virtual-texture/DXGI/Metal pixel-format validation required.
- `.dxil`, `.dxbc`, `.dxc`, `.dxe`, `.cso`, `.ushaderbytecode`, `.ushadercode`, `.udd` = bytecode/cache introspection/extraction lanes.

SearXNG Docker search was restored on `http://localhost:8888`, and `web-search` is now on PATH at `/opt/homebrew/bin/web-search` for docs research.

Artifacts:

- manifest: `00-manifests/phase-4-metal-shader-converter-special-render-artifact-lanes.md`
- completed results: `06-results/completed/20260625-metal-shader-converter-special-render-artifact-lanes`
- Epic docs extract: `09-notes/epic-docs-material-shader-texture-extract-20260625-002825.md`
- new/updated scripts:
  - `08-scripts/12-compile-shader-batch-with-dxc.py` gained `--convert-metallib`
  - `08-scripts/13-plan-special-shader-conversion.py` added explicit shader/material/texture/bytecode/DDC lanes

Key smoke result: SM6-sensitive batch 0001 produced 8 reduced DXIL successes and all 8 converted through `metal-shaderconverter --validateAll` to MetalLib+reflection. This proves the MSC lane works for valid DXIL but does not complete Unreal-native SM6 acceptance because the remaining failures require Unreal-generated shader environments/material or cache extraction.

## 2026-06-25 Unreal-native material shader debug dump smoke

A bounded `CompileShadersTestBed` material-only commandlet now proves Unreal-native generated material shader artifact extraction.

- result: `06-results/completed/20260625-unreal-native-material-shader-debug-dump-smoke`
- manifest: `00-manifests/phase-4-unreal-native-material-shader-debug-dump-smoke.md`
- material: `/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial`
- temporary `ConsoleVariables.ini` edits were restored byte-for-byte
- forced-key CVar: `r.SupportSkyAtmosphere=0`
- shaders compiled: `98`
- jobs completed: `98 / 98`
- captured changed/new debug files: `1591` / `95,572,361` bytes
- captured artifacts include generated/debug `.usf`, `.orig.hlsl`, `.rewritten.hlsl`, `.metal`, `.spv`, `.spvasm`, `.dxil`, `ShaderCode.bin`, `DebugCompile.in`, and `DebugCompileArgs.txt`

This proves the material/permutation debug-dump lane, not final D3D12/DXIL/M12 conversion success.

Archive for material shader debug dump smoke:

- `07-archives/20260625-010229-phase4-unreal-native-material-shader-debug-dump-smoke.tar.zst`
- SHA256 `b47cb195927dc51ae6d743147ec27e51c0e23d533ebd1823c3b6d744395fc8e0`
- generated Unreal-side `Engine/Saved/ShaderDebugInfo` removed after archival; captured copy remains in completed result/archive.

## 2026-06-25 D3D target platform probe

`CookGlobalShaders -platform=Windows` was probed from the Mac-built Unreal editor.

- result: `06-results/completed/20260625-phase8-d3d-targetplatform-probe`
- manifest: `00-manifests/phase-8-d3d-targetplatform-probe.md`
- return code: `1`
- UBT platform validation: `Win64 INVALID <UNKNOWN>`
- commandlet warning: `Target platform 'Windows' was not found`
- loaded target platforms: iOS, Mac, tvOS, VisionOS variants
- no built `WindowsTargetPlatform` or `ShaderFormatD3D` module was found under `Engine/Binaries/Mac`

Conclusion: Unreal-native D3D shader compilation is not available in this current Mac editor build. Continue D3D work through Wine DXC, bytecode/cache extraction, M12 probes, or a Windows host/toolchain.

Archive for D3D target platform probe:

- `07-archives/20260625-010720-phase8-d3d-targetplatform-probe.tar.zst`
- SHA256 `e40d5eb0a1e019c5e9ad916c2ee2139d88a916f64708d88153c6695ca0d864d7`

Companion `ShaderFormatD3D` Mac module build probe:

- result: `06-results/completed/20260625-phase8-shaderformatd3d-mac-module-build-probe`
- command: `Build.sh UnrealEditor Mac Development -Module=ShaderFormatD3D -NoUBA -MaxParallelActions=4`
- return code: `6`
- UBT: `Unable to find output items for module 'ShaderFormatD3D'`
- no Mac `ShaderFormatD3D` binary produced

This reinforces that stock Mac Unreal cannot currently produce native D3D shader commandlet output.

Archive for companion `ShaderFormatD3D` Mac module build probe:

- `07-archives/20260625-011103-phase8-shaderformatd3d-mac-module-build-probe.tar.zst`
- SHA256 `1572b1ea0226708aaa28e8260e4efef2825ad071b895efd4083b347fdfa52821`

## 2026-06-25 Nanite DXC + Metal Shader Converter batch 0001

First Nanite-sensitive 100-file batch completed through the bounded standalone DXC runner with MSC enabled.

- result: `06-results/completed/20260625-phase4-nanite-dxc-msc-batch0001-smoke`
- manifest: `00-manifests/phase-4-nanite-dxc-msc-batch0001-smoke.md`
- batch: `nanite_candidate-batch-0001.json`
- shader model: `6_6`
- files: `100`
- DXC attempts: `85`
- DXIL successes: `0`
- MSC conversions: `0`
- statuses: `attempted_failed=32`, `include_header=43`, `no_entrypoint=25`
- failure class: `requires_unreal_generated_uniform_buffers=85`

Conclusion: Nanite standalone `.usf` conversion is blocked by missing Unreal-generated shader environment (`/Engine/Generated/GeneratedUniformBuffers.ush`, `UE_LWC_RENDER_TILE_SIZE`, etc.). This is expected and reinforces that Nanite acceptance requires Unreal-native environment reconstruction or extracted bytecode/cache artifacts.

Archive for Nanite DXC + MSC batch 0001:

- `07-archives/20260625-012332-phase4-nanite-dxc-msc-batch0001-smoke.tar.zst`
- SHA256 `41d041813c083aca06b5b762353a446ffba11a382e678d49805f9f65cf58c414`

## 2026-06-25 Nanite DXC + Metal Shader Converter batch 0002

Second/final Nanite-sensitive batch completed through the bounded standalone DXC runner with MSC enabled.

- result: `06-results/completed/20260625-phase4-nanite-dxc-msc-batch0002-smoke`
- manifest: `00-manifests/phase-4-nanite-dxc-msc-batch0002-smoke.md`
- batch: `nanite_candidate-batch-0002.json`
- files: `2`
- DXC attempts: `1`
- DXIL successes: `0`
- statuses: `attempted_failed=1`, `include_header=1`
- failure class: Unreal-generated shader environment required (`DoubleFloatOperations.ush` standalone mismatch and `/Engine/Generated/GeneratedUniformBuffers.ush` missing)

Nanite standalone `.usf` batches are now fully accounted for: both batches require Unreal-native environment reconstruction or compiled bytecode/cache extraction before real DXIL/MSC/M12 acceptance.

Archive for Nanite DXC + MSC batch 0002:

- `07-archives/20260625-012713-phase4-nanite-dxc-msc-batch0002-smoke.tar.zst`
- SHA256 `852d66233e8f55f877839f99059b405121c66ad5d0c78d8734f250cf38dcbfd7`

## 2026-06-25 Material DXIL reflection inventory

Inventoried Unreal-native `METAL_SM6` material shader debug dump DXIL/reflection artifacts.

- result source: `06-results/completed/20260625-unreal-native-material-shader-debug-dump-smoke/captured-shader-debug`
- inventory: `06-results/completed/20260625-unreal-native-material-shader-debug-dump-smoke/material-dxil-reflection-inventory.{json,md}`
- manifest: `00-manifests/phase-4-material-dxil-reflection-inventory.md`
- DXIL files: `51`
- reflection JSON files: `51`
- shader types: `Fragment=35`, `Vertex=12`, `Compute=4`
- key binding metadata: `TopLevelArgumentBuffer`, `UsedResources`, CBV slot usage, table sentinel entries

Conclusion: Material/permutation extraction should use these Unreal-native DXIL/reflection artifacts for M12 root signature and descriptor mapping validation rather than treating standalone `.usf` DXC guesses as acceptance.

Archive for material DXIL reflection inventory:

- `07-archives/20260625-013324-phase4-material-dxil-reflection-inventory-updated-debugdump.tar.zst`
- SHA256 `ff6cdbe4e7eba971e6ffcfd27f560650e35f684a3fe18f69a44ada65bdbd14ac`

## 2026-06-25 In-progress cleanup

Moved and archived resolved provenance outputs from `06-results/in-progress`:

- `20260624-phase1v2-full-unreal-release` — complete full non-sparse checkout manifest/logs only
- `20260624-phase4-unreal-setup-release` — complete `Setup.sh` dependencies log
- `20260624-phase5-generate-project-files` — complete project generation log
- `20260625-unreal-native-shader-debug-dump-material-config-no-output-smoke` — superseded no-output material debug-dump attempt
- `20260625-unreal-native-shader-debug-dump-material-execcmd-timeout-smoke` — superseded timed-out `-ExecCmds` debug-dump attempt

`06-results/in-progress` is intended to contain only live or not-yet-inspected runs.

## 2026-06-25 SM6 DXC + Metal Shader Converter batch 0002

Second SM6-sensitive 100-file batch completed through bounded standalone Wine DXC with Apple Metal Shader Converter enabled.

- result: `06-results/completed/20260625-phase4-sm6-dxc-msc-batch0002-smoke`
- manifest: `00-manifests/phase-4-sm6-dxc-msc-batch0002-smoke.md`
- batch: `sm6_sensitive_candidate-batch-0002.json`
- files: `100`
- DXC attempts: `195`
- DXIL successes: `0`
- MSC conversions: `0`
- statuses: `attempted_failed=62`, `include_header=27`, `no_entrypoint=11`
- failure classes: `missing_generated_uniform_buffers=194`, `missing entry point definition=1`

Conclusion: SM6 standalone `.usf` batch 0002 is dominated by Unreal-generated shader environment requirements and does not count as M12/MSC acceptance.

Archive for SM6 DXC + MSC batch 0002:

- `07-archives/20260625-015610-phase4-sm6-dxc-msc-batch0002-smoke.tar.zst`
- SHA256 `727c5cbe8e0211bb0efd84cb259d0a1b06d99f0e91d50f63c508c57e689d3817`

## 2026-06-25 SM6 DXC + Metal Shader Converter batch 0003

Third SM6-sensitive 100-file batch completed through bounded standalone Wine DXC with Apple Metal Shader Converter enabled.

- result: `06-results/completed/20260625-phase4-sm6-dxc-msc-batch0003-smoke`
- manifest: `00-manifests/phase-4-sm6-dxc-msc-batch0003-smoke.md`
- batch: `sm6_sensitive_candidate-batch-0003.json`
- files: `100`
- DXC attempts: `101`
- DXIL successes: `0`
- MSC conversions: `0`
- statuses: `include_header=40`, `attempted_failed=32`, `no_entrypoint=28`
- failure class: `missing_generated_uniform_buffers=101`

Conclusion: SM6 standalone `.usf` batch 0003 is entirely blocked by Unreal-generated shader environment requirements and does not count as M12/MSC acceptance.

Archive for SM6 DXC + MSC batch 0003:

- `07-archives/20260625-020712-phase4-sm6-dxc-msc-batch0003-smoke.tar.zst`
- SHA256 `21047233398ea90a0cad85e22e8181c139b751e9d9a68d49966921873df777f4`

## 2026-06-25 SM6 DXC + Metal Shader Converter batch 0004

Fourth SM6-sensitive 100-file batch completed through bounded standalone Wine DXC with Apple Metal Shader Converter enabled.

- result: `06-results/completed/20260625-phase4-sm6-dxc-msc-batch0004-smoke`
- manifest: `00-manifests/phase-4-sm6-dxc-msc-batch0004-smoke.md`
- batch: `sm6_sensitive_candidate-batch-0004.json`
- files: `100`
- DXC attempts: `58`
- DXIL successes: `0`
- MSC conversions: `0`
- statuses: `attempted_failed=29`, `include_header=48`, `no_entrypoint=23`
- failure class: `missing_generated_uniform_buffers=58`

Conclusion: SM6 standalone `.usf` batch 0004 is entirely blocked by Unreal-generated shader environment requirements and does not count as M12/MSC acceptance.

Archive for SM6 DXC + MSC batch 0004:

- `07-archives/20260625-021434-phase4-sm6-dxc-msc-batch0004-smoke.tar.zst`
- SHA256 `4ec001a1ad27726a53917dfb0d683b6d9ed5925e3d43fb0c1b86a0ed80e01aaa`

## 2026-06-25 SM6 DXC + Metal Shader Converter batch 0005

Fifth/final SM6-sensitive batch completed through bounded standalone Wine DXC with Apple Metal Shader Converter enabled.

- result: `06-results/completed/20260625-phase4-sm6-dxc-msc-batch0005-smoke`
- manifest: `00-manifests/phase-4-sm6-dxc-msc-batch0005-smoke.md`
- batch: `sm6_sensitive_candidate-batch-0005.json`
- files: `97`
- DXC attempts: `68`
- DXIL successes: `1`
- MSC conversions: `1`
- success: `Engine/Shaders/Private/UpdateDescriptorHandle.usf` / `MainCS` / `cs_6_6`
- DXIL SHA256: `7515405c89829c495d2021a2986b9736bb8e154931678a317c4eeb8df1a0ce71`
- MetalLib SHA256: `447cbbbb13cf3ab57aeb28bd2a677fc99f7831fe01b1d9ec003f62b5071d3013`
- reflection: SRV slots 0/1, UAV slot 0, CBV slot 0 in `TopLevelArgumentBuffer`
- failures: `missing_generated_uniform_buffers=57`, third-party HLSL/profile mismatch `8`, entrypoint/profile mismatch `2`

Conclusion: SM6 batch 0005 produced one real DXIL -> Apple MSC -> MetalLib/reflection artifact useful for M12 descriptor/UAV/CBV mapping. Most remaining failures remain Unreal-generated shader environment or profile classification issues.

Archive for SM6 DXC + MSC batch 0005:

- `07-archives/20260625-022349-phase4-sm6-dxc-msc-batch0005-smoke.tar.zst`
- SHA256 `b7af40ce0770ed3c44b3706649147a04dd430a9f9937b7741ae6480fa7a5277b`

## 2026-06-25 SM6 DXC + Metal Shader Converter aggregate

All five SM6-sensitive standalone DXC + Apple Metal Shader Converter batches have been run and aggregated.

- aggregate result: `06-results/completed/20260625-phase4-sm6-dxc-msc-aggregate`
- manifest: `00-manifests/phase-4-sm6-dxc-msc-aggregate.md`
- files: `497`
- DXC attempts: `534`
- DXIL successes: `9`
- Apple MSC conversions: `9`
- failures: `525`
- file statuses: `attempted_failed=192`, `include_header=180`, `no_entrypoint=121`, `success=4`
- dominant blocker: `missing_generated_uniform_buffers=504`
- notable SM6.6 success: `UpdateDescriptorHandle.usf` / `MainCS` / `cs_6_6`, features `int64`, `atomics`, reflection has SRV slots `0/1`, UAV slot `0`, CBV slot `0`

Conclusion: SM6-sensitive standalone corpus accounting is complete. The valid DXIL+MSC successes are useful M12 mapping inputs; most remaining source files require Unreal-native generated shader environments and cannot be judged by standalone `.usf` compilation.

Archive for SM6 DXC + MSC aggregate:

- `07-archives/20260625-022707-phase4-sm6-dxc-msc-aggregate.tar.zst`
- SHA256 `125308d267a6b8fafa488e9fa8cce4e8e143f86054cc19224e19eea6a9581ce9`

## 2026-06-25 SM7 discovery

Phase 5 SM7 discovery completed.

- result: `06-results/completed/20260625-phase5-sm7-discovery`
- manifest: `00-manifests/phase-5-sm7-discovery.md`
- shader inventory records: `1987`
- SM7 candidates: `0`
- DXC help profile markers: none for `*_7_*` / Shader Model 7
- source marker scan: no D3D/Unreal SM7 definitions; false positives only (`sm_75` CUDA/IREE, package hashes, Python PKCS_7, generic test names)

Conclusion: SM7 is unavailable/not applicable for the current Unreal `release` checkout and bundled toolchain. M12 must not advertise SM7 support from this roadmap.

Archive for SM7 discovery:

- `07-archives/20260625-022926-phase5-sm7-discovery.tar.zst`
- SHA256 `9c2032b555aadf27e79e57604e6edbc1e99bf9a1110e884a3d51e88cca9bb793`

## 2026-06-25 Phase 8 Unreal-to-M12 probe map

Generated Phase 8 probe/spec artifacts:

- `05-probes/unreal-d3d12-to-m12-probe-map.md`
- `05-probes/nanite-transient-allocation-spec.md`
- `05-probes/sm6-wave-int64-capability-policy.md`
- result copy: `06-results/completed/20260625-phase8-unreal-to-m12-probe-map`
- manifest: `00-manifests/phase-8-unreal-to-m12-probe-map.md`

Source anchors include `ShaderFormatD3D.cpp`, `D3DShaderCompilerDXC.cpp`, `MetalCommon.ush`, `RHICoreTransientResourceAllocator.cpp`, and Nanite candidate-cluster shaders. These artifacts map Unreal expectations to required M12 offline probes and capability-denial policy.

Archive for Phase 8 Unreal-to-M12 probe map:

- `07-archives/20260625-023448-phase8-unreal-to-m12-probe-map.tar.zst`
- SHA256 `d1d965b0b740de299011bf4851edae3b098cf770772bac620b23a32f5d24f5dc`

## 2026-06-25 Phase 9 M12 transfer design

Generated MetalSharp M12 transfer design.

- design: `09-notes/m12-transfer-design.md`
- result copy: `06-results/completed/20260625-phase9-m12-transfer-design`
- manifest: `00-manifests/phase-9-m12-transfer-design.md`
- first Phase 10 target: offline `probe_nanite_transient_allocation` before any live launch

The plan preserves the narrow launch shape and treats SM6.6/WaveOps/int64/atomic64/Nanite-adjacent features as denied unless offline runtime readback probes prove behavior.

Archive for Phase 9 M12 transfer design:

- `/Volumes/AverySSD/MetalSharp-SM6-UE-Lab/07-archives/20260625-024028-phase9-m12-transfer-design.tar.zst`
- SHA256 `44e562197f92483330449d3f1c7e23940671770d03c685b148ef88c70ad4a94b`

## 2026-06-25 — Phase 10 Nanite transient allocation offline probe

- Completed `20260625-phase10-nanite-transient-allocation-probe`; canonical runner returned `0` and probe JSON `pass=true`.
- Validated 128 MiB `Nanite.MainAndPostCandidateClustersBuffer` allocation with observed 64 KiB alignment, raw sentinel Store3/Load3 path, queue/fence completion, and candidate first/tail readback.
- Live Subnautica 2 remains paused pending remaining offline SM6/Nanite gates.
- MetalSharp branch `fix/m12-shader-probe-lab` pushed commit `a15b86d test: add nanite transient allocation probe`.

## 2026-06-25 — Phase 10 SM6 capability honesty

- Completed `20260625-phase10-sm6-capability-honesty`; rebuilt/staged M12 runtime and ran offline capability probes with `DXMT_D3D12_UE_SM6_COMPAT=1`.
- Verified M12 now reports highest shader model `6_5`, `meets_6_6=false`, `WaveOps=false`, `Int64ShaderOps=false`, and all atomic64 feature bits false until runtime readback probes prove behavior.
- Live Subnautica 2 remains paused; remaining gates include runtime WaveOps/int64/atomic64 behavior, descriptor indexing/bindless, reflection ABI, and DXIL semantic UAV readback.
- MetalSharp branch `fix/m12-shader-probe-lab` pushed commit `6dfe1ff fix: deny unproven sm6 runtime caps`.

## 2026-06-25 Phase 15-18 Unreal-generated DXIL replay + M12 graphics fix

Completed fallback/offline proof lane after Phase 14 proved current Mac-host `PCD3D_SM6` commandlets unavailable.

- Manifest: `00-manifests/phase-15-18-unreal-generated-dxil-msc-m12-replay-and-fix.md`
- Phase 15/16 replay: `06-results/in-progress/phase15-16-unreal-generated-dxil-msc-m12-replay-20260625-143503`
- Phase 17 accounting: `06-results/in-progress/phase17-full-sm6-corpus-accounting-20260625-153942`
- Phase 18 final contract rerun: `06-results/in-progress/phase18-m12-runtime-selection-fixed-full-contract-rerun-20260625-153326`

Evidence summary:

- Unreal-generated material DXIL: `51`
- MSC4 conversion: `51/51`
- native Metal offline function/PSO creation: `51/51`
- M12 compute PSO replay: `4/4`
- M12 graphics PSO replay after fix: `35/35`, return code `0`, no timeout
- full M12 required probe contract: `19/19`, compare-contract `PASS`, issues `0`
- raw SM6-sensitive inventory accounted: `497` records assigned to lanes

Fixes made in `metalsharp-m12-lab`:

- M12 DXIL→MSL lowering now treats pointer-form vector reinterpret-casts as vector expressions, fixing `bool = uint4 != 0` style generated code via `any(vector != zero)`.
- `m12-dev.sh probes` now passes the M12 runtime path.
- `run-probes.sh --profile metalsharp` now prefers `dxmt_m12`/`dxmt-m12` over older `dxmt`, preventing stale `out/bin` DLL shadowing.

Claim boundary remains conservative: this proves targeted source-backed Unreal-generated SM6 material DXIL fallback artifacts through offline M12 paths; it does not prove universal Unreal `PCD3D_SM6` commandlet success or live Subnautica 2 success. No live game launch performed.
