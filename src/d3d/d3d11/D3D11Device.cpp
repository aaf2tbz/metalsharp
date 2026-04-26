#include <metalsharp/D3D11Device.h>
#include <metalsharp/D3D11DeviceContext.h>
#include <metalsharp/FormatTranslation.h>
#include <metalsharp/PipelineState.h>
#include <cstring>
#include <vector>

namespace metalsharp {

#define MS_COM_BODY() \
    ULONG refCount = 1; \
    HRESULT QueryInterface(REFIID riid, void** ppv) override { \
        if (!ppv) return E_POINTER; \
        AddRef(); *ppv = this; return S_OK; \
    } \
    ULONG AddRef() override { return ++refCount; } \
    ULONG Release() override { ULONG c = --refCount; if (c == 0) delete this; return c; } \
    STDMETHOD(GetDevice)(ID3D11Device**) override { return E_NOTIMPL; } \
    STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override { return E_NOTIMPL; } \
    STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override { return E_NOTIMPL; } \
    STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override { return E_NOTIMPL; }

HRESULT D3D11Device::create(D3D11Device** ppDevice) {
    if (!ppDevice) return E_POINTER;
    auto* device = new D3D11Device();
    device->m_metalDevice = std::unique_ptr<MetalDevice>(MetalDevice::create());
    if (!device->m_metalDevice) {
        delete device;
        return E_FAIL;
    }
    device->m_shaderTranslator = std::make_unique<ShaderTranslator>();
    device->m_immediateContext = std::make_unique<D3D11DeviceContext>(*device);
    *ppDevice = device;
    return S_OK;
}

D3D11Device::D3D11Device() = default;
D3D11Device::~D3D11Device() = default;

HRESULT D3D11Device::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11Device)) {
        AddRef();
        *ppvObject = this;
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG D3D11Device::AddRef() { return ++m_refCount; }

ULONG D3D11Device::Release() {
    ULONG count = --m_refCount;
    if (count == 0) delete this;
    return count;
}

HRESULT D3D11Device::CreateBuffer(const D3D11_BUFFER_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Buffer** ppBuffer) {
    if (!pDesc || !ppBuffer) return E_INVALIDARG;
    size_t size = pDesc->ByteWidth;
    MetalBuffer* metalBuf = MetalBuffer::create(*m_metalDevice, size, pInitialData ? pInitialData->pSysMem : nullptr);
    if (!metalBuf) return E_OUTOFMEMORY;

    struct Impl : public ID3D11Buffer {
        MS_COM_BODY()
        STDMETHOD(GetType)(UINT* p) override { if(p) *p = D3D11_RESOURCE_DIMENSION_BUFFER; return S_OK; }
        STDMETHOD(SetEvictionPriority)(UINT) override { return S_OK; }
        STDMETHOD_(UINT, GetEvictionPriority)() override { return 0; }
        std::unique_ptr<MetalBuffer> metalBuffer;
        D3D11_BUFFER_DESC desc;
        void* __metalBufferPtr() const override { return metalBuffer ? metalBuffer->nativeBuffer() : nullptr; }
        UINT __getResourceDimension() const override { return D3D11_RESOURCE_DIMENSION_BUFFER; }
        UINT __getCPUAccessFlags() const override { return desc.CPUAccessFlags; }
        UINT __getUsage() const override { return desc.Usage; }
    };

    auto* buf = new Impl();
    buf->metalBuffer = std::unique_ptr<MetalBuffer>(metalBuf);
    buf->desc = *pDesc;
    *ppBuffer = buf;
    return S_OK;
}

HRESULT D3D11Device::CreateTexture2D(const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D) {
    if (!pDesc || !ppTexture2D) return E_INVALIDARG;

    uint32_t metalFmt = dxgiFormatToMetal((DXGITranslation)pDesc->Format);
    uint32_t usage = 0;
    if (pDesc->BindFlags & D3D11_BIND_RENDER_TARGET) usage |= 0x1;
    if (pDesc->BindFlags & D3D11_BIND_DEPTH_STENCIL) usage |= 0x1;
    if (pDesc->BindFlags & D3D11_BIND_SHADER_RESOURCE) usage |= 0x2;
    if (pDesc->BindFlags & D3D11_BIND_UNORDERED_ACCESS) usage |= 0x4;
    if (pDesc->BindFlags & D3D11_BIND_SHADER_RESOURCE) usage |= 0x8;

    uint32_t mipLevels = pDesc->MipLevels > 0 ? pDesc->MipLevels : 1;
    uint32_t sampleCount = pDesc->SampleDesc.Count > 0 ? pDesc->SampleDesc.Count : 1;

    MetalTexture* tex = MetalTexture::create2D(*m_metalDevice, pDesc->Width, pDesc->Height, metalFmt, usage, mipLevels, sampleCount);
    if (!tex) return E_OUTOFMEMORY;

    if (pInitialData) {
        tex->uploadData(0, 0, pInitialData->pSysMem, pInitialData->SysMemPitch, pDesc->Width, pDesc->Height);
    }

    struct Impl : public ID3D11Texture2D {
        MS_COM_BODY()
        STDMETHOD(GetType)(UINT* p) override { if(p) *p = D3D11_RESOURCE_DIMENSION_TEXTURE2D; return S_OK; }
        STDMETHOD(SetEvictionPriority)(UINT) override { return S_OK; }
        STDMETHOD_(UINT, GetEvictionPriority)() override { return 0; }
        std::unique_ptr<MetalTexture> metalTexture;
        D3D11_TEXTURE2D_DESC desc;
        void GetDesc(D3D11_TEXTURE2D_DESC* p) override { if(p) *p = desc; }
        void* __metalTexturePtr() const override { return metalTexture ? metalTexture->nativeTexture() : nullptr; }
        UINT __getResourceDimension() const override { return D3D11_RESOURCE_DIMENSION_TEXTURE2D; }
        UINT __getCPUAccessFlags() const override { return desc.CPUAccessFlags; }
        UINT __getUsage() const override { return desc.Usage; }
    };

    auto* impl = new Impl();
    impl->metalTexture = std::unique_ptr<MetalTexture>(tex);
    impl->desc = *pDesc;
    *ppTexture2D = impl;
    return S_OK;
}

HRESULT D3D11Device::CreateTexture1D(const D3D11_TEXTURE1D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture1D** ppTexture1D) {
    if (!pDesc || !ppTexture1D) return E_INVALIDARG;

    uint32_t metalFmt = dxgiFormatToMetal((DXGITranslation)pDesc->Format);
    uint32_t usage = 0;
    if (pDesc->BindFlags & D3D11_BIND_RENDER_TARGET) usage |= 0x1;
    if (pDesc->BindFlags & D3D11_BIND_DEPTH_STENCIL) usage |= 0x1;
    if (pDesc->BindFlags & D3D11_BIND_SHADER_RESOURCE) usage |= 0x2;
    if (pDesc->BindFlags & D3D11_BIND_UNORDERED_ACCESS) usage |= 0x4;
    if (pDesc->BindFlags & D3D11_BIND_SHADER_RESOURCE) usage |= 0x8;

    uint32_t mipLevels = pDesc->MipLevels > 0 ? pDesc->MipLevels : 1;

    MetalTexture* tex = MetalTexture::create1D(*m_metalDevice, pDesc->Width, metalFmt, usage, mipLevels);
    if (!tex) return E_OUTOFMEMORY;

    if (pInitialData) {
        tex->uploadData(0, 0, pInitialData->pSysMem, pInitialData->SysMemPitch, pDesc->Width, 1);
    }

    struct Impl : public ID3D11Texture1D {
        MS_COM_BODY()
        STDMETHOD(GetType)(UINT* p) override { if(p) *p = D3D11_RESOURCE_DIMENSION_TEXTURE1D; return S_OK; }
        STDMETHOD(SetEvictionPriority)(UINT) override { return S_OK; }
        STDMETHOD_(UINT, GetEvictionPriority)() override { return 0; }
        std::unique_ptr<MetalTexture> metalTexture;
        D3D11_TEXTURE1D_DESC desc;
        void GetDesc(D3D11_TEXTURE1D_DESC* p) override { if(p) *p = desc; }
        void* __metalTexturePtr() const override { return metalTexture ? metalTexture->nativeTexture() : nullptr; }
        UINT __getResourceDimension() const override { return D3D11_RESOURCE_DIMENSION_TEXTURE1D; }
        UINT __getCPUAccessFlags() const override { return desc.CPUAccessFlags; }
        UINT __getUsage() const override { return desc.Usage; }
    };

    auto* impl = new Impl();
    impl->metalTexture = std::unique_ptr<MetalTexture>(tex);
    impl->desc = *pDesc;
    *ppTexture1D = impl;
    return S_OK;
}

HRESULT D3D11Device::CreateTexture3D(const D3D11_TEXTURE3D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture3D** ppTexture3D) {
    if (!pDesc || !ppTexture3D) return E_INVALIDARG;

    uint32_t metalFmt = dxgiFormatToMetal((DXGITranslation)pDesc->Format);
    uint32_t usage = 0;
    if (pDesc->BindFlags & D3D11_BIND_RENDER_TARGET) usage |= 0x1;
    if (pDesc->BindFlags & D3D11_BIND_DEPTH_STENCIL) usage |= 0x1;
    if (pDesc->BindFlags & D3D11_BIND_SHADER_RESOURCE) usage |= 0x2;
    if (pDesc->BindFlags & D3D11_BIND_UNORDERED_ACCESS) usage |= 0x4;
    if (pDesc->BindFlags & D3D11_BIND_SHADER_RESOURCE) usage |= 0x8;

    uint32_t mipLevels = pDesc->MipLevels > 0 ? pDesc->MipLevels : 1;

    MetalTexture* tex = MetalTexture::create3D(*m_metalDevice, pDesc->Width, pDesc->Height, pDesc->Depth, metalFmt, usage, mipLevels);
    if (!tex) return E_OUTOFMEMORY;

    if (pInitialData) {
        tex->uploadData(0, 0, pInitialData->pSysMem, pInitialData->SysMemPitch, pDesc->Width, pDesc->Height, pDesc->Depth);
    }

    struct Impl : public ID3D11Texture3D {
        MS_COM_BODY()
        STDMETHOD(GetType)(UINT* p) override { if(p) *p = D3D11_RESOURCE_DIMENSION_TEXTURE3D; return S_OK; }
        STDMETHOD(SetEvictionPriority)(UINT) override { return S_OK; }
        STDMETHOD_(UINT, GetEvictionPriority)() override { return 0; }
        std::unique_ptr<MetalTexture> metalTexture;
        D3D11_TEXTURE3D_DESC desc;
        void GetDesc(D3D11_TEXTURE3D_DESC* p) override { if(p) *p = desc; }
        void* __metalTexturePtr() const override { return metalTexture ? metalTexture->nativeTexture() : nullptr; }
        UINT __getResourceDimension() const override { return D3D11_RESOURCE_DIMENSION_TEXTURE3D; }
        UINT __getCPUAccessFlags() const override { return desc.CPUAccessFlags; }
        UINT __getUsage() const override { return desc.Usage; }
    };

    auto* impl = new Impl();
    impl->metalTexture = std::unique_ptr<MetalTexture>(tex);
    impl->desc = *pDesc;
    *ppTexture3D = impl;
    return S_OK;
}

HRESULT D3D11Device::CreateVertexShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage*, ID3D11VertexShader** ppVertexShader) {
    if (!pShaderBytecode || !ppVertexShader) return E_INVALIDARG;

    struct Impl : public ID3D11VertexShader {
        MS_COM_BODY()
        void* metalVertexFunction = nullptr;
        void* __metalVertexFunction() const override { return metalVertexFunction; }
    };

    CompiledShader compiled;
    if (!m_shaderTranslator->compileMSL(
            reinterpret_cast<const char*>(pShaderBytecode),
            "vertexShader", "fragmentShader", compiled)) {
        return E_FAIL;
    }

    auto* vs = new Impl();
    vs->metalVertexFunction = compiled.vertexFunction;
    *ppVertexShader = vs;
    return S_OK;
}

