/// @file Device.mm
/// @brief Metal device creation — MTLDevice initialization and capability queries.
///
/// Objective-C++ implementation that wraps MTLDevice creation, queries GPU
/// capabilities, and exposes the Metal device to the rest of the translation layer.

#include <Foundation/Foundation.h>
#include <Metal/Metal.h>
#include <metalsharp/MetalBackend.h>
#include <QuartzCore/QuartzCore.h>

namespace metalsharp {

struct MetalDevice::Impl {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commandQueue = nil;
};

MetalDevice::MetalDevice() : m_impl(new Impl()) {}

MetalDevice::~MetalDevice() {
    delete m_impl;
}

bool MetalDevice::init() {
    m_impl->device = MTLCreateSystemDefaultDevice();
    if (!m_impl->device)
        return false;

    m_impl->commandQueue = [m_impl->device newCommandQueue];
    if (!m_impl->commandQueue)
        return false;

    return true;
}

MetalDevice* MetalDevice::create() {
    MetalDevice* dev = new MetalDevice();
    if (!dev->init()) {
        delete dev;
        return nullptr;
    }
    return dev;
}

void* MetalDevice::nativeDevice() const {
    return (__bridge void*)m_impl->device;
}

void* MetalDevice::nativeCommandQueue() const {
    return (__bridge void*)m_impl->commandQueue;
}

} // namespace metalsharp
