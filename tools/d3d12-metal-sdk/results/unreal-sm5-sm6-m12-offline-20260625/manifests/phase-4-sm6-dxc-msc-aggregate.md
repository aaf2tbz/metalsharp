# Phase 4 SM6 DXC + Metal Shader Converter Aggregate

Date: 2026-06-25

## Purpose

Roll up all five SM6-sensitive standalone DXC + Apple Metal Shader Converter batches into one corpus-accounting status.

## Inputs

- `sm6_sensitive_candidate-batch-0001.json` through `sm6_sensitive_candidate-batch-0005.json`
- Batch 0001 result embedded in `06-results/completed/20260625-metal-shader-converter-special-render-artifact-lanes/phase4-sm6-dxc-msc-batch0001-smoke-20260625-000847`
- Batches 0002-0005 top-level completed result dirs under `06-results/completed/20260625-phase4-sm6-dxc-msc-batch000{2,3,4,5}-smoke`

Aggregate result:

`06-results/completed/20260625-phase4-sm6-dxc-msc-aggregate`

## Corpus totals

- batches included: `5`
- SM6-sensitive files: `497`
- DXC attempts: `534`
- successful DXIL attempts: `9`
- Apple Metal Shader Converter conversions: `9`
- failed DXC attempts: `525`
- skipped/classified files: `301`

File statuses across batches:

- `attempted_failed=192`
- `include_header=180`
- `no_entrypoint=121`
- `success=4`

## Successes

All successes produced real DXIL plus `metal-shaderconverter --validateAll` MetalLib/reflection output.

- Batch 0001: 8 `cs_6_0` successes
  - `PVLightDetectionCS.usf` / `MainCS`
  - `PVLightVectorCalculationCS.usf` / `MainCS`
  - `RivermaxShaders.usf` entries:
    - `RGB10BitToRGBACS`
    - `RGB12BitToRGBACS`
    - `RGB16fBitToRGBACS`
    - `RGB8BitToRGBA8CS`
    - `RGBToRGB10BitCS`
    - `RGBToRGB12BitCS`
- Batch 0005: 1 `cs_6_6` success
  - `UpdateDescriptorHandle.usf` / `MainCS`
  - inventory features: `int64`, `atomics`
  - DXIL SHA256: `7515405c89829c495d2021a2986b9736bb8e154931678a317c4eeb8df1a0ce71`
  - MetalLib SHA256: `447cbbbb13cf3ab57aeb28bd2a677fc99f7831fe01b1d9ec003f62b5071d3013`
  - reflection: SRV slots `0/1`, UAV slot `0`, CBV slot `0` in `TopLevelArgumentBuffer`

## Failure classes

- `missing_generated_uniform_buffers=504`
- `other_compile_failure=14`
- `entrypoint_or_profile_guess_mismatch=3`
- `hlsl_vector_scalar_operator_mismatch=3`
- `unreal_lwc_or_generated_define_missing=1`

## Interpretation

The standalone SM6-sensitive corpus is fully accounted for, but it is **not** an Unreal/M12 acceptance result. The reduced standalone lane proves Apple MSC conversion for 9 valid DXIL outputs and provides binding/reflection data for M12 mapping, while the dominant blocker remains Unreal-generated shader environment state (`/Engine/Generated/GeneratedUniformBuffers.ush`, generated uniform buffer includes, LWC/DoubleFloat generated context, material/vertex factory permutations).

Next acceptance-grade lanes should use:

- Unreal-native debug/preprocess output,
- material/permutation debug dump artifacts,
- compiled shader cache/bytecode extraction,
- Windows-native D3D shader tooling if available,
- then M12 root-signature/descriptor/UAV/reflection offline probes.

## Archive

Archived bundle:

`07-archives/20260625-022707-phase4-sm6-dxc-msc-aggregate.tar.zst`

SHA256:

`125308d267a6b8fafa488e9fa8cce4e8e143f86054cc19224e19eea6a9581ce9`
