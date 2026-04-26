#include <metalsharp/DeferredContext.h>
#include <metalsharp/D3D11Device.h>
#include <cstring>

namespace metalsharp {

HRESULT CommandList::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11CommandList)) {
        AddRef(); *ppvObject = this; return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG CommandList::AddRef() { return ++m_refCount; }
ULONG CommandList::Release() { ULONG c = --m_refCount; if (c == 0) delete this; return c; }

HRESULT CommandList::GetDevice(ID3D11Device** ppDevice) {
    if (!ppDevice) return E_POINTER;
    return E_NOTIMPL;
}

HRESULT CommandList::GetPrivateData(const GUID&, UINT*, void*) { return E_NOTIMPL; }
HRESULT CommandList::SetPrivateData(const GUID&, UINT, const void*) { return E_NOTIMPL; }
HRESULT CommandList::SetPrivateDataInterface(const GUID&, const IUnknown*) { return E_NOTIMPL; }
UINT CommandList::GetContextFlags() { return 0; }

DeferredContext::DeferredContext(D3D11Device& device) : m_device(device) {}

HRESULT DeferredContext::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceContext)) {
        AddRef(); *ppvObject = this; return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG DeferredContext::AddRef() { return ++m_refCount; }
ULONG DeferredContext::Release() { ULONG c = --m_refCount; if (c == 0) delete this; return c; }

HRESULT DeferredContext::GetDevice(ID3D11Device** ppDevice) {
    if (!ppDevice) return E_POINTER;
    m_device.AddRef();
    *ppDevice = &m_device;
    return S_OK;
}

HRESULT DeferredContext::GetPrivateData(const GUID&, UINT*, void*) { return E_NOTIMPL; }
HRESULT DeferredContext::SetPrivateData(const GUID&, UINT, const void*) { return E_NOTIMPL; }
HRESULT DeferredContext::SetPrivateDataInterface(const GUID&, const IUnknown*) { return E_NOTIMPL; }

HRESULT DeferredContext::IASetInputLayout(ID3D11InputLayout* p) {
    if (p) p->AddRef();
    record([p](ID3D11DeviceContext* ctx) {
        ctx->IASetInputLayout(p);
        if (p) p->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppVBs, const UINT* pStrides, const UINT* pOffsets) {
    auto bufs = std::make_shared<std::vector<ID3D11Buffer*>>(ppVBs ? ppVBs : nullptr, ppVBs ? ppVBs + NumBuffers : nullptr);
    auto strides = std::make_shared<std::vector<UINT>>(pStrides ? pStrides : nullptr, pStrides ? pStrides + NumBuffers : nullptr);
    auto offsets = std::make_shared<std::vector<UINT>>(pOffsets ? pOffsets : nullptr, pOffsets ? pOffsets + NumBuffers : nullptr);
    for (auto& b : *bufs) if (b) b->AddRef();
    record([StartSlot, NumBuffers, bufs, strides, offsets](ID3D11DeviceContext* ctx) {
        ctx->IASetVertexBuffers(StartSlot, NumBuffers, bufs->data(), strides->data(), offsets->data());
        for (auto& b : *bufs) if (b) b->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::IASetIndexBuffer(ID3D11Buffer* p, DXGI_FORMAT fmt, UINT off) {
    if (p) p->AddRef();
    record([p, fmt, off](ID3D11DeviceContext* ctx) {
        ctx->IASetIndexBuffer(p, fmt, off);
        if (p) p->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::IASetPrimitiveTopology(UINT topo) {
    record([topo](ID3D11DeviceContext* ctx) {
        ctx->IASetPrimitiveTopology(topo);
    });
    return S_OK;
}

HRESULT DeferredContext::VSSetShader(ID3D11VertexShader* p, ID3D11ClassInstance* const*, UINT) {
    if (p) p->AddRef();
    record([p](ID3D11DeviceContext* ctx) {
        ctx->VSSetShader(p, nullptr, 0);
        if (p) p->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::VSSetConstantBuffers(UINT slot, UINT num, ID3D11Buffer* const* pp) {
    auto bufs = std::make_shared<std::vector<ID3D11Buffer*>>(pp ? pp : nullptr, pp ? pp + num : nullptr);
    for (auto& b : *bufs) if (b) b->AddRef();
    record([slot, num, bufs](ID3D11DeviceContext* ctx) {
        ctx->VSSetConstantBuffers(slot, num, bufs->data());
        for (auto& b : *bufs) if (b) b->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::VSSetShaderResources(UINT slot, UINT num, ID3D11ShaderResourceView* const* pp) {
    auto views = std::make_shared<std::vector<ID3D11ShaderResourceView*>>(pp ? pp : nullptr, pp ? pp + num : nullptr);
    for (auto& v : *views) if (v) v->AddRef();
    record([slot, num, views](ID3D11DeviceContext* ctx) {
        ctx->VSSetShaderResources(slot, num, views->data());
        for (auto& v : *views) if (v) v->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::VSSetSamplers(UINT slot, UINT num, ID3D11SamplerState* const* pp) {
    auto samps = std::make_shared<std::vector<ID3D11SamplerState*>>(pp ? pp : nullptr, pp ? pp + num : nullptr);
    for (auto& s : *samps) if (s) s->AddRef();
    record([slot, num, samps](ID3D11DeviceContext* ctx) {
        ctx->VSSetSamplers(slot, num, samps->data());
        for (auto& s : *samps) if (s) s->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::PSSetShader(ID3D11PixelShader* p, ID3D11ClassInstance* const*, UINT) {
    if (p) p->AddRef();
    record([p](ID3D11DeviceContext* ctx) {
        ctx->PSSetShader(p, nullptr, 0);
        if (p) p->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::PSSetConstantBuffers(UINT slot, UINT num, ID3D11Buffer* const* pp) {
    auto bufs = std::make_shared<std::vector<ID3D11Buffer*>>(pp ? pp : nullptr, pp ? pp + num : nullptr);
    for (auto& b : *bufs) if (b) b->AddRef();
    record([slot, num, bufs](ID3D11DeviceContext* ctx) {
        ctx->PSSetConstantBuffers(slot, num, bufs->data());
        for (auto& b : *bufs) if (b) b->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::PSSetShaderResources(UINT slot, UINT num, ID3D11ShaderResourceView* const* pp) {
    auto views = std::make_shared<std::vector<ID3D11ShaderResourceView*>>(pp ? pp : nullptr, pp ? pp + num : nullptr);
    for (auto& v : *views) if (v) v->AddRef();
    record([slot, num, views](ID3D11DeviceContext* ctx) {
        ctx->PSSetShaderResources(slot, num, views->data());
        for (auto& v : *views) if (v) v->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::PSSetSamplers(UINT slot, UINT num, ID3D11SamplerState* const* pp) {
    auto samps = std::make_shared<std::vector<ID3D11SamplerState*>>(pp ? pp : nullptr, pp ? pp + num : nullptr);
    for (auto& s : *samps) if (s) s->AddRef();
    record([slot, num, samps](ID3D11DeviceContext* ctx) {
        ctx->PSSetSamplers(slot, num, samps->data());
        for (auto& s : *samps) if (s) s->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::GSSetShader(ID3D11GeometryShader*, ID3D11ClassInstance* const*, UINT) { return E_NOTIMPL; }

HRESULT DeferredContext::CSSetShader(ID3D11ComputeShader* p, ID3D11ClassInstance* const*, UINT) {
    if (p) p->AddRef();
    record([p](ID3D11DeviceContext* ctx) {
        ctx->CSSetShader(p, nullptr, 0);
        if (p) p->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::CSSetConstantBuffers(UINT slot, UINT num, ID3D11Buffer* const* pp) {
    auto bufs = std::make_shared<std::vector<ID3D11Buffer*>>(pp ? pp : nullptr, pp ? pp + num : nullptr);
    for (auto& b : *bufs) if (b) b->AddRef();
    record([slot, num, bufs](ID3D11DeviceContext* ctx) {
        ctx->CSSetConstantBuffers(slot, num, bufs->data());
        for (auto& b : *bufs) if (b) b->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::CSSetShaderResources(UINT slot, UINT num, ID3D11ShaderResourceView* const* pp) {
    auto views = std::make_shared<std::vector<ID3D11ShaderResourceView*>>(pp ? pp : nullptr, pp ? pp + num : nullptr);
    for (auto& v : *views) if (v) v->AddRef();
    record([slot, num, views](ID3D11DeviceContext* ctx) {
        ctx->CSSetShaderResources(slot, num, views->data());
        for (auto& v : *views) if (v) v->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::CSSetUnorderedAccessViews(UINT slot, UINT num, ID3D11UnorderedAccessView* const* pp, const UINT* pCounts) {
    auto uavs = std::make_shared<std::vector<ID3D11UnorderedAccessView*>>(pp ? pp : nullptr, pp ? pp + num : nullptr);
    for (auto& u : *uavs) if (u) u->AddRef();
    record([slot, num, uavs](ID3D11DeviceContext* ctx) {
        ctx->CSSetUnorderedAccessViews(slot, num, uavs->data(), nullptr);
        for (auto& u : *uavs) if (u) u->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::CSSetSamplers(UINT slot, UINT num, ID3D11SamplerState* const* pp) {
    auto samps = std::make_shared<std::vector<ID3D11SamplerState*>>(pp ? pp : nullptr, pp ? pp + num : nullptr);
    for (auto& s : *samps) if (s) s->AddRef();
    record([slot, num, samps](ID3D11DeviceContext* ctx) {
        ctx->CSSetSamplers(slot, num, samps->data());
        for (auto& s : *samps) if (s) s->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::Dispatch(UINT x, UINT y, UINT z) {
    record([x, y, z](ID3D11DeviceContext* ctx) {
        ctx->Dispatch(x, y, z);
    });
    return S_OK;
}

HRESULT DeferredContext::OMSetRenderTargets(UINT num, ID3D11RenderTargetView* const* ppRTVs, ID3D11DepthStencilView* pDSV) {
    auto rtvs = std::make_shared<std::vector<ID3D11RenderTargetView*>>(ppRTVs ? ppRTVs : nullptr, ppRTVs ? ppRTVs + num : nullptr);
    for (auto& r : *rtvs) if (r) r->AddRef();
    if (pDSV) pDSV->AddRef();
    record([num, rtvs, pDSV](ID3D11DeviceContext* ctx) {
        ctx->OMSetRenderTargets(num, rtvs->data(), pDSV);
        for (auto& r : *rtvs) if (r) r->Release();
        if (pDSV) pDSV->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::OMSetRenderTargetsAndUnorderedAccessViews(UINT numRTV, ID3D11RenderTargetView* const* ppRTVs, ID3D11DepthStencilView* pDSV, UINT uavStart, UINT numUAV, ID3D11UnorderedAccessView* const* ppUAVs, const UINT* pCounts) {
    return OMSetRenderTargets(numRTV, ppRTVs, pDSV);
}

HRESULT DeferredContext::OMSetBlendState(ID3D11BlendState* p, const FLOAT factor[4], UINT mask) {
    if (p) p->AddRef();
    FLOAT f[4] = {};
    if (factor) memcpy(f, factor, sizeof(f));
    record([p, f, mask](ID3D11DeviceContext* ctx) {
        ctx->OMSetBlendState(p, f, mask);
        if (p) p->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::OMSetDepthStencilState(ID3D11DepthStencilState* p, UINT ref) {
    if (p) p->AddRef();
    record([p, ref](ID3D11DeviceContext* ctx) {
        ctx->OMSetDepthStencilState(p, ref);
        if (p) p->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::RSSetState(ID3D11RasterizerState* p) {
    if (p) p->AddRef();
    record([p](ID3D11DeviceContext* ctx) {
        ctx->RSSetState(p);
        if (p) p->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::RSSetViewports(UINT num, const D3D11_VIEWPORT* vp) {
    D3D11_VIEWPORT copy = {};
    if (num > 0 && vp) copy = vp[0];
    record([copy](ID3D11DeviceContext* ctx) {
        ctx->RSSetViewports(1, &copy);
    });
    return S_OK;
}

HRESULT DeferredContext::RSSetScissorRects(UINT num, const RECT* rects) { return S_OK; }

HRESULT DeferredContext::ClearRenderTargetView(ID3D11RenderTargetView* p, const FLOAT color[4]) {
    if (p) p->AddRef();
    FLOAT c[4] = {};
    if (color) memcpy(c, color, sizeof(c));
    record([p, c](ID3D11DeviceContext* ctx) {
        ctx->ClearRenderTargetView(p, c);
        if (p) p->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::ClearDepthStencilView(ID3D11DepthStencilView* p, UINT flags, FLOAT depth, UINT8 stencil) {
    if (p) p->AddRef();
    record([p, flags, depth, stencil](ID3D11DeviceContext* ctx) {
        ctx->ClearDepthStencilView(p, flags, depth, stencil);
        if (p) p->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::DrawIndexed(UINT count, UINT start, INT base) {
    record([count, start, base](ID3D11DeviceContext* ctx) {
        ctx->DrawIndexed(count, start, base);
    });
    return S_OK;
}

HRESULT DeferredContext::Draw(UINT count, UINT start) {
    record([count, start](ID3D11DeviceContext* ctx) {
        ctx->Draw(count, start);
    });
    return S_OK;
}

HRESULT DeferredContext::DrawIndexedInstanced(UINT idxPerInst, UINT inst, UINT start, INT base, UINT startInst) {
    record([idxPerInst, inst, start, base, startInst](ID3D11DeviceContext* ctx) {
        ctx->DrawIndexedInstanced(idxPerInst, inst, start, base, startInst);
    });
    return S_OK;
}

HRESULT DeferredContext::DrawInstanced(UINT vertPerInst, UINT inst, UINT start, UINT startInst) {
    record([vertPerInst, inst, start, startInst](ID3D11DeviceContext* ctx) {
        ctx->DrawInstanced(vertPerInst, inst, start, startInst);
    });
    return S_OK;
}

HRESULT DeferredContext::Map(ID3D11Resource*, UINT, UINT, UINT, void*) {
    return E_FAIL;
}

HRESULT DeferredContext::Unmap(ID3D11Resource*, UINT) {
    return S_OK;
}

HRESULT DeferredContext::GenerateMips(ID3D11ShaderResourceView* p) {
    if (p) p->AddRef();
    record([p](ID3D11DeviceContext* ctx) {
        ctx->GenerateMips(p);
        if (p) p->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::CopyResource(ID3D11Resource* dst, ID3D11Resource* src) {
    if (dst) dst->AddRef();
    if (src) src->AddRef();
    record([dst, src](ID3D11DeviceContext* ctx) {
        ctx->CopyResource(dst, src);
        if (dst) dst->Release();
        if (src) src->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::UpdateSubresource(ID3D11Resource* dst, UINT sub, const void* box, const void* data, UINT rowPitch, UINT depthPitch) {
    return E_FAIL;
}

HRESULT DeferredContext::CopySubresourceRegion(ID3D11Resource* dst, UINT dstSub, UINT dx, UINT dy, UINT dz, ID3D11Resource* src, UINT srcSub, const void* srcBox) {
    if (dst) dst->AddRef();
    if (src) src->AddRef();
    record([dst, dstSub, dx, dy, dz, src, srcSub](ID3D11DeviceContext* ctx) {
        ctx->CopySubresourceRegion(dst, dstSub, dx, dy, dz, src, srcSub, nullptr);
        if (dst) dst->Release();
        if (src) src->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::ResolveSubresource(ID3D11Resource* dst, UINT dstSub, ID3D11Resource* src, UINT srcSub, DXGI_FORMAT fmt) {
    if (dst) dst->AddRef();
    if (src) src->AddRef();
    record([dst, dstSub, src, srcSub, fmt](ID3D11DeviceContext* ctx) {
        ctx->ResolveSubresource(dst, dstSub, src, srcSub, fmt);
        if (dst) dst->Release();
        if (src) src->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::Begin(ID3D11Query* p) {
    if (p) p->AddRef();
    record([p](ID3D11DeviceContext* ctx) {
        ctx->Begin(p);
        if (p) p->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::End(ID3D11Query* p) {
    if (p) p->AddRef();
    record([p](ID3D11DeviceContext* ctx) {
        ctx->End(p);
        if (p) p->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::GetData(ID3D11Query*, void*, UINT, UINT) {
    return S_FALSE;
}

HRESULT DeferredContext::SetPredication(ID3D11Predicate* p, INT val) {
    if (p) p->AddRef();
    record([p, val](ID3D11DeviceContext* ctx) {
        ctx->SetPredication(p, val);
        if (p) p->Release();
    });
    return S_OK;
}

HRESULT DeferredContext::FinishCommandList(INT, ID3D11CommandList** ppCmdList) {
    if (!ppCmdList) return E_POINTER;
    *ppCmdList = m_commandList;
    m_commandList = new CommandList();
    return S_OK;
}

HRESULT DeferredContext::ExecuteCommandList(ID3D11CommandList*, INT) {
    return E_FAIL;
}

}
