#include <metalsharp/D3D11Device.h>
#include <metalsharp/D3D11DeviceContext.h>
#include <metalsharp/FormatTranslation.h>
#include <metalsharp/PipelineState.h>
#include <cstring>

namespace metalsharp {

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

    struct D3D11BufferImpl : public ID3D11Buffer {
        ULONG refCount = 1;
        std::unique_ptr<MetalBuffer> metalBuffer;
        D3D11_BUFFER_DESC desc;

        HRESULT QueryInterface(REFIID riid, void** ppv) override {
            if (!ppv) return E_POINTER;
            if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11Resource) || riid == __uuidof(ID3D11Buffer)) {
                AddRef(); *ppv = this; return S_OK;
            }
            return E_NOINTERFACE;
        }
        ULONG AddRef() override { return ++refCount; }
        ULONG Release() override { ULONG c = --refCount; if (c == 0) delete this; return c; }
        STDMETHOD(GetDevice)(ID3D11Device**) override { return E_NOTIMPL; }
        STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
        STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
        STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
        STDMETHOD(GetType)(UINT*) override { return E_NOTIMPL; }
        STDMETHOD(SetEvictionPriority)(UINT) override { return S_OK; }
        STDMETHOD_(UINT, GetEvictionPriority)() override { return 0; }
        void* __metalBufferPtr() const override { return metalBuffer ? metalBuffer->nativeBuffer() : nullptr; }
    };

    auto* buf = new D3D11BufferImpl();
    buf->metalBuffer = std::unique_ptr<MetalBuffer>(metalBuf);
    buf->desc = *pDesc;
    *ppBuffer = buf;
    return S_OK;
}

HRESULT D3D11Device::CreateTexture2D(const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D) {
    if (!pDesc || !ppTexture2D) return E_INVALIDARG;

    uint32_t metalFmt = dxgiFormatToMetal((DXGITranslation)pDesc->Format);
    uint32_t usage = 0;
    if (pDesc->BindFlags & 0x20) usage |= 0x1;
    if (pDesc->BindFlags & 0x1) usage |= 0x2;
    if (pDesc->BindFlags & 0x2) usage |= 0x4;

    MetalTexture* tex = MetalTexture::create2D(*m_metalDevice, pDesc->Width, pDesc->Height, metalFmt, usage);
    if (!tex) return E_OUTOFMEMORY;

    struct Texture2DImpl : public ID3D11Texture2D {
        ULONG refCount = 1;
        std::unique_ptr<MetalTexture> metalTexture;
        D3D11_TEXTURE2D_DESC desc;

        HRESULT QueryInterface(REFIID riid, void** ppv) override {
            if (!ppv) return E_POINTER;
            if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11Resource) || riid == __uuidof(ID3D11Texture2D)) {
                AddRef(); *ppv = this; return S_OK;
            }
            return E_NOINTERFACE;
        }
        ULONG AddRef() override { return ++refCount; }
        ULONG Release() override { ULONG c = --refCount; if (c == 0) delete this; return c; }
        STDMETHOD(GetDevice)(ID3D11Device**) override { return E_NOTIMPL; }
        STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
        STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
        STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
        STDMETHOD(GetType)(UINT*) override { return E_NOTIMPL; }
        STDMETHOD(SetEvictionPriority)(UINT) override { return S_OK; }
        STDMETHOD_(UINT, GetEvictionPriority)() override { return 0; }
    };

    auto* impl = new Texture2DImpl();
    impl->metalTexture = std::unique_ptr<MetalTexture>(tex);
    impl->desc = *pDesc;
    *ppTexture2D = impl;
    return S_OK;
}

