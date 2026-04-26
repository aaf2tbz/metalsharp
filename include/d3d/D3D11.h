#pragma once

#include <metalsharp/Platform.h>
#include <metalsharp/MetalBackend.h>

typedef UINT DXGI_FORMAT;

static const GUID IID_ID3D11Device = {0x2e8f6e0c, 0x3e86, 0x4a63, {0x9e, 0xa5, 0x7b, 0x52, 0xd9, 0x5e, 0x08, 0xc1}};
static const GUID IID_ID3D11DeviceContext = {0xaec81fb3, 0x4e01, 0x4dfe, {0xa0, 0xa3, 0xd0, 0xa9, 0x75, 0x7d, 0x88, 0x6b}};
static const GUID IID_ID3D11DeviceChild = {0xdb6f6ddb, 0xac77, 0x4e88, {0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40}};
static const GUID IID_ID3D11Buffer = {0x4eddea9b, 0x3239, 0x4e14, {0x8c, 0xb4, 0x4e, 0xe4, 0x3d, 0x72, 0x0d, 0x50}};
static const GUID IID_ID3D11Texture2D = {0x6f15aaf2, 0xd208, 0x4e89, {0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c}};
static const GUID IID_ID3D11RenderTargetView = {0xdfdba067, 0x0b8d, 0x4865, {0x87, 0x5b, 0x5d, 0x1e, 0x9c, 0x88, 0x4e, 0x36}};
static const GUID IID_ID3D11DepthStencilView = {0x9fdac92a, 0x187f, 0x4f1e, {0xa7, 0x24, 0x55, 0x8c, 0x1c, 0x8b, 0x2b, 0x49}};
static const GUID IID_ID3D11ShaderResourceView = {0xb0e06fe0, 0x8192, 0x4e05, {0xa9, 0x81, 0x22, 0x1a, 0x45, 0x6f, 0x10, 0x73}};
static const GUID IID_ID3D11InputLayout = {0xe0aac9bd, 0xd237, 0x4d80, {0xa8, 0x2f, 0x6e, 0x88, 0x6c, 0x8e, 0x54, 0x83}};
static const GUID IID_ID3D11VertexShader = {0xe4e6e4c1, 0x3a30, 0x4a65, {0x9c, 0x41, 0x82, 0xbc, 0x91, 0x5b, 0x62, 0x54}};
static const GUID IID_ID3D11PixelShader = {0xea82e2e4, 0x4e41, 0x4e80, {0xa8, 0xf7, 0x77, 0x81, 0x1f, 0xbe, 0x6c, 0x82}};
static const GUID IID_ID3D11Resource = {0xdb8d2769, 0x6ef3, 0x44ee, {0x8b, 0x8e, 0x10, 0x8f, 0x33, 0x36, 0x6d, 0xe0}};
static const GUID IID_ID3D11View = {0x839d1216, 0xbb2e, 0x412b, {0xb7, 0xf4, 0xa9, 0xdb, 0xeb, 0xe0, 0x8e, 0xd1}};

class ID3D11Device;
class ID3D11DeviceContext;
class ID3D11ClassInstance;
class ID3D11ClassLinkage;
class ID3D11SamplerState;
class ID3D11RasterizerState;
class ID3D11DepthStencilState;
class ID3D11BlendState;
class ID3D11UnorderedAccessView;

class MIDL_INTERFACE("db6f6ddb-ac77-4e88-8253-819df9bbf140")
ID3D11DeviceChild : public IUnknown {
public:
    STDMETHOD(GetDevice)(THIS_ struct ID3D11Device** ppDevice) PURE;
    STDMETHOD(GetPrivateData)(THIS_ const GUID& guid, UINT* pDataSize, void* pData) PURE;
    STDMETHOD(SetPrivateData)(THIS_ const GUID& guid, UINT DataSize, const void* pData) PURE;
    STDMETHOD(SetPrivateDataInterface)(THIS_ const GUID& guid, const IUnknown* pData) PURE;
};

struct D3D11_BUFFER_DESC {
    UINT ByteWidth;
    UINT Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
    UINT StructureByteStride;
};

struct D3D11_SUBRESOURCE_DATA {
    const void* pSysMem;
    UINT SysMemPitch;
    UINT SysMemSlicePitch;
};

struct D3D11_TEXTURE2D_DESC {
    UINT Width;
    UINT Height;
    UINT MipLevels;
    UINT ArraySize;
    DXGI_FORMAT Format;
    UINT SampleDesc;
    UINT Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
};

