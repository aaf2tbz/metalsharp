#pragma once

#include <metalsharp/Platform.h>
#include <cstdint>

namespace metalsharp {

class MetalFXUpscaler {
public:
    static MetalFXUpscaler* create(void* metalDevice);
    ~MetalFXUpscaler();

    bool init(uint32_t inputWidth, uint32_t inputHeight,
              uint32_t outputWidth, uint32_t outputHeight,
              uint32_t format = 87);
    void shutdown();

    bool process(void* inputTexture, void* outputTexture,
                 void* depthTexture, void* motionTexture,
                 float jitterOffsetX, float jitterOffsetY,
                 float motionScaleX, float motionScaleY);

    bool isAvailable() const { return m_available; }
    uint32_t inputWidth() const { return m_inputWidth; }
    uint32_t inputHeight() const { return m_inputHeight; }
    uint32_t outputWidth() const { return m_outputWidth; }
    uint32_t outputHeight() const { return m_outputHeight; }

    float sharpness() const { return m_sharpness; }
    void setSharpness(float s) { m_sharpness = s; }

private:
    MetalFXUpscaler() = default;

    void* m_scaler = nullptr;
    void* m_device = nullptr;
    uint32_t m_inputWidth = 0;
    uint32_t m_inputHeight = 0;
    uint32_t m_outputWidth = 0;
    uint32_t m_outputHeight = 0;
    float m_sharpness = 0.0f;
    bool m_available = false;
    bool m_initialized = false;
};

class MetalFXInterpolator {
public:
    static MetalFXInterpolator* create(void* metalDevice);
    ~MetalFXInterpolator();

    bool init(uint32_t width, uint32_t height, uint32_t format = 87);
    void shutdown();

    bool process(void* outputTexture,
                 void* depthTexture, void* motionTexture,
                 float jitterOffsetX, float jitterOffsetY);

    bool isAvailable() const { return m_available; }

private:
    MetalFXInterpolator() = default;

    void* m_interpolator = nullptr;
    void* m_device = nullptr;
    bool m_available = false;
    bool m_initialized = false;
};

}
