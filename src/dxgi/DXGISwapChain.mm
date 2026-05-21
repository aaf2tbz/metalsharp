/// @file DXGISwapChain.mm
/// @brief DXGI swap chain — CAMetalLayer-backed presentation and frame pacing.
///
/// Implements IDXGISwapChain using a CAMetalLayer for Metal-backed window presentation.
/// Handles buffer creation, present calls, resize, and full-screen transitions against
/// the native macOS windowing system.

#include <metalsharp/D3D11Device.h>
#include <metalsharp/DXGI.h>
#include <metalsharp/FormatTranslation.h>
#include <metalsharp/Logger.h>
#include <metalsharp/Platform.h>
#ifdef METALSHARP_NATIVE_LOADER
#include <metalsharp/WindowManager.h>
#endif
#include <AppKit/AppKit.h>
#include <cstdio>
#include <cstring>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>
#include <QuartzCore/QuartzCore.h>

static MTLPixelFormat dxgiFormatToSwapchainMetal(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return MTLPixelFormatRGBA8Unorm;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return MTLPixelFormatRGBA8Unorm_sRGB;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return MTLPixelFormatBGRA8Unorm;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return MTLPixelFormatBGRA8Unorm_sRGB;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return MTLPixelFormatRGBA16Float;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return MTLPixelFormatRGB10A2Unorm;
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
        return MTLPixelFormatBGR10A2Unorm;
    default:
        return MTLPixelFormatBGRA8Unorm;
    }
}

static bool isHDRFormat(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM;
}

static const GUID IID_ID3D12ResourceLocal = {
    0x696442be, 0xa72e, 0x4056, {0xbc, 0x83, 0x34, 0x96, 0x4d, 0x34, 0xe2, 0xf3}};

namespace metalsharp {

HRESULT createD3D12SwapChainBackBuffer(void* nativeTexture, uint32_t width, uint32_t height, uint32_t format,
                                       void** ppResource);

struct MetalSwapChain::Impl {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commandQueue = nil;
    CAMetalLayer* layer = nil;
    id<CAMetalDrawable> currentDrawable = nil;
    id<MTLTexture> offscreenTexture = nil;
    uint64_t drawableAcquireCount = 0;
    uint64_t textureRequestCount = 0;
    uint64_t presentCount = 0;
    uint64_t resizeCount = 0;
    uint32_t bufferCount = 2;
    uint32_t width = 0;
    uint32_t height = 0;
    DXGI_FORMAT dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    bool hdrEnabled = false;
};

static bool shouldLogSwapChainEvent(uint64_t count) {
    return count <= 5 || (count % 120) == 0;
}

MetalSwapChain::MetalSwapChain() : m_impl(new Impl()) {}
MetalSwapChain::~MetalSwapChain() {
    delete m_impl;
}

bool MetalSwapChain::init(MetalDevice& device, void* window, uint32_t width, uint32_t height, uint32_t bufferCount,
                          DXGI_FORMAT format) {
    m_impl->device = (__bridge id<MTLDevice>)device.nativeDevice();
    m_impl->commandQueue = (__bridge id<MTLCommandQueue>)device.nativeCommandQueue();
    m_impl->width = width;
    m_impl->height = height;
    m_impl->bufferCount = bufferCount;
    m_impl->dxgiFormat = format;

    MTLPixelFormat metalFormat = dxgiFormatToSwapchainMetal(format);

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
            m_impl->layer.pixelFormat = metalFormat;
            m_impl->layer.framebufferOnly = NO;
            m_impl->layer.frame = view.bounds;
            m_impl->layer.drawableSize =
                CGSizeMake(width > 0 ? width : view.bounds.size.width, height > 0 ? height : view.bounds.size.height);
            view.wantsLayer = YES;
            view.layer = m_impl->layer;

            if (isHDRFormat(format)) {
                m_impl->hdrEnabled = true;
                m_impl->layer.wantsExtendedDynamicRangeContent = YES;
                if (@available(macOS 10.15.4, *)) {
                    m_impl->layer.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);
                }
            }

            if (width == 0 || height == 0) {
                m_impl->width = (uint32_t)view.bounds.size.width;
                m_impl->height = (uint32_t)view.bounds.size.height;
            }

