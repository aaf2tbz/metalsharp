/// @file RenderThreadPool.h
/// @brief Thread pool for parallel render task execution and command buffer pooling.
///
/// Provides a fixed-size worker thread pool with FIFO task submission, barrier synchronization,
/// and idle-wait semantics for parallelizing independent render work across multiple threads.
/// The companion CommandBufferPool recycles MTLCommandBuffer objects to avoid per-frame
/// allocation overhead. Used by the D3D12 command queue to parallelize command list execution
/// and by the shader compilation service for background compilation tasks.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <metalsharp/Platform.h>
#include <mutex>
#include <thread>
#include <vector>

namespace metalsharp {

struct RenderTask {
    std::function<void()> work;
    uint64_t submissionOrder;
    bool isBarrier = false;
};

class RenderThreadPool {
  public:
    static RenderThreadPool& instance();

    void init(uint32_t threadCount = 0);
    void shutdown();

    uint64_t submit(std::function<void()> task);
    void submitBarrier();
    void waitIdle();

    uint32_t threadCount() const { return m_threadCount; }
    uint64_t tasksSubmitted() const { return m_tasksSubmitted; }
    uint64_t tasksCompleted() const { return m_tasksCompleted; }
    bool isInitialized() const { return m_initialized; }

  private:
    RenderThreadPool() = default;
    void workerLoop();

    std::vector<std::thread> m_threads;
    std::deque<RenderTask> m_queue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCV;
    std::condition_variable m_completionCV;
    std::atomic<bool> m_running{false};
    std::atomic<uint64_t> m_tasksSubmitted{0};
    std::atomic<uint64_t> m_tasksCompleted{0};
    uint64_t m_nextOrder = 0;
    uint32_t m_threadCount = 0;
    bool m_initialized = false;
};

class CommandBufferPool {
  public:
    static CommandBufferPool& instance();

    void init(void* commandQueue);
    void shutdown();

    void* acquire();
    void release(void* buffer);

    uint64_t activeCount() const;
    uint64_t pooledCount() const;

  private:
    CommandBufferPool() = default;

    struct PooledBuffer {
        void* buffer;
        bool inUse;
    };

    std::vector<PooledBuffer> m_pool;
    void* m_commandQueue = nullptr;
    std::mutex m_mutex;
    bool m_initialized = false;
};

} // namespace metalsharp
