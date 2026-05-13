/// @file BufferPool.h
/// @brief MTLBuffer allocation pool with size-bucketed recycling.
///
/// Reduces GPU memory allocation overhead by pooling released MTLBuffer objects and
/// reusing them for subsequent allocations of similar size. Tracks allocation and reuse
/// statistics, respects a configurable maximum pool size (default 256MB), and is thread-safe.
/// Used by the D3D11/D3D12 resource creation path for vertex buffers, index buffers,
/// constant buffers, and staging resources.

#pragma once

#include <cstdint>
#include <metalsharp/Platform.h>
#include <mutex>
#include <vector>

namespace metalsharp {

struct BufferPoolEntry {
    void* buffer;
    size_t size;
    bool inUse;
};

class BufferPool {
  public:
    static BufferPool& instance();

    void init(void* metalDevice);
    void shutdown();

    void* allocate(size_t size);
    void release(void* buffer);

    uint64_t totalAllocated() const { return m_totalAllocated; }
    uint64_t totalReused() const { return m_totalReused; }
    uint64_t activeBuffers() const;
    uint64_t pooledBuffers() const;

    void setMaxPoolSize(uint64_t maxBytes) { m_maxPoolSize = maxBytes; }

  private:
    BufferPool() = default;

    void* createBuffer(size_t size);
    void* findFree(size_t minSize, size_t maxSize);

    std::vector<BufferPoolEntry> m_entries;
    void* m_device = nullptr;
    bool m_initialized = false;
    uint64_t m_totalAllocated = 0;
    uint64_t m_totalReused = 0;
    uint64_t m_maxPoolSize = 256 * 1024 * 1024;
    std::mutex m_mutex;
};

} // namespace metalsharp