            MS_INFO("dxgi_swapchain_init width=%u height=%u buffer_count=%u dxgi_format=0x%x metal_format=%u "
                    "layer=true hdr=%s",
                    m_impl->width, m_impl->height, m_impl->bufferCount, format, (unsigned)metalFormat,
                    m_impl->hdrEnabled ? "true" : "false");
        }
    }

    if (!m_impl->layer) {
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:metalFormat
                                                                                        width:width
                                                                                       height:height
                                                                                    mipmapped:NO];
        desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite |
                     MTLTextureUsagePixelFormatView;
        desc.storageMode = MTLStorageModePrivate;
        m_impl->offscreenTexture = [m_impl->device newTextureWithDescriptor:desc];
        MS_INFO("dxgi_swapchain_init width=%u height=%u buffer_count=%u dxgi_format=0x%x metal_format=%u "
                "layer=false offscreen=%s hdr=%s",
                m_impl->width, m_impl->height, m_impl->bufferCount, format, (unsigned)metalFormat,
                m_impl->offscreenTexture ? "true" : "false", m_impl->hdrEnabled ? "true" : "false");
    }

    return true;
}

MetalSwapChain* MetalSwapChain::create(MetalDevice& device, void* window, uint32_t width, uint32_t height,
                                       uint32_t bufferCount, DXGI_FORMAT format) {
    auto* chain = new MetalSwapChain();
    if (!chain->init(device, window, width, height, bufferCount, format)) {
        delete chain;
        return nullptr;
    }
    return chain;
}

void MetalSwapChain::present(uint32_t syncInterval) {
    ++m_impl->presentCount;
    if (!m_impl->layer) {
        if (shouldLogSwapChainEvent(m_impl->presentCount)) {
            MS_INFO("dxgi_swapchain_present count=%llu sync=%u layer=false offscreen=%s",
                    (unsigned long long)m_impl->presentCount, syncInterval,
                    m_impl->offscreenTexture ? "true" : "false");
        }
        return;
    }

    @autoreleasepool {
        bool reusedDrawable = m_impl->currentDrawable != nil;
        id<CAMetalDrawable> drawable = m_impl->currentDrawable;
        if (!drawable) {
            drawable = [m_impl->layer nextDrawable];
            ++m_impl->drawableAcquireCount;
        }

        if (!drawable) {
            MS_WARN("dxgi_swapchain_present_no_drawable count=%llu sync=%u reused=false",
                    (unsigned long long)m_impl->presentCount, syncInterval);
            return;
        }

        id<MTLCommandBuffer> cmdBuffer = [m_impl->commandQueue commandBuffer];
        [cmdBuffer presentDrawable:drawable];
        [cmdBuffer commit];
        m_impl->currentDrawable = nil;

        if (shouldLogSwapChainEvent(m_impl->presentCount)) {
            MS_INFO("dxgi_swapchain_present count=%llu sync=%u reused_drawable=%s acquired=%llu size=%ux%u",
                    (unsigned long long)m_impl->presentCount, syncInterval, reusedDrawable ? "true" : "false",
                    (unsigned long long)m_impl->drawableAcquireCount, m_impl->width, m_impl->height);
        }
    }

#ifdef METALSHARP_NATIVE_LOADER
    win32::WindowManager::instance().pumpEvents();
#endif
}

void* MetalSwapChain::getCurrentDrawable() {
    if (!m_impl->currentDrawable) {
        @autoreleasepool {
            m_impl->currentDrawable = [m_impl->layer nextDrawable];
            if (m_impl->currentDrawable)
                ++m_impl->drawableAcquireCount;
        }
    }
    return (__bridge void*)m_impl->currentDrawable;
}

void* MetalSwapChain::getCurrentTexture() {
    ++m_impl->textureRequestCount;
    if (!m_impl->layer && m_impl->offscreenTexture) {
        if (shouldLogSwapChainEvent(m_impl->textureRequestCount)) {
            MS_INFO("dxgi_swapchain_get_texture count=%llu layer=false offscreen=true size=%ux%u",
                    (unsigned long long)m_impl->textureRequestCount, m_impl->width, m_impl->height);
        }
        return (__bridge void*)m_impl->offscreenTexture;
    }
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)getCurrentDrawable();
    if (!drawable) {
        MS_WARN("dxgi_swapchain_get_texture_no_drawable count=%llu layer=%s",
                (unsigned long long)m_impl->textureRequestCount, m_impl->layer ? "true" : "false");
        return nullptr;
    }
    if (shouldLogSwapChainEvent(m_impl->textureRequestCount)) {
        MS_INFO("dxgi_swapchain_get_texture count=%llu layer=true drawable_acquired=%llu size=%ux%u",
                (unsigned long long)m_impl->textureRequestCount, (unsigned long long)m_impl->drawableAcquireCount,
                m_impl->width, m_impl->height);
    }
    return (__bridge void*)drawable.texture;
}

