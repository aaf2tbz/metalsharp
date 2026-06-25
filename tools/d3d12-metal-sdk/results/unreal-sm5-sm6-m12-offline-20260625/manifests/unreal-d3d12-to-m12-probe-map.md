# Unreal D3D12 / SM6 / Nanite expectations to M12 probe map

Date: 2026-06-25

## Evidence sources

Completed lab evidence:

- Unreal-native SM5/SM6 Mac commandlet smoke compiled `17104/17104` Mac Metal shader jobs.
- Stock Mac Unreal cannot produce native D3D shader output:
  - `Target platform 'Windows' was not found`
  - no `ShaderFormatD3D` Mac module
- Standalone SM6-sensitive DXC+MSC aggregate:
  - `497` files accounted
  - `9` DXIL+MSC successes
  - most failures require Unreal-generated shader environment
- Nanite standalone batches accounted:
  - `102` files
  - `0` DXIL successes
  - dominant blocker: generated shader environment
- Material debug dump produced Unreal-native material DXIL/reflection:
  - `51` DXIL files
  - `51` reflection JSON files
  - `TopLevelArgumentBuffer` and `UsedResources` metadata

Unreal source anchors:

- `ShaderFormatD3D.cpp`: SM6 feature/profile define policy
- `D3DShaderCompiler.cpp`: SM6.0/6.6/6.8 profile strings including mesh/amplification profiles
- `D3DShaderCompilerDXC.cpp`: WaveOps, Atomic64, BindlessResources/Samplers optional feature extraction
- `MetalCommon.ush`: Metal SM6 wave + uint64 image atomic definitions
- `RHICoreTransientResourceAllocator.cpp`: transient buffer allocation fatal path
- Nanite shaders: candidate cluster buffers, wave interlocked queue offsets, atomic64 image writes

## Probe map

| Unreal expectation | Evidence | M12 area | Required offline probe / action |
|---|---|---|---|
| SM6 profile must not imply all SM6.6 features | `ShaderFormatD3D.cpp` separates SM6.0 features from `SM6_PROFILE >= SM6_6` | `d3d12_device.*`, caps reporting | `probe_sm66_capabilities`; deny SM6.6 unless behavior-proven |
| Wave operations gate shader paths | `PLATFORM_SUPPORTS_SM6_0_WAVE_OPERATIONS`, wave intrinsics shaders | `airconv/dxil`, MSL/MSC path, caps | `probe_wave_ops` with readback |
| Atomic64 is a real shader-code feature | DXC feature extraction and Metal SM6 `ImageInterlocked*UInt64` | `airconv/dxil`, `m12core`, UAV barriers | `probe_int64_atomic64`; typed and groupshared variants if advertised |
| Descriptor heap indexing / bindless | DXC extracts `BindlessResources` / `BindlessSamplers`; RHI bindless helpers | `d3d12_descriptor_heap.*`, `d3d12_root_signature.*`, `m12core` argument buffers | `probe_descriptor_table_indexing`; compare MSC `TopLevelArgumentBuffer` |
| Root signature + top-level argument buffer alignment | Material DXIL/reflection and `UpdateDescriptorHandle` reflection | `d3d12_root_signature.*`, `d3d12_pipeline_state.*`, `m12core_metal.c` | Reflection ABI probe comparing DXIL root signature to MSC reflection and M12 binding layout |
| Nanite candidate buffer allocation | live fatal 128 MiB / 64 KiB candidate buffer; RHICore allocator fatal | `d3d12_resource.*`, `d3d12_heap.*`, `m12core` allocation | `probe_nanite_transient_allocation` with 128 MiB buffer and alias/fence pressure |
| Nanite queue/candidate correctness | Nanite shaders use `WaveInterlockedAdd_`, `MaxCandidateClusters`, ByteAddressBuffer `Load3/Store3` | UAV, raw buffer load/store, barriers | Nanite candidate-cluster compute microprobe with readback |
| Mesh/amplification profiles exist in D3D compiler for SM6.6/6.8 | `D3DShaderCompiler.cpp` emits `ms_6_6`, `as_6_6`, `ms_6_8`, `as_6_8` | caps and PSO creation | deny mesh/amplification unless a dedicated M12 mesh/amplification path exists and passes |
| Unreal-generated shader environment is mandatory | SM6/Nanite standalone failures dominated by generated uniform buffers | pipeline/tooling | do not count standalone `.usf` failures as M12 failures; use debug/preprocess/cache lanes |

## Immediate MetalSharp transfer targets

- `vendor/dxmt/src/d3d12/d3d12_device.*`
  - capability honesty / feature query responses
- `vendor/dxmt/src/d3d12/d3d12_descriptor_heap.*`
  - SRV/UAV/CBV descriptor indexing and table mutation correctness
- `vendor/dxmt/src/d3d12/d3d12_root_signature.*`
  - root signature to MSC/M12 top-level argument buffer mapping
- `vendor/dxmt/src/d3d12/d3d12_pipeline_state.*`
  - PSO shader feature validation and denial
- `vendor/dxmt/src/d3d12/d3d12_resource.*`, `d3d12_heap.*`
  - transient/Nanite-sized allocation behavior
- `vendor/dxmt/src/airconv/dxil/*`
  - int64/atomic/wave lowering or pass-through strategy
- `vendor/dxmt/src/m12core/*`
  - Metal resource, argument buffer, UAV, fence, and allocation semantics
- `tools/d3d12-metal-sdk/probes/*`
  - implement/repair offline gates above

## Acceptance policy

No live Subnautica 2 launch should resume until offline probes prove or honestly deny:

- 128 MiB / 64 KiB Nanite transient buffer path
- WaveOps correctness
- int64/atomic64 correctness
- descriptor table indexing / bindless behavior
- root signature / MSC reflection / M12 top-level argument buffer alignment
- DXIL semantic UAV readback correctness
