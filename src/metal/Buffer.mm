#include <metalsharp/MetalBackend.h>
#include <Foundation/Foundation.h>
#include <Metal/Metal.h>

namespace metalsharp {

struct MetalBuffer::Impl {
    id<MTLBuffer> buffer = nil;
    size_t bufferSize = 0;
};

MetalBuffer::MetalBuffer() : m_impl(new Impl()) {}
MetalBuffer::~MetalBuffer() { delete m_impl; }

bool MetalBuffer::init(MetalDevice& device, size_t size, const void* data) {
    id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)device.nativeDevice();
    if (data) {
        m_impl->buffer = [mtlDevice newBufferWithBytes:data length:size options:MTLResourceStorageModeShared];
    } else {
        m_impl->buffer = [mtlDevice newBufferWithLength:size options:MTLResourceStorageModeShared];
    }
    m_impl->bufferSize = size;
    return m_impl->buffer != nil;
}

MetalBuffer* MetalBuffer::create(MetalDevice& device, size_t size, const void* data) {
    MetalBuffer* buf = new MetalBuffer();
    if (!buf->init(device, size, data)) {
        delete buf;
        return nullptr;
    }
    return buf;
}

void* MetalBuffer::nativeBuffer() const {
    return (__bridge void*)m_impl->buffer;
}

void* MetalBuffer::contents() {
    return m_impl->buffer ? [m_impl->buffer contents] : nullptr;
}

size_t MetalBuffer::size() const {
    return m_impl->bufferSize;
}

}
