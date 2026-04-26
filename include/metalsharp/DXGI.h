#pragma once

#include <dxgi/DXGI.h>
#include <metalsharp/MetalBackend.h>
#include <memory>

namespace metalsharp {

class MetalSwapChain {
public:
    static MetalSwapChain* create(MetalDevice& device, void* window, uint32_t width, uint32_t height, uint32_t bufferCount);
    ~MetalSwapChain();

    void present(uint32_t syncInterval);
    void* getCurrentDrawable();
    void* getCurrentTexture();
    void resize(uint32_t width, uint32_t height);

private:
    MetalSwapChain();
    bool init(MetalDevice& device, void* window, uint32_t width, uint32_t height, uint32_t bufferCount);

    struct Impl;
    Impl* m_impl;
};

class DXGIFactory : public IDXGIFactory4 {
public:
    static HRESULT create(const GUID& riid, void** ppFactory);

    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObject) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;
    STDMETHOD(EnumAdapters)(UINT Adapter, IDXGIAdapter** ppAdapter) override;
    STDMETHOD(MakeWindowAssociation)(HWND, UINT) override { return S_OK; }
    STDMETHOD(GetWindowAssociation)(HWND*) override { return E_NOTIMPL; }
    STDMETHOD(CreateSwapChain)(IUnknown* pDevice, void* pDesc, IDXGISwapChain** ppSwapChain) override;
    STDMETHOD(CreateSoftwareAdapter)(HMODULE, IDXGIAdapter**) override { return E_NOTIMPL; }
    STDMETHOD(EnumAdapters1)(UINT, IDXGIAdapter**) override { return DXGI_ERROR_NOT_FOUND; }
    STDMETHOD_(INT, IsCurrent)() override { return TRUE; }

private:
    DXGIFactory() = default;
    ~DXGIFactory() = default;
    ULONG m_refCount = 1;
};

}