void MetalSwapChain::resize(uint32_t width, uint32_t height) {
    ++m_impl->resizeCount;
    m_impl->width = width;
    m_impl->height = height;
    if (m_impl->layer) {
        CGSize size = CGSizeMake(width, height);
        m_impl->layer.drawableSize = size;
    } else if (m_impl->offscreenTexture) {
        MTLPixelFormat metalFormat = dxgiFormatToSwapchainMetal(m_impl->dxgiFormat);
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:metalFormat
                                                                                        width:width
                                                                                       height:height
                                                                                    mipmapped:NO];
        desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite |
                     MTLTextureUsagePixelFormatView;
        desc.storageMode = MTLStorageModePrivate;
        m_impl->offscreenTexture = [m_impl->device newTextureWithDescriptor:desc];
    }
    m_impl->currentDrawable = nil;
    MS_INFO("dxgi_swapchain_resize count=%llu width=%u height=%u layer=%s offscreen=%s",
            (unsigned long long)m_impl->resizeCount, m_impl->width, m_impl->height, m_impl->layer ? "true" : "false",
            m_impl->offscreenTexture ? "true" : "false");
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

    HRESULT QueryInterface(REFIID, void** ppv) override {
        AddRef();
        *ppv = this;
        return S_OK;
    }
    ULONG AddRef() override { return ++refCount; }
    ULONG Release() override {
        ULONG c = --refCount;
        if (c == 0)
            delete this;
        return c;
    }
    HRESULT GetDevice(ID3D11Device** ppDevice) override { return E_NOTIMPL; }
    HRESULT GetPrivateData(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT SetPrivateData(const GUID&, UINT, const void*) override { return E_NOTIMPL; }
    HRESULT SetPrivateDataInterface(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
    HRESULT GetType(UINT* pType) override {
        if (pType)
            *pType = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
        return S_OK;
    }
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
    if (m_d3d11BackBufferTexture) {
        auto* tex = static_cast<DXGISwapChainBackBuffer*>(m_d3d11BackBufferTexture);
        tex->Release();
    }
    if (m_d3d12BackBufferTexture) {
        reinterpret_cast<IUnknown*>(m_d3d12BackBufferTexture)->Release();
    }
}

HRESULT DXGISwapChainImpl::create(MetalDevice* metalDevice, HWND window, uint32_t width, uint32_t height,
                                  uint32_t bufferCount, DXGI_FORMAT format, IDXGISwapChain** ppSwapChain) {
    if (!ppSwapChain || !metalDevice)
        return E_POINTER;

    auto* swapChain = new DXGISwapChainImpl();
    swapChain->m_metalDevice = metalDevice;
    swapChain->m_hwnd = window;
    swapChain->m_width = width;
    swapChain->m_height = height;
    swapChain->m_format = format;
    swapChain->m_bufferCount = bufferCount;

    swapChain->m_metalSwapChain.reset(
        MetalSwapChain::create(*metalDevice, (void*)window, width, height, bufferCount, format));

    if (!swapChain->m_metalSwapChain) {
        delete swapChain;
        return E_FAIL;
    }

    *ppSwapChain = swapChain;
    return S_OK;
}

HRESULT DXGISwapChainImpl::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject)
        return E_POINTER;
    AddRef();
    *ppvObject = this;
    return S_OK;
}

ULONG DXGISwapChainImpl::AddRef() {
    return ++m_refCount;
}
ULONG DXGISwapChainImpl::Release() {
    ULONG c = --m_refCount;
    if (c == 0)
        delete this;
    return c;
}

HRESULT DXGISwapChainImpl::GetDevice(const GUID& riid, void** ppDevice) {
    return E_NOTIMPL;
}
HRESULT DXGISwapChainImpl::GetPrivateData(const GUID&, UINT*, void*) {
    return E_NOTIMPL;
}
HRESULT DXGISwapChainImpl::SetPrivateData(const GUID&, UINT, const void*) {
    return E_NOTIMPL;
}
HRESULT DXGISwapChainImpl::SetPrivateDataInterface(const GUID&, const IUnknown*) {
    return E_NOTIMPL;
}
HRESULT DXGISwapChainImpl::GetParent(const GUID& riid, void** ppParent) {
    return E_NOTIMPL;
}