HRESULT D3D11Device::CreatePixelShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage*, ID3D11PixelShader** ppPixelShader) {
    if (!pShaderBytecode || !ppPixelShader) return E_INVALIDARG;

    struct Impl : public ID3D11PixelShader {
        MS_COM_BODY()
        void* metalFragmentFunction = nullptr;
        void* __metalFragmentFunction() const override { return metalFragmentFunction; }
    };

    CompiledShader compiled;
    if (!m_shaderTranslator->compileMSL(
            reinterpret_cast<const char*>(pShaderBytecode),
            "vertexShader", "fragmentShader", compiled)) {
        return E_FAIL;
    }

    auto* ps = new Impl();
    ps->metalFragmentFunction = compiled.fragmentFunction;
    *ppPixelShader = ps;
    return S_OK;
}

HRESULT D3D11Device::CreateGeometryShader(const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11GeometryShader**) { return E_NOTIMPL; }
HRESULT D3D11Device::CreateComputeShader(const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11ComputeShader**) { return E_NOTIMPL; }

HRESULT D3D11Device::CreateInputLayout(const void* pInputElementDescs, UINT NumElements, const void*, SIZE_T, ID3D11InputLayout** ppInputLayout) {
    if (!pInputElementDescs || !ppInputLayout || NumElements == 0) return E_INVALIDARG;

    struct Impl : public ID3D11InputLayout {
        MS_COM_BODY()
        std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
        UINT __getNumElements() const override { return (UINT)elements.size(); }
        const void* __getElements() const override { return elements.data(); }
    };

    auto* layout = new Impl();
    auto* descs = static_cast<const D3D11_INPUT_ELEMENT_DESC*>(pInputElementDescs);
    layout->elements.assign(descs, descs + NumElements);
    *ppInputLayout = layout;
    return S_OK;
}

