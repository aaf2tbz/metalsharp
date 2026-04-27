#include <metalsharp/DXGI.h>
#include <metalsharp/D3D11Device.h>
#include <metalsharp/Platform.h>
#ifdef METALSHARP_NATIVE_LOADER
#include <metalsharp/WindowManager.h>
#endif
#include <AppKit/AppKit.h>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>
#include <QuartzCore/QuartzCore.h>
#include <cstring>
#include <cstdio>

namespace metalsharp {

struct MetalSwapChain::Impl {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commandQueue = nil;
    CAMetalLayer* layer = nil;
    id<CAMetalDrawable> currentDrawable = nil;
    uint32_t bufferCount = 2;
    uint32_t width = 0;
    uint32_t height = 0;
};

MetalSwapChain::MetalSwapChain() : m_impl(new Impl()) {}
MetalSwapChain::~MetalSwapChain() { delete m_impl; }

bool MetalSwapChain::init(MetalDevice& device, void* window, uint32_t width, uint32_t height, uint32_t bufferCount) {
    m_impl->device = (__bridge id<MTLDevice>)device.nativeDevice();
    m_impl->commandQueue = (__bridge id<MTLCommandQueue>)device.nativeCommandQueue();
    m_impl->width = width;
    m_impl->height = height;
    m_impl->bufferCount = bufferCount;

    if (window) {
        NSView* view = nil;
#ifdef METALSHARP_NATIVE_LOADER
        void* nsWinPtr = win32::WindowManager::instance().getNSWindow((HANDLE)window);
        if (nsWinPtr) {
            NSWindow* nsWin = (__bridge NSWindow*)nsWinPtr;
            view = [nsWin contentView];
        }
#endif
        if (!view) {
            view = (__bridge NSView*)window;
        }

        if (view) {
            m_impl->layer = [CAMetalLayer layer];
            m_impl->layer.device = m_impl->device;
            m_impl->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            m_impl->layer.framebufferOnly = NO;
            m_impl->layer.frame = view.bounds;
            m_impl->layer.drawableSize = CGSizeMake(width > 0 ? width : view.bounds.size.width,
                                                      height > 0 ? height : view.bounds.size.height);
            view.wantsLayer = YES;
            view.layer = m_impl->layer;

            if (width == 0 || height == 0) {
                m_impl->width = (uint32_t)view.bounds.size.width;
                m_impl->height = (uint32_t)view.bounds.size.height;
            }
        }
    }

    return true;
}

MetalSwapChain* MetalSwapChain::create(MetalDevice& device, void* window, uint32_t width, uint32_t height, uint32_t bufferCount) {
    auto* chain = new MetalSwapChain();
    if (!chain->init(device, window, width, height, bufferCount)) {
        delete chain;
        return nullptr;
    }
    return chain;
}

void MetalSwapChain::present(uint32_t syncInterval) {
    if (!m_impl->layer) return;

    @autoreleasepool {
        m_impl->currentDrawable = [m_impl->layer nextDrawable];
        if (!m_impl->currentDrawable) return;

        id<MTLCommandBuffer> cmdBuffer = [m_impl->commandQueue commandBuffer];
        [cmdBuffer presentDrawable:m_impl->currentDrawable];
        [cmdBuffer commit];
    }

#ifdef METALSHARP_NATIVE_LOADER
    win32::WindowManager::instance().pumpEvents();
#endif
}

void* MetalSwapChain::getCurrentDrawable() {
    if (!m_impl->currentDrawable) {
        @autoreleasepool {
            m_impl->currentDrawable = [m_impl->layer nextDrawable];
        }
    }
    return (__bridge void*)m_impl->currentDrawable;
}

void* MetalSwapChain::getCurrentTexture() {
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)getCurrentDrawable();
    if (!drawable) return nullptr;
    return (__bridge void*)drawable.texture;
}

void MetalSwapChain::resize(uint32_t width, uint32_t height) {
    m_impl->width = width;
    m_impl->height = height;
    if (m_impl->layer) {
        CGSize size = CGSizeMake(width, height);
        m_impl->layer.drawableSize = size;
    }
    m_impl->currentDrawable = nil;
}