HRESULT DXGISwapChainImpl::Present(UINT SyncInterval, UINT Flags) {
    if (m_metalSwapChain) {
        m_metalSwapChain->present(SyncInterval);
        ++m_lastPresentCount;
        if (m_d3d11BackBufferTexture) {
            static_cast<DXGISwapChainBackBuffer*>(m_d3d11BackBufferTexture)->Release();
            m_d3d11BackBufferTexture = nullptr;
        }
        if (m_d3d12BackBufferTexture) {
            reinterpret_cast<IUnknown*>(m_d3d12BackBufferTexture)->Release();
            m_d3d12BackBufferTexture = nullptr;
        }
    }
    return S_OK;
}

HRESULT DXGISwapChainImpl::GetBuffer(UINT Buffer, const GUID& riid, void** ppSurface) {
    if (!ppSurface)
        return E_POINTER;
    if (!m_metalSwapChain)
        return E_FAIL;
    *ppSurface = nullptr;

    void* texPtr = m_metalSwapChain->getCurrentTexture();
    if (!texPtr)
        return E_FAIL;

    if (riid == IID_ID3D12ResourceLocal) {
        void* backBuffer = nullptr;
        HRESULT hr = createD3D12SwapChainBackBuffer(texPtr, m_width, m_height, (uint32_t)m_format, &backBuffer);
        if (FAILED(hr))
            return hr;
        if (m_d3d12BackBufferTexture) {
            reinterpret_cast<IUnknown*>(m_d3d12BackBufferTexture)->Release();
        }
        m_d3d12BackBufferTexture = backBuffer;
        reinterpret_cast<IUnknown*>(backBuffer)->AddRef();
        *ppSurface = backBuffer;
        return S_OK;
    }

    auto* backBuffer = new DXGISwapChainBackBuffer();
    backBuffer->metalTexture = texPtr;
    backBuffer->width = m_width;
    backBuffer->height = m_height;
    backBuffer->format = m_format;

    if (m_d3d11BackBufferTexture) {
        static_cast<DXGISwapChainBackBuffer*>(m_d3d11BackBufferTexture)->Release();
    }
    m_d3d11BackBufferTexture = backBuffer;
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
    if (pFullscreen)
        *pFullscreen = FALSE;
    if (ppTarget)
        *ppTarget = nullptr;
    return S_OK;
}

HRESULT DXGISwapChainImpl::GetDesc(void* pDesc) {
    if (!pDesc)
        return E_POINTER;
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

HRESULT DXGISwapChainImpl::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat,
                                         UINT SwapChainFlags) {
    if (BufferCount > 0)
        m_bufferCount = BufferCount;
    if (NewFormat != DXGI_FORMAT_UNKNOWN)
        m_format = NewFormat;

    if (m_metalSwapChain) {
        uint32_t newW = Width > 0 ? Width : m_width;
        uint32_t newH = Height > 0 ? Height : m_height;
        m_metalSwapChain->resize(newW, newH);
        m_width = newW;
        m_height = newH;
    }

    if (m_d3d11BackBufferTexture) {
        static_cast<DXGISwapChainBackBuffer*>(m_d3d11BackBufferTexture)->Release();
        m_d3d11BackBufferTexture = nullptr;
    }
    if (m_d3d12BackBufferTexture) {
        reinterpret_cast<IUnknown*>(m_d3d12BackBufferTexture)->Release();
        m_d3d12BackBufferTexture = nullptr;
    }

    return S_OK;
}

HRESULT DXGISwapChainImpl::ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters) {
    return S_OK;
}
HRESULT DXGISwapChainImpl::GetContainingOutput(IDXGIOutput** ppOutput) {
    return E_NOTIMPL;
}
HRESULT DXGISwapChainImpl::GetFrameStatistics(void* pStats) {
    return E_NOTIMPL;
}
HRESULT DXGISwapChainImpl::GetLastPresentCount(UINT* pLastPresentCount) {
    if (pLastPresentCount)
        *pLastPresentCount = m_lastPresentCount;
    return S_OK;
}

HRESULT DXGISwapChainImpl::Present1(UINT SyncInterval, UINT PresentFlags, const void* pPresentParameters) {
    return Present(SyncInterval, PresentFlags);
}

} // namespace metalsharp
