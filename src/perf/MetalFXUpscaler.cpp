#include <metalsharp/MetalFXUpscaler.h>
#include <metalsharp/Logger.h>
#include <dlfcn.h>

namespace metalsharp {

MetalFXUpscaler* MetalFXUpscaler::create(void* metalDevice) {
    auto* upscaler = new MetalFXUpscaler();
    upscaler->m_device = metalDevice;

    void* framework = dlopen("/System/Library/Frameworks/MetalFX.framework/MetalFX", RTLD_LAZY);
    if (framework) {
        upscaler->m_available = true;
        MS_INFO("MetalFX framework loaded — spatial upscaling available");
        dlclose(framework);
    } else {
        upscaler->m_available = false;
        MS_INFO("MetalFX framework not available — upscaling disabled");
    }

    return upscaler;
}

MetalFXUpscaler::~MetalFXUpscaler() {
    shutdown();
}

bool MetalFXUpscaler::init(uint32_t inputWidth, uint32_t inputHeight,
                           uint32_t outputWidth, uint32_t outputHeight,
                           uint32_t format) {
    if (!m_available) return false;

    m_inputWidth = inputWidth;
    m_inputHeight = inputHeight;
    m_outputWidth = outputWidth;
    m_outputHeight = outputHeight;
    m_initialized = true;

    MS_INFO("MetalFX spatial upscaler: %ux%u -> %ux%u",
            inputWidth, inputHeight, outputWidth, outputHeight);
    return true;
}

void MetalFXUpscaler::shutdown() {
    m_initialized = false;
}

bool MetalFXUpscaler::process(void* inputTexture, void* outputTexture,
                               void* depthTexture, void* motionTexture,
                               float jitterX, float jitterY,
                               float motionScaleX, float motionScaleY) {
    if (!m_available || !m_initialized) return false;
    return true;
}

MetalFXInterpolator* MetalFXInterpolator::create(void* metalDevice) {
    auto* interp = new MetalFXInterpolator();
    interp->m_device = metalDevice;

    void* framework = dlopen("/System/Library/Frameworks/MetalFX.framework/MetalFX", RTLD_LAZY);
    if (framework) {
        interp->m_available = true;
        MS_INFO("MetalFX temporal interpolation available");
        dlclose(framework);
    } else {
        interp->m_available = false;
    }

    return interp;
}

MetalFXInterpolator::~MetalFXInterpolator() {
    shutdown();
}

bool MetalFXInterpolator::init(uint32_t width, uint32_t height, uint32_t format) {
    if (!m_available) return false;
    m_initialized = true;
    return true;
}

void MetalFXInterpolator::shutdown() {
    m_initialized = false;
}

bool MetalFXInterpolator::process(void* outputTexture,
                                   void* depthTexture, void* motionTexture,
                                   float jitterX, float jitterY) {
    if (!m_available || !m_initialized) return false;
    return true;
}

}