HRESULT D3D11Device::CreateRenderTargetView(ID3D11Resource* pResource, const void* pDesc, ID3D11RenderTargetView** ppRTView) {
    if (!pResource || !ppRTView) return E_INVALIDARG;
    void* texPtr = pResource->__metalTexturePtr();
    if (!texPtr) return E_FAIL;

    struct Impl : public ID3D11RenderTargetView {
        MS_COM_BODY()
        STDMETHOD(GetResource)(ID3D11Resource** pp) override { return E_NOTIMPL; }
        ID3D11Resource* resource = nullptr;
        void* __metalTexturePtr() const override { return metalTex; }
        void* metalTex = nullptr;
    };

    auto* rtv = new Impl();
    rtv->resource = pResource;
    rtv->metalTex = texPtr;
    *ppRTView = rtv;
    return S_OK;
}

HRESULT D3D11Device::CreateDepthStencilView(ID3D11Resource* pResource, const void* pDesc, ID3D11DepthStencilView** ppDSView) {
    if (!pResource || !ppDSView) return E_INVALIDARG;
    void* texPtr = pResource->__metalTexturePtr();
    if (!texPtr) return E_FAIL;

    struct Impl : public ID3D11DepthStencilView {
        MS_COM_BODY()
        STDMETHOD(GetResource)(ID3D11Resource** pp) override { return E_NOTIMPL; }
        ID3D11Resource* resource = nullptr;
        void* __metalTexturePtr() const override { return metalTex; }
        void* metalTex = nullptr;
    };

    auto* dsv = new Impl();
    dsv->resource = pResource;
    dsv->metalTex = texPtr;
    *ppDSView = dsv;
    return S_OK;
}

