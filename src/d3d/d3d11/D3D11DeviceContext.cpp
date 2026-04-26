#include <metalsharp/D3D11DeviceContext.h>
#include <metalsharp/D3D11Device.h>
#include <cstring>

namespace metalsharp {

D3D11DeviceContext::D3D11DeviceContext(D3D11Device& device)
    : m_device(device) {
    m_framebuffer = std::make_unique<MetalFramebuffer>();
}

HRESULT D3D11DeviceContext::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceContext)) {
        AddRef(); *ppvObject = this; return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG D3D11DeviceContext::AddRef() { return ++m_refCount; }
ULONG D3D11DeviceContext::Release() { ULONG c = --m_refCount; if (c == 0) delete this; return c; }

HRESULT D3D11DeviceContext::GetDevice(ID3D11Device** ppDevice) {
    if (!ppDevice) return E_POINTER;
    m_device.AddRef();
    *ppDevice = &m_device;
    return S_OK;
}

HRESULT D3D11DeviceContext::GetPrivateData(const GUID&, UINT*, void*) { return E_NOTIMPL; }
HRESULT D3D11DeviceContext::SetPrivateData(const GUID&, UINT, const void*) { return E_NOTIMPL; }
HRESULT D3D11DeviceContext::SetPrivateDataInterface(const GUID&, const IUnknown*) { return E_NOTIMPL; }

HRESULT D3D11DeviceContext::IASetInputLayout(ID3D11InputLayout* pInputLayout) {
    m_inputLayout = pInputLayout;
    return S_OK;
}

HRESULT D3D11DeviceContext::IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppVertexBuffers, const UINT* pStrides, const UINT* pOffsets) {
    for (UINT i = 0; i < NumBuffers && (StartSlot + i) < MAX_VERTEX_BUFFERS; ++i) {
        m_vertexBuffers[StartSlot + i].buffer = ppVertexBuffers ? ppVertexBuffers[i] : nullptr;
        m_vertexBuffers[StartSlot + i].stride = pStrides ? pStrides[i] : 0;
        m_vertexBuffers[StartSlot + i].offset = pOffsets ? pOffsets[i] : 0;
    }
    return S_OK;
}

HRESULT D3D11DeviceContext::IASetIndexBuffer(ID3D11Buffer* pIndexBuffer, DXGI_FORMAT Format, UINT Offset) {
    m_indexBuffer = pIndexBuffer;
    m_indexBufferFormat = Format;
    m_indexBufferOffset = Offset;
    return S_OK;
}

HRESULT D3D11DeviceContext::IASetPrimitiveTopology(UINT Topology) {
    m_primitiveTopology = Topology;
    return S_OK;
}

HRESULT D3D11DeviceContext::VSSetShader(ID3D11VertexShader* pVertexShader, ID3D11ClassInstance* const*, UINT) {
    m_vertexShader = pVertexShader;
    return S_OK;
}

HRESULT D3D11DeviceContext::VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) {
    for (UINT i = 0; i < NumBuffers && (StartSlot + i) < MAX_CONSTANT_BUFFERS; ++i) {
        m_vsConstantBuffers[StartSlot + i] = ppConstantBuffers ? ppConstantBuffers[i] : nullptr;
    }
    return S_OK;
}

HRESULT D3D11DeviceContext::VSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs) {
    for (UINT i = 0; i < NumViews && (StartSlot + i) < 128; ++i) {
        m_vsShaderResources[StartSlot + i] = ppSRVs ? ppSRVs[i] : nullptr;
    }
    return S_OK;
}

HRESULT D3D11DeviceContext::VSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) {
    for (UINT i = 0; i < NumSamplers && (StartSlot + i) < 16; ++i) {
        m_vsSamplers[StartSlot + i] = ppSamplers ? ppSamplers[i] : nullptr;
    }
    return S_OK;
}

HRESULT D3D11DeviceContext::PSSetShader(ID3D11PixelShader* pPixelShader, ID3D11ClassInstance* const*, UINT) {
    m_pixelShader = pPixelShader;
    return S_OK;
}

HRESULT D3D11DeviceContext::PSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) {
    for (UINT i = 0; i < NumBuffers && (StartSlot + i) < MAX_CONSTANT_BUFFERS; ++i) {
        m_psConstantBuffers[StartSlot + i] = ppConstantBuffers ? ppConstantBuffers[i] : nullptr;
    }
    return S_OK;
}

HRESULT D3D11DeviceContext::PSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs) {
    for (UINT i = 0; i < NumViews && (StartSlot + i) < 128; ++i) {
        m_psShaderResources[StartSlot + i] = ppSRVs ? ppSRVs[i] : nullptr;
    }
    return S_OK;
}

