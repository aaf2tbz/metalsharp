#include <metalsharp/MetalBackend.h>
#include <metalsharp/FormatTranslation.h>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>

namespace metalsharp {

struct MetalTexture::Impl {
    id<MTLTexture> texture = nil;
    uint32_t w = 0;
    uint32_t h = 0;
};

MetalTexture::MetalTexture() : m_impl(new Impl()) {}
MetalTexture::~MetalTexture() { delete m_impl; }

bool MetalTexture::init2D(MetalDevice& device, uint32_t width, uint32_t height, uint32_t format, uint32_t usage) {
    id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)device.nativeDevice();

    MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
    desc.textureType = MTLTextureType2D;
    desc.pixelFormat = (MTLPixelFormat)format;
    desc.width = width;
    desc.height = height;
    desc.mipmapLevelCount = 1;
    desc.sampleCount = 1;

    MTLTextureUsage mtlUsage = 0;
    if (usage & 0x1) mtlUsage |= MTLTextureUsageRenderTarget;
    if (usage & 0x2) mtlUsage |= MTLTextureUsageShaderRead;
    if (usage & 0x4) mtlUsage |= MTLTextureUsageShaderWrite;
    desc.usage = mtlUsage;
    desc.storageMode = MTLStorageModePrivate;

    m_impl->texture = [mtlDevice newTextureWithDescriptor:desc];
    m_impl->w = width;
    m_impl->h = height;
    return m_impl->texture != nil;
}

MetalTexture* MetalTexture::create2D(MetalDevice& device, uint32_t width, uint32_t height, uint32_t format, uint32_t usage) {
    MetalTexture* tex = new MetalTexture();
    if (!tex->init2D(device, width, height, format, usage)) {
        delete tex;
        return nullptr;
    }
    return tex;
}

void* MetalTexture::nativeTexture() const {
    return (__bridge void*)m_impl->texture;
}

uint32_t MetalTexture::width() const { return m_impl->w; }
uint32_t MetalTexture::height() const { return m_impl->h; }

}