HRESULT D3D11Device::CreateShaderResourceView(ID3D11Resource* pResource, const void* pDesc, ID3D11ShaderResourceView** ppSRView) {
    if (!pResource || !ppSRView) return E_INVALIDARG;
    void* texPtr = pResource->__metalTexturePtr();
    void* bufPtr = pResource->__metalBufferPtr();

    struct Impl : public ID3D11ShaderResourceView {
        MS_COM_BODY()
        STDMETHOD(GetResource)(ID3D11Resource** pp) override { return E_NOTIMPL; }
        ID3D11Resource* resource = nullptr;
        void* __metalTexturePtr() const override { return metalTex; }
        void* __metalBufferPtr() const override { return metalBuf; }
        void* metalTex = nullptr;
        void* metalBuf = nullptr;
    };

    auto* srv = new Impl();
    srv->resource = pResource;
    srv->metalTex = texPtr;
    srv->metalBuf = bufPtr;
    *ppSRView = srv;
    return S_OK;
}

HRESULT D3D11Device::CreateUnorderedAccessView(ID3D11Resource* pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC* pDesc, ID3D11UnorderedAccessView** ppUAView) {
    if (!pResource || !ppUAView) return E_INVALIDARG;
    void* texPtr = pResource->__metalTexturePtr();
    void* bufPtr = pResource->__metalBufferPtr();

    struct Impl : public ID3D11UnorderedAccessView {
        MS_COM_BODY()
        STDMETHOD(GetResource)(ID3D11Resource** pp) override { return E_NOTIMPL; }
        ID3D11Resource* resource = nullptr;
        void* __metalTexturePtr() const override { return metalTex; }
        void* __metalBufferPtr() const override { return metalBuf; }
        void* metalTex = nullptr;
        void* metalBuf = nullptr;
    };

    auto* uav = new Impl();
    uav->resource = pResource;
    uav->metalTex = texPtr;
    uav->metalBuf = bufPtr;
    *ppUAView = uav;
    return S_OK;
}

