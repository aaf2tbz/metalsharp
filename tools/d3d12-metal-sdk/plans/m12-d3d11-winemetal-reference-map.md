# D3D10/11 Winemetal Reference Map for M12

D3D10/11 are the working reference for how DXMT expects native Metal work to
flow through `winemetal.dll` and `winemetal.so`. D3D12 must extend this model,
not bypass it with a separate sidecar runtime.

## Loading model

| Working D3D10/11 path | D3D12/M12 target |
|---|---|
| `d3d10core.dll` layers onto `d3d11.dll` | `d3d12.dll` stays the D3D12 COM/API frontend |
| `d3d11.dll` links `winemetal_dep` | `d3d12.dll` already links `winemetal_dep`; keep that as the public shell |
| `winemetal.dll` exports PE thunks | keep `WMTM12Core*` PE thunks compatible |
| `winemetal.so` performs native Metal calls | internalize M12 logic so `winemetal.so` remains the only native Metal authority |

Relevant build files:

- `vendor/dxmt/src/d3d10/meson.build`
- `vendor/dxmt/src/d3d11/meson.build`
- `vendor/dxmt/src/d3d12/meson.build`
- `vendor/dxmt/src/winemetal/meson.build`
- `vendor/dxmt/src/winemetal/unix/meson.build`

## Backend substrate reference

| Area | D3D10/11 / DXMT reference | D3D12/M12 application |
|---|---|---|
| WMT handles | `vendor/dxmt/src/winemetal/winemetal.h`, `vendor/dxmt/src/winemetal/Metal.hpp` | continue passing opaque WMT handles through PE/unix thunks; do not pass C++ objects across PE/native boundary |
| Device creation | `MTLD3D11Device::GetMTLDevice()`, WMT device wrappers | D3D12 device should use the same WMT device identity and native `MTLDevice` ownership in `winemetal.so` |
| Command queue | `vendor/dxmt/src/dxmt/dxmt_command_queue.cpp` | map D3D12 direct/compute/copy queues onto compatible WMT command queue/chunk lifecycle |
| Command buffers | `CommandQueue::CommitCurrentChunk`, `CommandQueue::CommitChunkInternal` | D3D12 `ExecuteCommandLists` should feed a coherent WMT command-buffer lifecycle instead of sidecar replay ownership |
| Async encoding | `EncodingThread`, `CommandChunk`, `ASYNC_ENCODING` | D3D12 command replay/encoding should reuse or mirror the proven async encoding model |
| Completion | `WaitForFinishThread`, shared event listener, command buffer status | D3D12 fences/timeline waits must align with WMT completion and shared-event sequencing |
| Resource initialization | `dxmt_resource_initializer`, staging allocators in `CommandQueue` | D3D12 placed/default/upload/readback resources should extend these staging/init paths |
| Argument buffers | `argument_encoding_ctx`, `argbuf_allocator` | D3D12 descriptor heaps/root tables should map into the existing WMT argument-buffer model where possible |
| Shader cache | `vendor/dxmt/src/d3d11/d3d11_pipeline_cache.cpp`, `dxmt_shader_cache` | M12 DXIL->MSL outputs and metallibs should use a single cache policy behind `winemetal.so` |
| Pipeline cache | `PipelineCache`, compiled graphics/compute pipeline maps | M12 render/compute PSO creation and cache lookup should be unified under the same native backend authority |
| Texture creation | `vendor/dxmt/src/d3d11/d3d11_texture_device.cpp`, `dxmt_texture` | D3D12 resources/views should reuse format/usage/lifetime patterns while adding explicit-state tracking |
| Texture views | WMT `MTLTexture_newTextureView` path | add D3D12 typeless/depth/stencil compatibility validation before native Metal throws |
| Present | `vendor/dxmt/src/dxmt/dxmt_presenter.cpp`, D3D11 swapchain code | D3D12 DXGI swapchain present should use the proven drawable/presenter mechanics with D3D12 backbuffer state handling |

## D3D12-specific semantics not provided by D3D10/11

D3D10/11 are a backend oracle, not a complete D3D12 implementation. M12 must add
or preserve:

- explicit resource states and barriers
- command lists, command allocators, and `ExecuteCommandLists`
- descriptor heaps, descriptor tables, root descriptors, root constants
- root signatures and static samplers
- GPU virtual addresses
- D3D12 fences and timeline values
- DXIL/SM6 lowering and reflection
- D3D12 PSO stream semantics
- bindless-style descriptor behavior
- `ExecuteIndirect`
- placed resources/heaps and more explicit residency/lifetime rules

## Practical rule

When a D3D12 feature needs native Metal execution, first ask:

```text
How does D3D11 already call this through WMT/winemetal?
```

Then add the D3D12 translation layer on top. Do not create a second native
runtime authority outside `winemetal.so`.