typedef struct { UINT Width; UINT Height; } D3D11_VIEWPORT;

class ID3D11Resource : public ID3D11DeviceChild {
public:
    STDMETHOD(GetType)(THIS_ UINT* pResourceDimension) PURE;
    STDMETHOD(SetEvictionPriority)(THIS_ UINT EvictionPriority) PURE;
    STDMETHOD_(UINT, GetEvictionPriority)(THIS) PURE;
};

class ID3D11Buffer : public ID3D11Resource {
};

class ID3D11Texture2D : public ID3D11Resource {
};

class ID3D11View : public ID3D11DeviceChild {
public:
    STDMETHOD(GetResource)(THIS_ ID3D11Resource** ppResource) PURE;
};

class ID3D11RenderTargetView : public ID3D11View {
};

class ID3D11DepthStencilView : public ID3D11View {
};

class ID3D11ShaderResourceView : public ID3D11View {
};

class ID3D11UnorderedAccessView : public ID3D11View {
};

class ID3D11InputLayout : public ID3D11DeviceChild {
};

class ID3D11VertexShader : public ID3D11DeviceChild {
};

class ID3D11PixelShader : public ID3D11DeviceChild {
};

class ID3D11GeometryShader : public ID3D11DeviceChild {
};

class ID3D11ComputeShader : public ID3D11DeviceChild {
};

class ID3D11HullShader : public ID3D11DeviceChild {
};

class ID3D11DomainShader : public ID3D11DeviceChild {
};

class MIDL_INTERFACE("aec81fb3-4e01-4dfe-a0a3-d0a9757d886b")
ID3D11DeviceContext : public ID3D11DeviceChild {
public:
    STDMETHOD(VSSetConstantBuffers)(THIS_ UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) PURE;
    STDMETHOD(PSSetShaderResources)(THIS_ UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) PURE;
    STDMETHOD(PSSetShader)(THIS_ ID3D11PixelShader* pPixelShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) PURE;
    STDMETHOD(PSSetSamplers)(THIS_ UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) PURE;
    STDMETHOD(VSSetShader)(THIS_ ID3D11VertexShader* pVertexShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) PURE;
    STDMETHOD(DrawIndexed)(THIS_ UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) PURE;
    STDMETHOD(Draw)(THIS_ UINT VertexCount, UINT StartVertexLocation) PURE;
    STDMETHOD(Map)(THIS_ ID3D11Resource* pResource, UINT Subresource, UINT MapType, UINT MapFlags, void* pMappedResource) PURE;
    STDMETHOD(Unmap)(THIS_ ID3D11Resource* pResource, UINT Subresource) PURE;
    STDMETHOD(PSSetConstantBuffers)(THIS_ UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) PURE;
    STDMETHOD(IASetInputLayout)(THIS_ ID3D11InputLayout* pInputLayout) PURE;
    STDMETHOD(IASetVertexBuffers)(THIS_ UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppVertexBuffers, const UINT* pStrides, const UINT* pOffsets) PURE;
    STDMETHOD(IASetIndexBuffer)(THIS_ ID3D11Buffer* pIndexBuffer, DXGI_FORMAT Format, UINT Offset) PURE;
    STDMETHOD(DrawIndexedInstanced)(THIS_ UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) PURE;
    STDMETHOD(DrawInstanced)(THIS_ UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) PURE;
    STDMETHOD(GSSetShader)(THIS_ ID3D11GeometryShader* pShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) PURE;
    STDMETHOD(IASetPrimitiveTopology)(THIS_ UINT Topology) PURE;
    STDMETHOD(VSSetShaderResources)(THIS_ UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) PURE;
    STDMETHOD(VSSetSamplers)(THIS_ UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) PURE;
    STDMETHOD(OMSetRenderTargets)(THIS_ UINT NumViews, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView) PURE;
    STDMETHOD(OMSetRenderTargetsAndUnorderedAccessViews)(THIS_ UINT NumRTVs, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUnorderedAccessViews, const UINT* pUAVInitialCounts) PURE;
    STDMETHOD(OMSetBlendState)(THIS_ ID3D11BlendState* pBlendState, const FLOAT BlendFactor[4], UINT SampleMask) PURE;
    STDMETHOD(OMSetDepthStencilState)(THIS_ ID3D11DepthStencilState* pDepthStencilState, UINT StencilRef) PURE;
    STDMETHOD(RSSetState)(THIS_ ID3D11RasterizerState* pRasterizerState) PURE;
    STDMETHOD(RSSetViewports)(THIS_ UINT NumViewports, const D3D11_VIEWPORT* pViewports) PURE;
    STDMETHOD(RSSetScissorRects)(THIS_ UINT NumRects, const RECT* pRects) PURE;
    STDMETHOD(ClearRenderTargetView)(THIS_ ID3D11RenderTargetView* pRenderTargetView, const FLOAT ColorRGBA[4]) PURE;
    STDMETHOD(ClearDepthStencilView)(THIS_ ID3D11DepthStencilView* pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil) PURE;
    STDMETHOD(GenerateMips)(THIS_ ID3D11ShaderResourceView* pShaderResourceView) PURE;
    STDMETHOD(CSSetShader)(THIS_ ID3D11ComputeShader* pComputeShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) PURE;
    STDMETHOD(CSSetConstantBuffers)(THIS_ UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) PURE;
    STDMETHOD(CSSetShaderResources)(THIS_ UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) PURE;
    STDMETHOD(CSSetUnorderedAccessViews)(THIS_ UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUnorderedAccessViews, const UINT* pUAVInitialCounts) PURE;
    STDMETHOD(CSSetSamplers)(THIS_ UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) PURE;
    STDMETHOD(Dispatch)(THIS_ UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) PURE;
    STDMETHOD(CopyResource)(THIS_ ID3D11Resource* pDstResource, ID3D11Resource* pSrcResource) PURE;
    STDMETHOD(UpdateSubresource)(THIS_ ID3D11Resource* pDstResource, UINT DstSubresource, const void* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch) PURE;
};

