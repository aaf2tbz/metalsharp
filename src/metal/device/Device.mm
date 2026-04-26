#include <metalsharp/MetalBackend.h>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>
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
    if (!m_impl->device) return false;

    m_impl->commandQueue = [m_impl->device newCommandQueue];
    if (!m_impl->commandQueue) return false;

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

}
