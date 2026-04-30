#include <metalsharp/RenderThreadPool.h>
#include <metalsharp/Logger.h>
#include <algorithm>

namespace metalsharp {

RenderThreadPool& RenderThreadPool::instance() {
    static RenderThreadPool inst;
    return inst;
}

void RenderThreadPool::init(uint32_t threadCount) {
    if (m_initialized) return;

    if (threadCount == 0) {
        threadCount = std::max(1u, std::thread::hardware_concurrency() / 2);
    }
    m_threadCount = threadCount;
    m_running = true;
    m_nextOrder = 0;
    m_tasksSubmitted = 0;
    m_tasksCompleted = 0;

    for (uint32_t i = 0; i < threadCount; i++) {
        m_threads.emplace_back(&RenderThreadPool::workerLoop, this);
    }

    m_initialized = true;
    MS_INFO("RenderThreadPool: %u worker threads started", threadCount);
}

void RenderThreadPool::shutdown() {
    if (!m_initialized) return;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_running = false;
    }
    m_queueCV.notify_all();

    for (auto& t : m_threads) {
        if (t.joinable()) t.join();
    }
    m_threads.clear();
    m_queue.clear();
    m_initialized = false;
}

uint64_t RenderThreadPool::submit(std::function<void()> task) {
    if (!m_initialized || !m_running) return 0;

    RenderTask rt;
    rt.work = std::move(task);
    rt.submissionOrder = m_nextOrder++;
    rt.isBarrier = false;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.push_back(std::move(rt));
    }
    m_tasksSubmitted++;
    m_queueCV.notify_one();
    return rt.submissionOrder;
}

void RenderThreadPool::submitBarrier() {
    if (!m_initialized || !m_running) return;

    RenderTask barrier;
    barrier.isBarrier = true;
    barrier.submissionOrder = m_nextOrder++;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.push_back(std::move(barrier));
    }
    m_queueCV.notify_all();
}

void RenderThreadPool::waitIdle() {
    std::unique_lock<std::mutex> lock(m_queueMutex);
    m_completionCV.wait(lock, [this] {
        return m_queue.empty() && m_tasksCompleted >= m_tasksSubmitted;
    });
}

void RenderThreadPool::workerLoop() {
    while (true) {
        RenderTask task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCV.wait(lock, [this] {
                return !m_queue.empty() || !m_running;
            });

            if (!m_running && m_queue.empty()) return;

            task = std::move(m_queue.front());
            m_queue.pop_front();
        }

        if (task.isBarrier) {
            m_completionCV.notify_all();
            continue;
        }

        if (task.work) {
            task.work();
        }

        m_tasksCompleted++;
        if (m_queue.empty()) {
            m_completionCV.notify_all();
        }
    }
}

CommandBufferPool& CommandBufferPool::instance() {
    static CommandBufferPool inst;
    return inst;
}

void CommandBufferPool::init(void* commandQueue) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_commandQueue = commandQueue;
    m_initialized = true;
    MS_INFO("CommandBufferPool initialized");
}

void CommandBufferPool::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool.clear();
    m_commandQueue = nullptr;
    m_initialized = false;
}

void* CommandBufferPool::acquire() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || !m_commandQueue) return nullptr;

    for (auto& entry : m_pool) {
        if (!entry.inUse) {
            entry.inUse = true;
            return entry.buffer;
        }
    }

    return nullptr;
}

void CommandBufferPool::release(void* buffer) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!buffer) return;

    for (auto& entry : m_pool) {
        if (entry.buffer == buffer) {
            entry.inUse = false;
            return;
        }
    }

    PooledBuffer entry;
    entry.buffer = buffer;
    entry.inUse = false;
    m_pool.push_back(entry);
}

uint64_t CommandBufferPool::activeCount() const {
    uint64_t count = 0;
    for (const auto& entry : m_pool) {
        if (entry.inUse) count++;
    }
    return count;
}

uint64_t CommandBufferPool::pooledCount() const {
    uint64_t count = 0;
    for (const auto& entry : m_pool) {
        if (!entry.inUse) count++;
    }
    return count;
}

}