HRESULT D3D11DeviceContext::PSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) {
    for (UINT i = 0; i < NumSamplers && (StartSlot + i) < 16; ++i) {
        m_psSamplers[StartSlot + i] = ppSamplers ? ppSamplers[i] : nullptr;
    }
    return S_OK;
}

HRESULT D3D11DeviceContext::GSSetShader(ID3D11GeometryShader*, ID3D11ClassInstance* const*, UINT) { return E_NOTIMPL; }

HRESULT D3D11DeviceContext::CSSetShader(ID3D11ComputeShader*, ID3D11ClassInstance* const*, UINT) { return E_NOTIMPL; }
HRESULT D3D11DeviceContext::CSSetConstantBuffers(UINT, UINT, ID3D11Buffer* const*) { return E_NOTIMPL; }
HRESULT D3D11DeviceContext::CSSetShaderResources(UINT, UINT, ID3D11ShaderResourceView* const*) { return E_NOTIMPL; }
HRESULT D3D11DeviceContext::CSSetUnorderedAccessViews(UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*) { return E_NOTIMPL; }
HRESULT D3D11DeviceContext::CSSetSamplers(UINT, UINT, ID3D11SamplerState* const*) { return E_NOTIMPL; }
HRESULT D3D11DeviceContext::Dispatch(UINT, UINT, UINT) { return E_NOTIMPL; }

HRESULT D3D11DeviceContext::OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView) {
    m_renderTargets.fill(nullptr);
    for (UINT i = 0; i < NumViews && i < MAX_RENDER_TARGETS; ++i) {
        m_renderTargets[i] = ppRenderTargetViews ? ppRenderTargetViews[i] : nullptr;
    }
    m_depthStencilView = pDepthStencilView;
    return S_OK;
}

HRESULT D3D11DeviceContext::OMSetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView* const* ppRTVs, ID3D11DepthStencilView* pDSV, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*) {
    return OMSetRenderTargets(NumRTVs, ppRTVs, pDSV);
}

HRESULT D3D11DeviceContext::OMSetBlendState(ID3D11BlendState* pBlendState, const FLOAT[4], UINT) {
    m_blendState = pBlendState;
    return S_OK;
}

HRESULT D3D11DeviceContext::OMSetDepthStencilState(ID3D11DepthStencilState* pDSState, UINT StencilRef) {
    m_depthStencilState = pDSState;
    m_stencilRef = StencilRef;
    return S_OK;
}

HRESULT D3D11DeviceContext::RSSetState(ID3D11RasterizerState* pRasterizerState) {
    m_rasterizerState = pRasterizerState;
    return S_OK;
}

HRESULT D3D11DeviceContext::RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT* pViewports) {
    if (NumViewports > 0 && pViewports) m_viewport = pViewports[0];
    return S_OK;
}

HRESULT D3D11DeviceContext::RSSetScissorRects(UINT, const RECT*) { return S_OK; }

HRESULT D3D11DeviceContext::ClearRenderTargetView(ID3D11RenderTargetView*, const FLOAT[4]) {
    return E_NOTIMPL;
}

HRESULT D3D11DeviceContext::ClearDepthStencilView(ID3D11DepthStencilView*, UINT, FLOAT, UINT8) {
    return E_NOTIMPL;
}

void D3D11DeviceContext::flushRenderState() {
}

void D3D11DeviceContext::commitDraw() {
}

HRESULT D3D11DeviceContext::DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) {
    flushRenderState();
    commitDraw();
    return S_OK;
}

HRESULT D3D11DeviceContext::Draw(UINT VertexCount, UINT StartVertexLocation) {
    flushRenderState();
    commitDraw();
    return S_OK;
}

HRESULT D3D11DeviceContext::DrawIndexedInstanced(UINT, UINT, UINT, INT, UINT) { return E_NOTIMPL; }
HRESULT D3D11DeviceContext::DrawInstanced(UINT, UINT, UINT, UINT) { return E_NOTIMPL; }

HRESULT D3D11DeviceContext::Map(ID3D11Resource*, UINT, UINT, UINT, void*) { return E_NOTIMPL; }
HRESULT D3D11DeviceContext::Unmap(ID3D11Resource*, UINT) { return E_NOTIMPL; }

HRESULT D3D11DeviceContext::GenerateMips(ID3D11ShaderResourceView*) { return E_NOTIMPL; }
HRESULT D3D11DeviceContext::CopyResource(ID3D11Resource*, ID3D11Resource*) { return E_NOTIMPL; }
HRESULT D3D11DeviceContext::UpdateSubresource(ID3D11Resource*, UINT, const void*, const void*, UINT, UINT) { return E_NOTIMPL; }

}
