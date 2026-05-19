/// @file MetalCapabilities.mm
/// @brief Runtime Metal feature detection.

#include <dlfcn.h>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>
#include <metalsharp/Logger.h>
#include <metalsharp/MetalCapabilities.h>
#include <sstream>

namespace metalsharp {

static bool deviceSupportsFamily(id<MTLDevice> device, MTLGPUFamily family) {
    if (!device)
        return false;
    if (@available(macOS 10.15, *)) {
        return [device supportsFamily:family];
    }
    return false;
}

std::string MetalCapabilities::summary() const {
    std::ostringstream out;
    out << "device=" << deviceName << " os=" << osVersion << " appleFamilies=";
    bool wroteFamily = false;
    auto writeFamily = [&](const char* name, bool enabled) {
        if (!enabled)
            return;
        if (wroteFamily)
            out << ",";
        out << name;
        wroteFamily = true;
    };
    writeFamily("Apple4", supportsAppleGPUFamily4);
    writeFamily("Apple5", supportsAppleGPUFamily5);
    writeFamily("Apple6", supportsAppleGPUFamily6);
    writeFamily("Apple7", supportsAppleGPUFamily7);
    writeFamily("Apple8", supportsAppleGPUFamily8);
    if (!wroteFamily)
        out << "none";
    out << " mac2=" << (supportsMacGPUFamily2 ? "yes" : "no")
        << " argumentBuffers=" << (supportsArgumentBuffers ? "yes" : "no")
        << " icb=" << (supportsIndirectCommandBuffers ? "yes" : "no")
        << " binaryArchives=" << (supportsBinaryArchives ? "yes" : "no")
        << " rayTracing=" << (supportsRayTracing ? "yes" : "no")
        << " meshShaders=" << (supportsMeshShaders ? "yes" : "no")
        << " sparseTextures=" << (supportsSparseTextures ? "yes" : "no")
        << " functionPointers=" << (supportsFunctionPointers ? "yes" : "no")
        << " metalFX=" << (supportsMetalFX ? "yes" : "no");
    return out.str();
}

MetalCapabilities MetalCapabilityDetector::detect(void* nativeDevice) {
    MetalCapabilities caps;
    id<MTLDevice> device = (__bridge id<MTLDevice>)nativeDevice;
    if (!device)
        return caps;

    caps.deviceName = [[device name] UTF8String] ? [[device name] UTF8String] : "unknown";
    NSOperatingSystemVersion version = [[NSProcessInfo processInfo] operatingSystemVersion];
    caps.osVersion = std::to_string(version.majorVersion) + "." + std::to_string(version.minorVersion) + "." +
                     std::to_string(version.patchVersion);
    caps.recommendedMaxWorkingSetMB =
        static_cast<uint32_t>([device recommendedMaxWorkingSetSize] / (1024ull * 1024ull));
    MTLSize maxThreads = [device maxThreadsPerThreadgroup];
    caps.maxThreadsPerThreadgroup = static_cast<uint32_t>(maxThreads.width * maxThreads.height * maxThreads.depth);

    if (@available(macOS 10.15, *)) {
        caps.supportsAppleGPUFamily4 = deviceSupportsFamily(device, MTLGPUFamilyApple4);
        caps.supportsAppleGPUFamily5 = deviceSupportsFamily(device, MTLGPUFamilyApple5);
        caps.supportsAppleGPUFamily6 = deviceSupportsFamily(device, MTLGPUFamilyApple6);
        caps.supportsMacGPUFamily2 = deviceSupportsFamily(device, MTLGPUFamilyMac2);
    }
    if (@available(macOS 12.0, *)) {
        caps.supportsAppleGPUFamily7 = deviceSupportsFamily(device, MTLGPUFamilyApple7);
    }
    if (@available(macOS 13.0, *)) {
        caps.supportsAppleGPUFamily8 = deviceSupportsFamily(device, MTLGPUFamilyApple8);
    }

    caps.supportsArgumentBuffers = [device argumentBuffersSupport] != MTLArgumentBuffersTier1 ||
                                   caps.supportsMacGPUFamily2 || caps.supportsAppleGPUFamily4 ||
                                   caps.supportsAppleGPUFamily5 || caps.supportsAppleGPUFamily6 ||
                                   caps.supportsAppleGPUFamily7 || caps.supportsAppleGPUFamily8;
    caps.maxArgumentBufferSamplerCount = [device argumentBuffersSupport] == MTLArgumentBuffersTier2 ? 1024u : 16u;
    caps.supportsIndirectCommandBuffers = caps.supportsAppleGPUFamily4 || caps.supportsAppleGPUFamily5 ||
                                          caps.supportsAppleGPUFamily6 || caps.supportsAppleGPUFamily7 ||
                                          caps.supportsAppleGPUFamily8 || caps.supportsMacGPUFamily2;
    if (@available(macOS 11.0, *)) {
        caps.supportsBinaryArchives = true;
        caps.supportsFunctionPointers = caps.supportsAppleGPUFamily6 || caps.supportsAppleGPUFamily7 ||
                                        caps.supportsAppleGPUFamily8 || caps.supportsMacGPUFamily2;
    }
    if (@available(macOS 13.0, *)) {
        caps.supportsMeshShaders = caps.supportsAppleGPUFamily8;
    }
    if (@available(macOS 14.0, *)) {
        caps.supportsRayTracing = caps.supportsAppleGPUFamily8 || caps.supportsMacGPUFamily2;
    }
    caps.supportsSparseTextures = caps.supportsAppleGPUFamily6 || caps.supportsAppleGPUFamily7 ||
                                  caps.supportsAppleGPUFamily8 || caps.supportsMacGPUFamily2;
    caps.supportsMetalFX = dlopen("/System/Library/Frameworks/MetalFX.framework/MetalFX", RTLD_LAZY) != nullptr;
    return caps;
}

void MetalCapabilityDetector::log(const MetalCapabilities& caps, const char* context) {
    MS_INFO("MetalCapabilities[%s]: %s", context ? context : "runtime", caps.summary().c_str());
}

} // namespace metalsharp