void* MetalSwapChain::metalLayer() const {
    return (__bridge void*)m_impl->layer;
}

struct DXGISwapChainBackBuffer : public ID3D11Texture2D {
    ULONG refCount = 1;
    void* metalTexture = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    HRESULT QueryInterface(REFIID, void** ppv) override { AddRef(); *ppv = this; return S_OK; }
    ULONG AddRef() override { return ++refCount; }
    ULONG Release() override { ULONG c = --refCount; if (c == 0) delete this; return c; }
    HRESULT GetDevice(ID3D11Device** ppDevice) override { return E_NOTIMPL; }
    HRESULT GetPrivateData(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT SetPrivateData(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
    HRESULT SetPrivateDataInterface(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
    HRESULT GetType(UINT* pType) override { if (pType) *pType = D3D11_RESOURCE_DIMENSION_TEXTURE2D; return S_OK; }
    HRESULT SetEvictionPriority(UINT) override { return S_OK; }
    UINT GetEvictionPriority() override { return 0; }
    void GetDesc(struct D3D11_TEXTURE2D_DESC* pDesc) override {
        if (pDesc) {
            memset(pDesc, 0, sizeof(D3D11_TEXTURE2D_DESC));
            pDesc->Width = width;
            pDesc->Height = height;
            pDesc->Format = format;
            pDesc->MipLevels = 1;
            pDesc->ArraySize = 1;
            pDesc->SampleDesc.Count = 1;
            pDesc->BindFlags = D3D11_BIND_RENDER_TARGET;
        }
    }
    void* __metalTexturePtr() const override { return metalTexture; }
    void* __metalBufferPtr() const override { return nullptr; }
};

DXGISwapChainImpl::~DXGISwapChainImpl() {
    if (m_backBufferTexture) {
        auto* tex = static_cast<DXGISwapChainBackBuffer*>(m_backBufferTexture);
        tex->Release();
    }
}

HRESULT DXGISwapChainImpl::create(MetalDevice* metalDevice, HWND window, uint32_t width, uint32_t height, uint32_t bufferCount, DXGI_FORMAT format, IDXGISwapChain** ppSwapChain) {
    if (!ppSwapChain || !metalDevice) return E_POINTER;

    auto* swapChain = new DXGISwapChainImpl();
    swapChain->m_metalDevice = metalDevice;
    swapChain->m_hwnd = window;
    swapChain->m_width = width;
    swapChain->m_height = height;
    swapChain->m_format = format;
    swapChain->m_bufferCount = bufferCount;

    swapChain->m_metalSwapChain.reset(
        MetalSwapChain::create(*metalDevice, (void*)window, width, height, bufferCount));

    if (!swapChain->m_metalSwapChain) {
        delete swapChain;
        return E_FAIL;
    }

    *ppSwapChain = swapChain;
    return S_OK;
}

HRESULT DXGISwapChainImpl::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    AddRef();
    *ppvObject = this;
    return S_OK;
}

ULONG DXGISwapChainImpl::AddRef() { return ++m_refCount; }
ULONG DXGISwapChainImpl::Release() {
    ULONG c = --m_refCount;
    if (c == 0) delete this;
    return c;
}

HRESULT DXGISwapChainImpl::GetDevice(const GUID& riid, void** ppDevice) { return E_NOTIMPL; }
HRESULT DXGISwapChainImpl::GetPrivateData(const GUID&, UINT*, void*) { return E_NOTIMPL; }
HRESULT DXGISwapChainImpl::SetPrivateData(const GUID&, UINT, const void*) { return E_NOTIMPL; }
HRESULT DXGISwapChainImpl::SetPrivateDataInterface(const GUID&, const IUnknown*) { return E_NOTIMPL; }
HRESULT DXGISwapChainImpl::GetParent(const GUID& riid, void** ppParent) { return E_NOTIMPL; }

HRESULT DXGISwapChainImpl::Present(UINT SyncInterval, UINT Flags) {
    if (m_metalSwapChain) {
        m_metalSwapChain->present(SyncInterval);
        m_backBufferTexture = nullptr;
    }
    return S_OK;
}

HRESULT DXGISwapChainImpl::GetBuffer(UINT Buffer, const GUID& riid, void** ppSurface) {
    if (!ppSurface) return E_POINTER;
    if (!m_metalSwapChain) return E_FAIL;

    void* texPtr = m_metalSwapChain->getCurrentTexture();
    if (!texPtr) return E_FAIL;

    auto* backBuffer = new DXGISwapChainBackBuffer();
    backBuffer->metalTexture = texPtr;
    backBuffer->width = m_width;
    backBuffer->height = m_height;
    backBuffer->format = m_format;

    if (m_backBufferTexture) {
        static_cast<DXGISwapChainBackBuffer*>(m_backBufferTexture)->Release();
    }
    m_backBufferTexture = backBuffer;
    backBuffer->AddRef();

    *ppSurface = backBuffer;
    return S_OK;
}

HRESULT DXGISwapChainImpl::SetFullscreenState(INT Fullscreen, IDXGIOutput* pTarget) {
#ifdef METALSHARP_NATIVE_LOADER
    void* nsWinPtr = win32::WindowManager::instance().getNSWindow(m_hwnd);
    if (nsWinPtr) {
        NSWindow* nsWin = (__bridge NSWindow*)nsWinPtr;
        if (Fullscreen) {
            [nsWin toggleFullScreen:nil];
        } else {
            if ([nsWin styleMask] & NSWindowStyleMaskFullScreen) {
                [nsWin toggleFullScreen:nil];
            }
        }
    }
#endif
    return S_OK;
}
HRESULT DXGISwapChainImpl::GetFullscreenState(INT* pFullscreen, IDXGIOutput** ppTarget) {
    if (pFullscreen) *pFullscreen = FALSE;
    if (ppTarget) *ppTarget = nullptr;
    return S_OK;
}

HRESULT DXGISwapChainImpl::GetDesc(void* pDesc) {
    if (!pDesc) return E_POINTER;
    auto* desc = static_cast<DXGI_SWAP_CHAIN_DESC*>(pDesc);
    memset(desc, 0, sizeof(DXGI_SWAP_CHAIN_DESC));
    desc->BufferDesc.Width = m_width;
    desc->BufferDesc.Height = m_height;
    desc->BufferDesc.Format = m_format;
    desc->SampleDesc.Count = 1;
    desc->BufferUsage = 0x20;
    desc->BufferCount = m_bufferCount;
    desc->OutputWindow = m_hwnd;
    desc->Windowed = TRUE;
    return S_OK;
}

HRESULT DXGISwapChainImpl::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    if (BufferCount > 0) m_bufferCount = BufferCount;
    if (NewFormat != DXGI_FORMAT_UNKNOWN) m_format = NewFormat;

    if (m_metalSwapChain) {
        uint32_t newW = Width > 0 ? Width : m_width;
        uint32_t newH = Height > 0 ? Height : m_height;
        m_metalSwapChain->resize(newW, newH);
        m_width = newW;
        m_height = newH;
    }

    if (m_backBufferTexture) {
        static_cast<DXGISwapChainBackBuffer*>(m_backBufferTexture)->Release();
        m_backBufferTexture = nullptr;
    }

    return S_OK;
}

HRESULT DXGISwapChainImpl::ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters) { return S_OK; }
HRESULT DXGISwapChainImpl::GetContainingOutput(IDXGIOutput** ppOutput) { return E_NOTIMPL; }
HRESULT DXGISwapChainImpl::GetFrameStatistics(void* pStats) { return E_NOTIMPL; }
HRESULT DXGISwapChainImpl::GetLastPresentCount(UINT* pLastPresentCount) {
    if (pLastPresentCount) *pLastPresentCount = 0;
    return S_OK;
}

HRESULT DXGISwapChainImpl::Present1(UINT SyncInterval, UINT PresentFlags, const void* pPresentParameters) {
    return Present(SyncInterval, PresentFlags);
}

}
