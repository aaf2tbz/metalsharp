# D3D12 Offline ABI Surface Matrix

Date: 2026-06-12

Scope: phase 6 of the offline D3D12 finish roadmap. This file uses GPTK PE
decompilation evidence only for exported entry points, object category names,
and dispatch/vtable surface. It does not treat GPTK PE unix-dispatch bodies as
implementation algorithms.

## Evidence Inputs

- Offline GPTK markdown:
  - `/Users/alexmondello/Desktop/dx12-ghidra-analysis/markdown/gptk-d3d12.md`
  - `/Users/alexmondello/Desktop/dx12-ghidra-analysis/markdown/gptk-d3d11.md`
  - `/Users/alexmondello/Desktop/dx12-ghidra-analysis/markdown/gptk-dxgi.md`
- DXMT source:
  - `vendor/dxmt/src/d3d12`
  - `vendor/dxmt/src/d3d11`
  - `vendor/dxmt/src/dxgi`
- Built DXMT PE artifacts:
  - `vendor/dxmt/build-metalsharp-x64/src/d3d12/d3d12.dll`
  - `vendor/dxmt/build-metalsharp-x64/src/dxgi/dxgi_dxmt.dll`

## Export Surface

Validated with:

```sh
x86_64-w64-mingw32-objdump -p vendor/dxmt/build-metalsharp-x64/src/d3d12/d3d12.dll
x86_64-w64-mingw32-objdump -p vendor/dxmt/build-metalsharp-x64/src/dxgi/dxgi_dxmt.dll
```

| Surface | GPTK evidence | DXMT evidence | Status |
| --- | --- | --- | --- |
| `D3D12CreateDevice` | `gptk-d3d12.md` names `D3D12CreateDevice`; GPTK dispatch table includes `GFXT_CreateD3D12Device_Type`. | Exported by built `d3d12.dll`; implemented in `vendor/dxmt/src/d3d12/d3d12.cpp`. | Covered |
| Root signature deserializers | GPTK D3D12 exposes D3D12 root-signature object/serializer surface. | Built `d3d12.dll` exports `D3D12CreateRootSignatureDeserializer`, `D3D12CreateVersionedRootSignatureDeserializer`, `D3D12SerializeRootSignature`, and `D3D12SerializeVersionedRootSignature`. | Covered |
| D3D12 SDK helpers | D3D12 Agility-style loader surface must tolerate SDK path/version queries. | Built `d3d12.dll` exports `D3D12SDKPath` and `D3D12SDKVersion`. | Covered |
| D3D12 debug/interface helpers | GPTK D3D12 includes debug/interface labels and public IIDs. | Built `d3d12.dll` exports `D3D12GetDebugInterface`, `D3D12GetInterface`, and `D3D12EnableExperimentalFeatures`. | Covered |
| `CreateDXGIFactory*` | `gptk-dxgi.md` names `CreateDXGIFactory`, `CreateDXGIFactory1`, and `CreateDXGIFactory2`. | Built `dxgi_dxmt.dll` exports all three; `vendor/dxmt/src/dxgi/dxgi.def` declares ordinals 9-11. | Covered |
| DXGI debug helpers | GPTK DXGI has debug/interface public surface. | Built `dxgi_dxmt.dll` exports `DXGIGetDebugInterface` and `DXGIGetDebugInterface1`. | Covered |

## D3D12 Object Surface