class ID3D11SamplerState;
class ID3D11RasterizerState;
class ID3D11DepthStencilState;
class ID3D11BlendState;
class ID3D11ClassInstance;

typedef struct {
    UINT BufferCount;
    UINT Width;
    UINT Height;
    DXGI_FORMAT Format;
    UINT Windowed;
} DXGI_SWAP_CHAIN_DESC;

class MIDL_INTERFACE("2e8f6e0c-3e86-4a63-9ea5-7b52d95e08c1")
ID3D11Device : public IUnknown {
public:
    STDMETHOD(CreateBuffer)(THIS_ const D3D11_BUFFER_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Buffer** ppBuffer) PURE;
    STDMETHOD(CreateTexture2D)(THIS_ const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D) PURE;
    STDMETHOD(CreateShaderResourceView)(THIS_ ID3D11Resource* pResource, const void* pDesc, ID3D11ShaderResourceView** ppSRView) PURE;
    STDMETHOD(CreateRenderTargetView)(THIS_ ID3D11Resource* pResource, const void* pDesc, ID3D11RenderTargetView** ppRTView) PURE;
    STDMETHOD(CreateDepthStencilView)(THIS_ ID3D11Resource* pResource, const void* pDesc, ID3D11DepthStencilView** ppDepthStencilView) PURE;
    STDMETHOD(CreateVertexShader)(THIS_ const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11VertexShader** ppVertexShader) PURE;
    STDMETHOD(CreatePixelShader)(THIS_ const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11PixelShader** ppPixelShader) PURE;
    STDMETHOD(CreateGeometryShader)(THIS_ const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11GeometryShader** ppGeometryShader) PURE;
    STDMETHOD(CreateComputeShader)(THIS_ const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11ComputeShader** ppComputeShader) PURE;
    STDMETHOD(CreateInputLayout)(THIS_ const void* pInputElementDescs, UINT NumElements, const void* pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, ID3D11InputLayout** ppInputLayout) PURE;
    STDMETHOD(CreateSamplerState)(THIS_ const void* pSamplerDesc, ID3D11SamplerState** ppSamplerState) PURE;
    STDMETHOD(CreateRasterizerState)(THIS_ const void* pRasterizerDesc, ID3D11RasterizerState** ppRasterizerState) PURE;
    STDMETHOD(CreateDepthStencilState)(THIS_ const void* pDepthStencilDesc, ID3D11DepthStencilState** ppDepthStencilState) PURE;
    STDMETHOD(CreateBlendState)(THIS_ const void* pBlendStateDesc, ID3D11BlendState** ppBlendState) PURE;
    STDMETHOD_(void, GetImmediateContext)(THIS_ ID3D11DeviceContext** ppImmediateContext) PURE;
    STDMETHOD(GetDeviceFeatureLevel)(THIS_ UINT* pFeatureLevel) PURE;
};

class ID3D11ClassLinkage;
