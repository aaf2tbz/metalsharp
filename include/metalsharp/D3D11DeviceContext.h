#pragma once

#include <d3d/D3D11.h>
#include <metalsharp/MetalBackend.h>
#include <metalsharp/PipelineState.h>
#include <array>
#include <memory>

namespace metalsharp {

class D3D11Device;

class D3D11DeviceContext : public ID3D11DeviceContext {
public:
    explicit D3D11DeviceContext(D3D11Device& device);
    virtual ~D3D11DeviceContext() = default;

    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObject) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;
    STDMETHOD(GetDevice)(ID3D11Device** ppDevice) override;
    STDMETHOD(GetPrivateData)(const GUID& guid, UINT* pDataSize, void* pData) override;
    STDMETHOD(SetPrivateData)(const GUID& guid, UINT DataSize, const void* pData) override;
    STDMETHOD(SetPrivateDataInterface)(const GUID& guid, const IUnknown* pData) override;

    STDMETHOD(IASetInputLayout)(ID3D11InputLayout* pInputLayout) override;
    STDMETHOD(IASetVertexBuffers)(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppVertexBuffers, const UINT* pStrides, const UINT* pOffsets) override;
    STDMETHOD(IASetIndexBuffer)(ID3D11Buffer* pIndexBuffer, DXGI_FORMAT Format, UINT Offset) override;
    STDMETHOD(IASetPrimitiveTopology)(UINT Topology) override;

    STDMETHOD(VSSetShader)(ID3D11VertexShader* pVertexShader, ID3D11ClassInstance* const*, UINT) override;
    STDMETHOD(VSSetConstantBuffers)(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) override;
    STDMETHOD(VSSetShaderResources)(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) override;
    STDMETHOD(VSSetSamplers)(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override;

    STDMETHOD(PSSetShader)(ID3D11PixelShader* pPixelShader, ID3D11ClassInstance* const*, UINT) override;
    STDMETHOD(PSSetConstantBuffers)(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) override;
    STDMETHOD(PSSetShaderResources)(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) override;
    STDMETHOD(PSSetSamplers)(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override;

    STDMETHOD(GSSetShader)(ID3D11GeometryShader* pShader, ID3D11ClassInstance* const*, UINT) override;

    STDMETHOD(CSSetShader)(ID3D11ComputeShader* pComputeShader, ID3D11ClassInstance* const*, UINT) override;
    STDMETHOD(CSSetConstantBuffers)(UINT, UINT, ID3D11Buffer* const*) override;
    STDMETHOD(CSSetShaderResources)(UINT, UINT, ID3D11ShaderResourceView* const*) override;
    STDMETHOD(CSSetUnorderedAccessViews)(UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*) override;
    STDMETHOD(CSSetSamplers)(UINT, UINT, ID3D11SamplerState* const*) override;
    STDMETHOD(Dispatch)(UINT, UINT, UINT) override;

    STDMETHOD(OMSetRenderTargets)(UINT NumViews, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView) override;
    STDMETHOD(OMSetRenderTargetsAndUnorderedAccessViews)(UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*) override;
    STDMETHOD(OMSetBlendState)(ID3D11BlendState* pBlendState, const FLOAT BlendFactor[4], UINT SampleMask) override;
    STDMETHOD(OMSetDepthStencilState)(ID3D11DepthStencilState* pDepthStencilState, UINT StencilRef) override;

    STDMETHOD(RSSetState)(ID3D11RasterizerState* pRasterizerState) override;
    STDMETHOD(RSSetViewports)(UINT NumViewports, const D3D11_VIEWPORT* pViewports) override;
    STDMETHOD(RSSetScissorRects)(UINT NumRects, const RECT* pRects) override;

    STDMETHOD(ClearRenderTargetView)(ID3D11RenderTargetView* pRenderTargetView, const FLOAT ColorRGBA[4]) override;
    STDMETHOD(ClearDepthStencilView)(ID3D11DepthStencilView* pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil) override;

    STDMETHOD(DrawIndexed)(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) override;
    STDMETHOD(Draw)(UINT VertexCount, UINT StartVertexLocation) override;
    STDMETHOD(DrawIndexedInstanced)(UINT, UINT, UINT, INT, UINT) override;
    STDMETHOD(DrawInstanced)(UINT, UINT, UINT, UINT) override;

    STDMETHOD(Map)(ID3D11Resource* pResource, UINT Subresource, UINT MapType, UINT MapFlags, void* pMappedResource) override;
    STDMETHOD(Unmap)(ID3D11Resource* pResource, UINT Subresource) override;

    STDMETHOD(GenerateMips)(ID3D11ShaderResourceView* pShaderResourceView) override;
    STDMETHOD(CopyResource)(ID3D11Resource* pDstResource, ID3D11Resource* pSrcResource) override;
    STDMETHOD(UpdateSubresource)(ID3D11Resource* pDstResource, UINT DstSubresource, const void* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch) override;

private:
    void ensurePipeline();
    void commitDraw(UINT vertexCount, UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation, bool indexed);

    ULONG m_refCount = 1;
    D3D11Device& m_device;

    ID3D11VertexShader* m_vertexShader = nullptr;
    ID3D11PixelShader* m_pixelShader = nullptr;
    ID3D11InputLayout* m_inputLayout = nullptr;
    ID3D11BlendState* m_blendState = nullptr;
    ID3D11DepthStencilState* m_depthStencilState = nullptr;
    ID3D11RasterizerState* m_rasterizerState = nullptr;
    ID3D11DepthStencilView* m_depthStencilView = nullptr;
    UINT m_stencilRef = 0;
    UINT m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    static constexpr UINT MAX_VERTEX_BUFFERS = 32;
    static constexpr UINT MAX_RENDER_TARGETS = 8;
    static constexpr UINT MAX_CONSTANT_BUFFERS = 14;

    struct VertexBufferBinding {
        ID3D11Buffer* buffer = nullptr;
        UINT stride = 0;
        UINT offset = 0;
    };

    std::array<VertexBufferBinding, MAX_VERTEX_BUFFERS> m_vertexBuffers{};
    std::array<ID3D11RenderTargetView*, MAX_RENDER_TARGETS> m_renderTargets{};
    std::array<ID3D11Buffer*, MAX_CONSTANT_BUFFERS> m_vsConstantBuffers{};
    std::array<ID3D11Buffer*, MAX_CONSTANT_BUFFERS> m_psConstantBuffers{};
    std::array<ID3D11ShaderResourceView*, 128> m_vsShaderResources{};
    std::array<ID3D11ShaderResourceView*, 128> m_psShaderResources{};
    std::array<ID3D11SamplerState*, 16> m_vsSamplers{};
    std::array<ID3D11SamplerState*, 16> m_psSamplers{};

    ID3D11Buffer* m_indexBuffer = nullptr;
    DXGI_FORMAT m_indexBufferFormat = DXGI_FORMAT_UNKNOWN;
    UINT m_indexBufferOffset = 0;

    std::unique_ptr<PipelineState> m_cachedPipeline;
    D3D11_VIEWPORT m_viewport = {};
    FLOAT m_blendFactor[4] = {1,1,1,1};
    UINT m_sampleMask = 0xFFFFFFFF;
};

}