| GPTK object category evidence | DXMT source surface | Status |
| --- | --- | --- |
| `D3D12Device`, `ID3D12Device`, unix `d3d12_device` markers | `MTLD3D12Device : public ID3D12Device12Compat` in `vendor/dxmt/src/d3d12/d3d12_device.hpp`; `QueryInterface` covers `ID3D12Device` through `ID3D12Device12`. | Covered |
| `D3D12CommandQueue`, unix `d3d12_command_queue` markers | `MTLD3D12CommandQueue : public ID3D12CommandQueue` in `vendor/dxmt/src/d3d12/d3d12_command_queue.hpp`. | Covered |
| `D3D12GraphicsCommandListMTL`, `ID3D12GraphicsCommandList`, unix `d3d12_command_list` markers | `MTLD3D12GraphicsCommandList : public ID3D12GraphicsCommandList6` in `vendor/dxmt/src/d3d12/d3d12_command_list.hpp`. | Covered |
| `D3D12PipelineState`, unix `d3d12_pipeline_state` markers | `MTLD3D12PipelineState : public ID3D12PipelineState` in `vendor/dxmt/src/d3d12/d3d12_pipeline_state.hpp`. | Covered |
| unix `d3d12_root_signature` markers | `MTLD3D12RootSignature : public ID3D12RootSignature` in `vendor/dxmt/src/d3d12/d3d12_root_signature.hpp`. | Covered |
| unix `d3d12_descriptor_heap` markers | `MTLD3D12DescriptorHeap : public ID3D12DescriptorHeap` in `vendor/dxmt/src/d3d12/d3d12_descriptor_heap.hpp`. | Covered |
| unix `d3d12_buffer` / `d3d12_texture` markers | `MTLD3D12Resource : public ID3D12Resource` in `vendor/dxmt/src/d3d12/d3d12_resource.hpp`; resource creation paths are in `d3d12_device.cpp`. | Covered |
| unix `d3d12_fence` markers | `MTLD3D12Fence : public ID3D12Fence` in `vendor/dxmt/src/d3d12/d3d12_fence.hpp`. | Covered |
| command allocator/signature/query heap/pipeline library/heap markers | DXMT has `d3d12_command_allocator`, `d3d12_device.cpp` command signature and pipeline library classes, `d3d12_query_heap.hpp`, and `d3d12_heap.hpp`. | Covered |
| state object / acceleration structure / work graph markers | DXMT has partial `MTLD3D12StateObject` and related no-op/compat surfaces in `d3d12_device.cpp`; full DXR/work-graph behavior remains out of scope for the current vertex/PSO repair. | Partial, acceptable for current goal |

## D3D11 Object Surface

GPTK D3D11/DXGI evidence names unix `d3d11_device`, `d3d11_device_context`,
`d3d11_input_layout`, buffers, textures, shaders, views, and state objects.
DXMT has matching D3D11 object categories:

| GPTK object category evidence | DXMT source surface | Status |
| --- | --- | --- |
| D3D11 device | `MTLD3D11Device : public ID3D11Device5` in `vendor/dxmt/src/d3d11/d3d11_device.hpp`; implementation in `d3d11_device.cpp`. | Covered |
| D3D11 device context | `MTLD3D11DeviceContextImplBase` in `vendor/dxmt/src/d3d11/d3d11_context_impl.cpp`. | Covered |
| D3D11 input layout | `MTLD3D11InputLayout` in `vendor/dxmt/src/d3d11/d3d11_pipeline_cache.cpp`; public input-layout declarations in `d3d11_input_layout.hpp`. | Covered |
| Buffers/textures/views | D3D11 buffer, texture, RTV, DSV, SRV, and UAV sources exist under `vendor/dxmt/src/d3d11`. | Covered |
| Shaders | D3D11 shader object/cache code exists under `vendor/dxmt/src/d3d11`, with DXBC conversion through the established DXMT shader path. | Covered |
| States | `d3d11_state_object.cpp` implements sampler, blend, rasterizer, and depth/stencil state objects. | Covered |

## Translation Rules Confirmed

- Translate GPTK evidence as ABI and object-boundary confirmation only.
- Do not copy `__wine_unix_call_dispatcher` bodies into DXMT.
- Do not treat GPTK PE vtable setup as evidence for Metal binding behavior.
- Do not infer vertex pulling or MSL lowering algorithms from GPTK PE stubs.
- Continue using DXMT D3D11's own vertex/input-layout contract as the algorithmic reference for DXMT D3D12.

## Phase 6 Result

The public D3D12/DXGI export surface and the D3D12/D3D11 object categories that
matter for the current DX12 vertex/PSO repair are present in DXMT. GPTK provides
no stronger offline evidence that should override the phase 1-5 DXMT-internal
vertex metadata, PSO, and queue replay contracts.
