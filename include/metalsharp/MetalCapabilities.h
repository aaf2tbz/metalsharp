/// @file MetalCapabilities.h
/// @brief Runtime Metal feature matrix used by launch diagnostics and render-path selection.

#pragma once

#include <cstdint>
#include <string>

namespace metalsharp {

struct MetalCapabilities {
    std::string deviceName;
    std::string osVersion;
    uint32_t recommendedMaxWorkingSetMB = 0;
    uint32_t maxThreadsPerThreadgroup = 0;
    uint32_t maxArgumentBufferSamplerCount = 0;
    bool supportsAppleGPUFamily4 = false;
    bool supportsAppleGPUFamily5 = false;
    bool supportsAppleGPUFamily6 = false;
    bool supportsAppleGPUFamily7 = false;
    bool supportsAppleGPUFamily8 = false;
    bool supportsMacGPUFamily2 = false;
    bool supportsArgumentBuffers = false;
    bool supportsIndirectCommandBuffers = false;
    bool supportsBinaryArchives = false;
    bool supportsRayTracing = false;
    bool supportsMeshShaders = false;
    bool supportsSparseTextures = false;
    bool supportsFunctionPointers = false;
    bool supportsMetalFX = false;

    std::string summary() const;
};

class MetalCapabilityDetector {
  public:
    static MetalCapabilities detect(void* nativeDevice);
    static void log(const MetalCapabilities& caps, const char* context);
};

} // namespace metalsharp
