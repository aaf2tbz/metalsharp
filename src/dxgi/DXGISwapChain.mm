#include <metalsharp/DXGI.h>
#include <AppKit/AppKit.h>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>
#include <QuartzCore/QuartzCore.h>
#include <vector>

namespace metalsharp {

struct MetalSwapChain::Impl {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commandQueue = nil;
    CAMetalLayer* layer = nil;
    id<CAMetalDrawable> currentDrawable = nil;
    std::vector<std::unique_ptr<MetalTexture>> buffers;
    uint32_t currentBuffer = 0;
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
        NSView* view = (__bridge NSView*)window;
        m_impl->layer = [CAMetalLayer layer];
        m_impl->layer.device = m_impl->device;
        m_impl->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        m_impl->layer.framebufferOnly = NO;
        m_impl->layer.frame = view.layer.frame;
        view.wantsLayer = YES;
        view.layer = m_impl->layer;
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

    m_impl->currentDrawable = [m_impl->layer nextDrawable];
    if (!m_impl->currentDrawable) return;

    id<MTLCommandBuffer> cmdBuffer = [m_impl->commandQueue commandBuffer];
    [cmdBuffer presentDrawable:m_impl->currentDrawable];
    [cmdBuffer commit];

    if (syncInterval == 0) {
        [cmdBuffer waitUntilScheduled];
    }
}

void* MetalSwapChain::getCurrentDrawable() {
    if (!m_impl->currentDrawable) {
        m_impl->currentDrawable = [m_impl->layer nextDrawable];
    }
    return (__bridge void*)m_impl->currentDrawable;
}

void* MetalSwapChain::getCurrentTexture() {
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)getCurrentDrawable();
    return (__bridge void*)drawable.texture;
}

void MetalSwapChain::resize(uint32_t width, uint32_t height) {
    m_impl->width = width;
    m_impl->height = height;
    if (m_impl->layer) {
        CGSize size = CGSizeMake(width, height);
        m_impl->layer.drawableSize = size;
    }
    m_impl->buffers.clear();
}

}