HRESULT D3D11Device::CreateSamplerState(const void* pSamplerDesc, ID3D11SamplerState** ppSamplerState) {
    if (!pSamplerDesc || !ppSamplerState) return E_INVALIDARG;
    auto* desc = static_cast<const D3D11_SAMPLER_DESC*>(pSamplerDesc);

    auto* sampler = MetalSampler::create(*m_metalDevice,
        desc->Filter, desc->AddressU, desc->AddressV, desc->AddressW,
        desc->MipLODBias, desc->MaxAnisotropy, desc->ComparisonFunc);
    if (!sampler) return E_OUTOFMEMORY;

    struct Impl : public ID3D11SamplerState {
        MS_COM_BODY()
        std::unique_ptr<MetalSampler> metalSampler;
        D3D11_SAMPLER_DESC desc;
        void* __metalSamplerState() const override { return metalSampler ? metalSampler->nativeSamplerState() : nullptr; }
    };

    auto* impl = new Impl();
    impl->metalSampler = std::unique_ptr<MetalSampler>(sampler);
    impl->desc = *desc;
    *ppSamplerState = impl;
    return S_OK;
}

HRESULT D3D11Device::CreateRasterizerState(const void* pRasterizerDesc, ID3D11RasterizerState** ppRasterizerState) {
    if (!pRasterizerDesc || !ppRasterizerState) return E_INVALIDARG;
    auto* desc = static_cast<const D3D11_RASTERIZER_DESC*>(pRasterizerDesc);

    struct Impl : public ID3D11RasterizerState {
        MS_COM_BODY()
        D3D11_RASTERIZER_DESC desc;
        INT __getCullMode() const override { return desc.CullMode; }
        INT __getFillMode() const override { return desc.FillMode; }
        INT __getDepthClipEnable() const override { return desc.DepthClipEnable; }
        INT __getFrontCCW() const override { return desc.FrontCounterClockwise; }
        INT __getDepthBias() const override { return desc.DepthBias; }
        FLOAT __getSlopeScaledDepthBias() const override { return desc.SlopeScaledDepthBias; }
    };

    auto* impl = new Impl();
    impl->desc = *desc;
    *ppRasterizerState = impl;
    return S_OK;
}

