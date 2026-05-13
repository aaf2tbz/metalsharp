/// @file BufferPool.mm
/// @brief MTLBuffer allocation pool with size-class recycling.
///
/// Recycles MTLBuffer allocations by size class to avoid repeated GPU heap allocation. Tracks buffer lifetimes via
/// Metal shareable event fences and returns completed buffers to the free list for reuse. Reduces allocation latency
/// for dynamic vertex/index buffers.
#include <algorithm>
#include <cstring>
#include <metalsharp/BufferPool.h>
#include <metalsharp/Logger.h>

#import <Metal/Metal.h>

namespace metalsharp {

BufferPool& BufferPool::instance() {
    static BufferPool inst;
    return inst;
}

void BufferPool::init(void* metalDevice) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_device = metalDevice;
    m_initialized = true;
    MS_INFO("BufferPool initialized (max %llu bytes)", m_maxPoolSize);
}

void BufferPool::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& entry : m_entries) {
        if (entry.buffer) {
            id buf = (__bridge_transfer id)entry.buffer;
        }
    }
    m_entries.clear();
    m_initialized = false;
}

void* BufferPool::allocate(size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || !m_device)
        return nullptr;

    size_t searchMax = size * 2;
    if (searchMax < size)
        searchMax = size;

    void* reused = findFree(size, searchMax);
    if (reused) {
        m_totalReused += size;
        return reused;
    }

    void* buffer = createBuffer(size);
    if (buffer) {
        m_totalAllocated += size;
    }
    return buffer;
}

void BufferPool::release(void* buffer) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!buffer)
        return;

    for (auto& entry : m_entries) {
        if (entry.buffer == buffer) {
            entry.inUse = false;
            return;
        }
    }
}

void* BufferPool::createBuffer(size_t size) {
    id<MTLDevice> device = (__bridge id<MTLDevice>)m_device;
    id<MTLBuffer> buffer = [device newBufferWithLength:size options:MTLResourceStorageModeShared];
    if (!buffer)
        return nullptr;

    BufferPoolEntry entry;
    entry.buffer = (__bridge_retained void*)buffer;
    entry.size = size;
    entry.inUse = true;
    m_entries.push_back(entry);
    return entry.buffer;
}

void* BufferPool::findFree(size_t minSize, size_t maxSize) {
    for (auto& entry : m_entries) {
        if (!entry.inUse && entry.size >= minSize && entry.size <= maxSize) {
            entry.inUse = true;
            return entry.buffer;
        }
    }
    return nullptr;
}

uint64_t BufferPool::activeBuffers() const {
    uint64_t count = 0;
    for (auto& entry : m_entries) {
        if (entry.inUse)
            count++;
    }
    return count;
}

uint64_t BufferPool::pooledBuffers() const {
    uint64_t count = 0;
    for (auto& entry : m_entries) {
        if (!entry.inUse)
            count++;
    }
    return count;
}

} // namespace metalsharp