HRESULT D3D11Device::CreateVertexShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage*, ID3D11VertexShader** ppVertexShader) {
    if (!pShaderBytecode || !ppVertexShader) return E_INVALIDARG;

    struct VertexShaderImpl : public ID3D11VertexShader {
        ULONG refCount = 1;
        void* metalVertexFunction = nullptr;

        HRESULT QueryInterface(REFIID riid, void** ppv) override {
            if (!ppv) return E_POINTER;
            if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11VertexShader)) {
                AddRef(); *ppv = this; return S_OK;
            }
            return E_NOINTERFACE;
        }
        ULONG AddRef() override { return ++refCount; }
        ULONG Release() override { ULONG c = --refCount; if (c == 0) delete this; return c; }
        STDMETHOD(GetDevice)(ID3D11Device**) override { return E_NOTIMPL; }
        STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
        STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
        STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
        void* __metalVertexFunction() const override { return metalVertexFunction; }
    };

    CompiledShader compiled;
    if (!m_shaderTranslator->compileMSL(
            reinterpret_cast<const char*>(pShaderBytecode),
            "vertexShader", "fragmentShader", compiled)) {
        return E_FAIL;
    }

    auto* vs = new VertexShaderImpl();
    vs->metalVertexFunction = compiled.vertexFunction;
    *ppVertexShader = vs;
    return S_OK;
}

HRESULT D3D11Device::CreatePixelShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage*, ID3D11PixelShader** ppPixelShader) {
    if (!pShaderBytecode || !ppPixelShader) return E_INVALIDARG;

    struct PixelShaderImpl : public ID3D11PixelShader {
        ULONG refCount = 1;
        void* metalFragmentFunction = nullptr;

        HRESULT QueryInterface(REFIID riid, void** ppv) override {
            if (!ppv) return E_POINTER;
            if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11PixelShader)) {
                AddRef(); *ppv = this; return S_OK;
            }
            return E_NOINTERFACE;
        }
        ULONG AddRef() override { return ++refCount; }
        ULONG Release() override { ULONG c = --refCount; if (c == 0) delete this; return c; }
        STDMETHOD(GetDevice)(ID3D11Device**) override { return E_NOTIMPL; }
        STDMETHOD(GetPrivateData)(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
        STDMETHOD(SetPrivateData)(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
        STDMETHOD(SetPrivateDataInterface)(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
        void* __metalFragmentFunction() const override { return metalFragmentFunction; }
    };

    CompiledShader compiled;
    if (!m_shaderTranslator->compileMSL(
            reinterpret_cast<const char*>(pShaderBytecode),
            "vertexShader", "fragmentShader", compiled)) {
        return E_FAIL;
    }

    auto* ps = new PixelShaderImpl();
    ps->metalFragmentFunction = compiled.fragmentFunction;
    *ppPixelShader = ps;
    return S_OK;
}

HRESULT D3D11Device::CreateGeometryShader(const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11GeometryShader**) { return E_NOTIMPL; }
HRESULT D3D11Device::CreateComputeShader(const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11ComputeShader**) { return E_NOTIMPL; }
HRESULT D3D11Device::CreateInputLayout(const void*, UINT, const void*, SIZE_T, ID3D11InputLayout**) { return E_NOTIMPL; }
HRESULT D3D11Device::CreateShaderResourceView(ID3D11Resource*, const void*, ID3D11ShaderResourceView**) { return E_NOTIMPL; }
HRESULT D3D11Device::CreateRenderTargetView(ID3D11Resource*, const void*, ID3D11RenderTargetView**) { return E_NOTIMPL; }
HRESULT D3D11Device::CreateDepthStencilView(ID3D11Resource*, const void*, ID3D11DepthStencilView**) { return E_NOTIMPL; }
HRESULT D3D11Device::CreateSamplerState(const void*, ID3D11SamplerState**) { return E_NOTIMPL; }
HRESULT D3D11Device::CreateRasterizerState(const void*, ID3D11RasterizerState**) { return E_NOTIMPL; }
HRESULT D3D11Device::CreateDepthStencilState(const void*, ID3D11DepthStencilState**) { return E_NOTIMPL; }
HRESULT D3D11Device::CreateBlendState(const void*, ID3D11BlendState**) { return E_NOTIMPL; }

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
