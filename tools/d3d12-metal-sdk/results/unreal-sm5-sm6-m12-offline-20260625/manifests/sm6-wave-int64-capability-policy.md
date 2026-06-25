# SM6 Wave / int64 / atomic64 capability policy for M12

Date: 2026-06-25

## Unreal source anchors

D3D shader model selection:

- `Engine/Source/Developer/Windows/ShaderFormatD3D/Private/ShaderFormatD3D.cpp`
  - SM6 features are required by SM6.8, SM6.6, `CFLAG_WaveOperations`, `CFLAG_ForceDXC`, or DXIL preference.
  - `PLATFORM_SUPPORTS_SM6_0_WAVE_OPERATIONS` is set from `bSM6Features`.
  - `SM6_PROFILE` is only true when shader model is `>= SM6_6`.
  - SM6.6+ additionally sets real types, inline ray tracing, static samplers, callable shaders, noinline support.

DXC feature extraction:

- `Engine/Source/Developer/Windows/ShaderFormatD3D/Private/D3DShaderCompilerDXC.cpp`
  - `D3D_SHADER_REQUIRES_WAVE_OPS` -> `EShaderCodeFeatures::WaveOps`
  - `D3D_SHADER_REQUIRES_ATOMIC_INT64_ON_TYPED_RESOURCE` / `...GROUP_SHARED` -> `Atomic64`
  - descriptor heap indexing -> `BindlessResources` / `BindlessSamplers`
  - SM6 DXC shaders receive `EShaderOptionalDataKey::ShaderModel6`

Metal SM6 platform source:

- `Engine/Shaders/Public/Platform/Metal/MetalCommon.ush`
  - `METAL_SM6_PROFILE == 1` sets `PLATFORM_SUPPORTS_SM6_0_WAVE_OPERATIONS 1`
  - `METAL_SM6_PROFILE == 1` sets `COMPILER_SUPPORTS_ULONG_TYPES 1`
  - `METAL_SM6_PROFILE == 1` sets `COMPILER_SUPPORTS_UINT64_IMAGE_ATOMICS 1`
  - defines `ImageInterlockedMaxUInt64`, `ImageInterlockedAddUInt64`, `ImageInterlockedOrUInt64`

Shader source anchors:

- `Engine/Shaders/Public/Platform.ush`
  - `PLATFORM_SUPPORTS_SM6_0_WAVE_OPERATIONS` default false
  - comment says atomic64 mirror tracks `GRHISupportsAtomicUInt64`
- `Engine/Shaders/Public/WaveBroadcastIntrinsics.ush`
- `Engine/Shaders/Private/Nanite/NaniteWritePixel.ush`
  - uses `ImageInterlockedMaxUInt64`
- `Engine/Shaders/Private/Nanite/NaniteHierarchyTraversal.ush`
  - uses wave/interlocked queue allocation patterns

## Corpus evidence

Standalone SM6-sensitive aggregate:

- files: `497`
- DXC attempts: `534`
- DXIL successes: `9`
- Apple MSC conversions: `9`
- dominant source blocker: Unreal-generated shader environment, not M12 acceptance
- notable SM6.6 success:
  - `Engine/Shaders/Private/UpdateDescriptorHandle.usf` / `MainCS` / `cs_6_6`
  - inventory features: `int64`, `atomics`
  - reflection: SRV slots `0/1`, UAV slot `0`, CBV slot `0`

## M12 policy

M12 must not advertise any of these capabilities from compile-only evidence:

- SM6.6 general support
- WaveOps
- int64 arithmetic correctness
- typed-resource atomic64
- groupshared atomic64
- descriptor heap indexing / bindless resources
- mesh/amplification/Nanite-adjacent readiness

A feature may be advertised only when an offline runtime probe proves behavior through M12 and Metal readback/reflection.

## Required probes

1. `probe_sm66_capabilities`
   - Query reported D3D12 options and shader model caps.
   - Fail if M12 reports SM6.6 but wave/int64/atomic64 probes are absent or failing.
2. `probe_wave_ops`
   - WaveActiveSum / WaveReadLaneAt / WavePrefix or equivalent deterministic readback.
3. `probe_int64_atomic64`
   - int64 arithmetic, typed UAV atomic64 add/max/or, groupshared atomic64 where claimed.
4. `probe_descriptor_table_indexing`
   - Non-uniform or dynamic descriptor indexing with SRV/UAV/CBV table correctness.
5. `probe_update_descriptor_handle_sm66`
   - Use the successful `UpdateDescriptorHandle.usf` DXIL/MSC reflection as a minimal descriptor-handle compute case.

## Acceptance

Capability reporting is honest only when every advertised bit has a matching passing offline probe result. Otherwise M12 should force-deny or downlevel the feature even if Unreal/MSC compile-only artifacts exist.