HRESULT D3D11Device::CreateDepthStencilState(const void* pDepthStencilDesc, ID3D11DepthStencilState** ppDepthStencilState) {
    if (!pDepthStencilDesc || !ppDepthStencilState) return E_INVALIDARG;
    auto* desc = static_cast<const D3D11_DEPTH_STENCIL_DESC*>(pDepthStencilDesc);

    struct Impl : public ID3D11DepthStencilState {
        MS_COM_BODY()
        D3D11_DEPTH_STENCIL_DESC desc;
        INT __getDepthEnable() const override { return desc.DepthEnable; }
        UINT __getDepthWriteMask() const override { return desc.DepthWriteMask; }
        UINT __getDepthFunc() const override { return desc.DepthFunc; }
        INT __getStencilEnable() const override { return desc.StencilEnable; }
        UINT __getStencilReadMask() const override { return desc.StencilReadMask; }
        UINT __getStencilWriteMask() const override { return desc.StencilWriteMask; }
    };

    auto* impl = new Impl();
    impl->desc = *desc;
    *ppDepthStencilState = impl;
    return S_OK;
}

HRESULT D3D11Device::CreateBlendState(const void* pBlendStateDesc, ID3D11BlendState** ppBlendState) {
    if (!pBlendStateDesc || !ppBlendState) return E_INVALIDARG;
    auto* desc = static_cast<const D3D11_BLEND_DESC*>(pBlendStateDesc);

    struct Impl : public ID3D11BlendState {
        MS_COM_BODY()
        D3D11_BLEND_DESC desc;
        INT __getBlendEnable(UINT i) const override { return desc.RenderTarget[i < 8 ? i : 0].BlendEnable; }
        UINT __getSrcBlend(UINT i) const override { return desc.RenderTarget[i < 8 ? i : 0].SrcBlend; }
        UINT __getDestBlend(UINT i) const override { return desc.RenderTarget[i < 8 ? i : 0].DestBlend; }
        UINT __getBlendOp(UINT i) const override { return desc.RenderTarget[i < 8 ? i : 0].BlendOp; }
        UINT __getSrcBlendAlpha(UINT i) const override { return desc.RenderTarget[i < 8 ? i : 0].SrcBlendAlpha; }
        UINT __getDestBlendAlpha(UINT i) const override { return desc.RenderTarget[i < 8 ? i : 0].DestBlendAlpha; }
        UINT __getBlendOpAlpha(UINT i) const override { return desc.RenderTarget[i < 8 ? i : 0].BlendOpAlpha; }
        UINT __getRenderTargetWriteMask(UINT i) const override { return desc.RenderTarget[i < 8 ? i : 0].RenderTargetWriteMask; }
        INT __getAlphaToCoverageEnable() const override { return desc.AlphaToCoverageEnable; }
    };

    auto* impl = new Impl();
    impl->desc = *desc;
    *ppBlendState = impl;
    return S_OK;
}

HRESULT D3D11Device::CreateQuery(const D3D11_QUERY_DESC* pQueryDesc, ID3D11Query** ppQuery) {
    if (!pQueryDesc || !ppQuery) return E_INVALIDARG;

    struct Impl : public ID3D11Query {
        MS_COM_BODY()
        D3D11_QUERY_DESC desc;
        void GetDesc(D3D11_QUERY_DESC* p) override { if(p) *p = desc; }
        UINT __getQueryType() const override { return desc.Query; }
    };

    auto* impl = new Impl();
    impl->desc = *pQueryDesc;
    *ppQuery = impl;
    return S_OK;
}

HRESULT D3D11Device::CreatePredicate(const D3D11_QUERY_DESC* pPredicateDesc, ID3D11Predicate** ppPredicate) {
    if (!pPredicateDesc || !ppPredicate) return E_INVALIDARG;

    struct Impl : public ID3D11Predicate {
        MS_COM_BODY()
    };

    *ppPredicate = new Impl();
    return S_OK;
}

void D3D11Device::GetImmediateContext(ID3D11DeviceContext** ppImmediateContext) {
    if (ppImmediateContext) {
        m_immediateContext->AddRef();
        *ppImmediateContext = m_immediateContext.get();
    }
}

HRESULT D3D11Device::GetDeviceFeatureLevel(UINT* pFeatureLevel) {
    if (!pFeatureLevel) return E_POINTER;
    *pFeatureLevel = 0xb000;
    return S_OK;
}

}
