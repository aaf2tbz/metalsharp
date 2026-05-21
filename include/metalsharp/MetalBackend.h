/// @file MetalBackend.h
/// @brief Metal resource wrappers — the bridge between D3D objects and Metal.
///
/// D3D → Metal Resource Mapping
/// =============================
///
///   D3D11/D3D12 Resource    → Metal Object          → Wrapper Class
///   ──────────────────────────────────────────────────────────────
///   ID3D11Device / Device   → MTLDevice              → MetalDevice
///   DeviceContext / CmdList → MTLCommandBuffer        → MetalCommandBuffer
///   ID3D11Buffer / Resource → MTLBuffer               → MetalBuffer
///   ID3D11Texture2D / Res   → MTLTexture              → MetalTexture (1D/2D/3D)
///   ID3D11SamplerState      → MTLSamplerState          → MetalSampler
///   RenderTarget+Depth      → MTLRenderPassDescriptor  → MetalFramebuffer
///
/// Design Principles
/// =================
///
///   - C++ RAII wrappers around Objective-C Metal objects
///   - .cpp files for portable logic, .mm files for Metal API calls only
///   - MetalDevice owns the MTLDevice and MTLCommandQueue singleton
///   - All objects are reference-counted via shared_ptr in the D3D shim layer
///   - MetalBuffer supports both shared (CPU+GPU) and private (GPU-only) storage
///   - MetalTexture handles 1D, 2D, and 3D textures with mip level supports
///   - MetalSampler converts D3D11_SAMPLER_DESC filter/address fields to Metal equivalents
///   - MetalFramebuffer assembles MTLRenderPassDescriptor from color+depth+stencil attachments
///
///   Implementation files are split by type:
///     src/metal/device/Device.mm — MTLDevice creation
///     src/metal/Buffer.mm       — MTLBuffer creation + data upload
///     src/metal/Texture.mm      — MTLTexture 1D/2D/3D creation
///     src/metal/Sampler.mm      — MTLSamplerState from D3D sampler desc
///     src/metal/Framebuffer.mm  — MTLRenderPassDescriptor assembly

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
    static MetalTexture* create1D(MetalDevice& device, uint32_t width, uint32_t format, uint32_t usage,
                                  uint32_t mipLevels = 1);
    static MetalTexture* create2D(MetalDevice& device, uint32_t width, uint32_t height, uint32_t format, uint32_t usage,
                                  uint32_t mipLevels = 1, uint32_t sampleCount = 1);
    static MetalTexture* create3D(MetalDevice& device, uint32_t width, uint32_t height, uint32_t depth, uint32_t format,
                                  uint32_t usage, uint32_t mipLevels = 1);
    ~MetalTexture();

    void* nativeTexture() const;
    uint32_t width() const;
    uint32_t height() const;
    uint32_t depth() const;
    uint32_t mipLevels() const;
    uint32_t sampleCount() const;

    void uploadData(uint32_t mipLevel, uint32_t slice, const void* data, uint32_t rowPitch, uint32_t width,
                    uint32_t height, uint32_t depth = 1);

  private:
    MetalTexture();
    bool init1D(MetalDevice& device, uint32_t width, uint32_t format, uint32_t usage, uint32_t mipLevels);
    bool init2D(MetalDevice& device, uint32_t width, uint32_t height, uint32_t format, uint32_t usage,
                uint32_t mipLevels, uint32_t sampleCount);
    bool init3D(MetalDevice& device, uint32_t width, uint32_t height, uint32_t depth, uint32_t format, uint32_t usage,
                uint32_t mipLevels);

    struct Impl;
    Impl* m_impl;
};

class MetalSampler {
  public:
    static MetalSampler* create(MetalDevice& device, uint32_t filter, uint32_t addressU, uint32_t addressV,
                                uint32_t addressW, float mipLodBias, uint32_t maxAnisotropy, uint32_t comparisonFunc);
    ~MetalSampler();

    void* nativeSamplerState() const;

  private:
    MetalSampler();
    bool init(MetalDevice& device, uint32_t filter, uint32_t addressU, uint32_t addressV, uint32_t addressW,
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

} // namespace metalsharp
