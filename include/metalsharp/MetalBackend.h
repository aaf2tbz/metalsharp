#pragma once

#include <metalsharp/Platform.h>

struct MTLDevice;
struct MTLCommandQueue;

namespace metalsharp {

class MetalDevice {
public:
    static MetalDevice* create();

    void* nativeDevice() const;
    void* nativeCommandQueue() const;

    ~MetalDevice();
    MetalDevice(const MetalDevice&) = delete;
    MetalDevice& operator=(const MetalDevice&) = delete;

private:
    MetalDevice();
    bool init();

    struct Impl;
    Impl* m_impl;
};

class MetalCommandBuffer {
public:
    explicit MetalCommandBuffer(MetalDevice& device);
    ~MetalCommandBuffer();

    void beginRenderPass(void* renderPassDescriptor);
    void endRenderPass();
    void commit();
    void present(void* drawable);
    void waitUntilCompleted();

    void* nativeBuffer() const;

    MetalCommandBuffer(const MetalCommandBuffer&) = delete;
    MetalCommandBuffer& operator=(const MetalCommandBuffer&) = delete;

private:
    struct Impl;
    Impl* m_impl;
};

class MetalBuffer {
public:
    static MetalBuffer* create(MetalDevice& device, size_t size, const void* data);
    ~MetalBuffer();

    void* nativeBuffer() const;
    void* contents();
    size_t size() const;

private:
    MetalBuffer();
    bool init(MetalDevice& device, size_t size, const void* data);

    struct Impl;
    Impl* m_impl;
};

class MetalTexture {
public:
    static MetalTexture* create2D(MetalDevice& device, uint32_t width, uint32_t height, uint32_t format, uint32_t usage, uint32_t mipLevels = 1, uint32_t sampleCount = 1);
    ~MetalTexture();

    void* nativeTexture() const;
    uint32_t width() const;
    uint32_t height() const;
    uint32_t mipLevels() const;
    uint32_t sampleCount() const;

    void uploadData(uint32_t mipLevel, uint32_t slice, const void* data, uint32_t rowPitch, uint32_t width, uint32_t height);

private:
    MetalTexture();
    bool init2D(MetalDevice& device, uint32_t width, uint32_t height, uint32_t format, uint32_t usage, uint32_t mipLevels, uint32_t sampleCount);

    struct Impl;
    Impl* m_impl;
};

class MetalSampler {
public:
    static MetalSampler* create(MetalDevice& device,
        uint32_t filter, uint32_t addressU, uint32_t addressV, uint32_t addressW,
        float mipLodBias, uint32_t maxAnisotropy, uint32_t comparisonFunc);
    ~MetalSampler();

    void* nativeSamplerState() const;

private:
    MetalSampler();
    bool init(MetalDevice& device,
        uint32_t filter, uint32_t addressU, uint32_t addressV, uint32_t addressW,
        float mipLodBias, uint32_t maxAnisotropy, uint32_t comparisonFunc);

    struct Impl;
    Impl* m_impl;
};

class MetalFramebuffer {
public:
    MetalFramebuffer();
    ~MetalFramebuffer();

    void setColorAttachment(uint32_t index, MetalTexture* texture);
    void setDepthAttachment(MetalTexture* texture);
    void setStencilAttachment(MetalTexture* texture);

    void* nativeRenderPassDescriptor() const;

private:
    struct Impl;
    Impl* m_impl;
};

}
