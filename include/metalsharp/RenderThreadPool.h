#pragma once

#include <metalsharp/Platform.h>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <cstdint>

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

}
