#include <metalsharp/MetalBackend.h>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>
#include <array>

namespace metalsharp {

struct MetalFramebuffer::Impl {
    MTLRenderPassDescriptor* descriptor = nil;
    std::array<id<MTLTexture>, 8> colorAttachments;
    id<MTLTexture> depthAttachment = nil;
    id<MTLTexture> stencilAttachment = nil;
};

MetalFramebuffer::MetalFramebuffer() : m_impl(new Impl()) {
    m_impl->descriptor = [[MTLRenderPassDescriptor alloc] init];
}

MetalFramebuffer::~MetalFramebuffer() { delete m_impl; }

void MetalFramebuffer::setColorAttachment(uint32_t index, MetalTexture* texture) {
    if (index >= 8 || !texture) return;
    id<MTLTexture> mtlTex = (__bridge id<MTLTexture>)texture->nativeTexture();
    m_impl->colorAttachments[index] = mtlTex;
    m_impl->descriptor.colorAttachments[index].texture = mtlTex;
    m_impl->descriptor.colorAttachments[index].loadAction = MTLLoadActionLoad;
    m_impl->descriptor.colorAttachments[index].storeAction = MTLStoreActionStore;
}

void MetalFramebuffer::setDepthAttachment(MetalTexture* texture) {
    if (!texture) return;
    id<MTLTexture> mtlTex = (__bridge id<MTLTexture>)texture->nativeTexture();
    m_impl->depthAttachment = mtlTex;
    m_impl->descriptor.depthAttachment.texture = mtlTex;
    m_impl->descriptor.depthAttachment.loadAction = MTLLoadActionLoad;
    m_impl->descriptor.depthAttachment.storeAction = MTLStoreActionStore;
}

void MetalFramebuffer::setStencilAttachment(MetalTexture* texture) {
    if (!texture) return;
    id<MTLTexture> mtlTex = (__bridge id<MTLTexture>)texture->nativeTexture();
    m_impl->stencilAttachment = mtlTex;
    m_impl->descriptor.stencilAttachment.texture = mtlTex;
    m_impl->descriptor.stencilAttachment.loadAction = MTLLoadActionLoad;
    m_impl->descriptor.stencilAttachment.storeAction = MTLStoreActionStore;
}

void* MetalFramebuffer::nativeRenderPassDescriptor() const {
    return (__bridge void*)m_impl->descriptor;
}

}
