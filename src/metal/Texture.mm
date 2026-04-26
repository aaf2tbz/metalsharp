#include <metalsharp/MetalBackend.h>
#include <metalsharp/FormatTranslation.h>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>

namespace metalsharp {

struct MetalTexture::Impl {
    id<MTLTexture> texture = nil;
    uint32_t w = 0;
    uint32_t h = 0;
    uint32_t mips = 1;
    uint32_t samples = 1;
};

MetalTexture::MetalTexture() : m_impl(new Impl()) {}
MetalTexture::~MetalTexture() { delete m_impl; }

bool MetalTexture::init2D(MetalDevice& device, uint32_t width, uint32_t height, uint32_t format, uint32_t usage, uint32_t mipLevels, uint32_t sampleCount) {
    id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)device.nativeDevice();

    MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
    desc.textureType = sampleCount > 1 ? MTLTextureType2DMultisample : MTLTextureType2D;
    desc.pixelFormat = (MTLPixelFormat)format;
    desc.width = width;
    desc.height = height;
    desc.mipmapLevelCount = mipLevels > 0 ? mipLevels : 1;
    desc.sampleCount = sampleCount > 0 ? sampleCount : 1;

    MTLTextureUsage mtlUsage = 0;
    if (usage & 0x1) mtlUsage |= MTLTextureUsageRenderTarget;
    if (usage & 0x2) mtlUsage |= MTLTextureUsageShaderRead;
    if (usage & 0x4) mtlUsage |= MTLTextureUsageShaderWrite;
    if (usage & 0x8) mtlUsage |= MTLTextureUsagePixelFormatView;
    desc.usage = mtlUsage;
    desc.storageMode = MTLStorageModePrivate;

    m_impl->texture = [mtlDevice newTextureWithDescriptor:desc];
    m_impl->w = width;
    m_impl->h = height;
    m_impl->mips = mipLevels > 0 ? mipLevels : 1;
    m_impl->samples = sampleCount > 0 ? sampleCount : 1;
    return m_impl->texture != nil;
}

MetalTexture* MetalTexture::create2D(MetalDevice& device, uint32_t width, uint32_t height, uint32_t format, uint32_t usage, uint32_t mipLevels, uint32_t sampleCount) {
    MetalTexture* tex = new MetalTexture();
    if (!tex->init2D(device, width, height, format, usage, mipLevels, sampleCount)) {
        delete tex;
        return nullptr;
    }
    return tex;
}

void MetalTexture::uploadData(uint32_t mipLevel, uint32_t slice, const void* data, uint32_t rowPitch, uint32_t w, uint32_t h) {
    if (!m_impl->texture || !data) return;
    MTLRegion region = MTLRegionMake2D(0, 0, w, h);
    [m_impl->texture replaceRegion:region
                        mipmapLevel:mipLevel
                              slice:slice
                          withBytes:data
                        bytesPerRow:rowPitch
                      bytesPerImage:0];
}

void* MetalTexture::nativeTexture() const {
    return (__bridge void*)m_impl->texture;
}

uint32_t MetalTexture::width() const { return m_impl->w; }
uint32_t MetalTexture::height() const { return m_impl->h; }
uint32_t MetalTexture::mipLevels() const { return m_impl->mips; }
uint32_t MetalTexture::sampleCount() const { return m_impl->samples; }

}
