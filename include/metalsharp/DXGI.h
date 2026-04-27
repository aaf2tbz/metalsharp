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
    void* metalLayer() const;

private:
    MetalSwapChain();
    bool init(MetalDevice& device, void* window, uint32_t width, uint32_t height, uint32_t bufferCount);

    struct Impl;
    Impl* m_impl;
};

class D3D11Device;

class DXGISwapChainImpl : public IDXGISwapChain1 {
public:
    static HRESULT create(MetalDevice* metalDevice, HWND window, uint32_t width, uint32_t height, uint32_t bufferCount, DXGI_FORMAT format, IDXGISwapChain** ppSwapChain);

    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObject) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;
    STDMETHOD(GetDevice)(const GUID& riid, void** ppDevice) override;
    STDMETHOD(GetPrivateData)(const GUID& guid, UINT* pDataSize, void* pData) override;
    STDMETHOD(SetPrivateData)(const GUID& guid, UINT DataSize, const void* pData) override;
    STDMETHOD(SetPrivateDataInterface)(const GUID& guid, const IUnknown* pData) override;
    STDMETHOD(GetParent)(const GUID& riid, void** ppParent) override;
    STDMETHOD(Present)(UINT SyncInterval, UINT Flags) override;
    STDMETHOD(GetBuffer)(UINT Buffer, const GUID& riid, void** ppSurface) override;
    STDMETHOD(SetFullscreenState)(INT Fullscreen, IDXGIOutput* pTarget) override;
    STDMETHOD(GetFullscreenState)(INT* pFullscreen, IDXGIOutput** ppTarget) override;
    STDMETHOD(GetDesc)(void* pDesc) override;
    STDMETHOD(ResizeBuffers)(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) override;
    STDMETHOD(ResizeTarget)(const DXGI_MODE_DESC* pNewTargetParameters) override;
    STDMETHOD(GetContainingOutput)(IDXGIOutput** ppOutput) override;
    STDMETHOD(GetFrameStatistics)(void* pStats) override;
    STDMETHOD(GetLastPresentCount)(UINT* pLastPresentCount) override;
    STDMETHOD(Present1)(UINT SyncInterval, UINT PresentFlags, const void* pPresentParameters) override;

private:
    DXGISwapChainImpl() = default;
    ~DXGISwapChainImpl();

    ULONG m_refCount = 1;
    MetalDevice* m_metalDevice = nullptr;
    std::unique_ptr<MetalSwapChain> m_metalSwapChain;
    HWND m_hwnd = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    DXGI_FORMAT m_format = 0;
    uint32_t m_bufferCount = 2;
    void* m_backBufferTexture = nullptr;
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
