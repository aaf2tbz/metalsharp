#include <metalsharp/CommandBatcher.h>
#include <metalsharp/Logger.h>

namespace metalsharp {

CommandBatcher& CommandBatcher::instance() {
    static CommandBatcher inst;
    return inst;
}

void CommandBatcher::init() {
    m_batch.reserve(m_maxBatchSize);
    m_initialized = true;
    MS_INFO("CommandBatcher initialized (max batch: %u)", m_maxBatchSize);
}

void CommandBatcher::shutdown() {
    flush();
    m_initialized = false;
}

void CommandBatcher::beginFrame() {
    m_batch.clear();
}

void CommandBatcher::endFrame() {
    flush();
}

void CommandBatcher::enqueue(const BatchedCommand& cmd) {
    if (!m_initialized) return;

    m_batch.push_back(cmd);
    m_totalBatches++;

    if (m_batch.size() >= m_maxBatchSize) {
        flush();
    }
}

void CommandBatcher::flush() {
    if (m_batch.empty()) return;
    if (!m_execute) {
        m_batch.clear();
        return;
    }

    for (const auto& cmd : m_batch) {
        m_execute(cmd);
    }

    m_totalFlushes++;
    m_batch.clear();
}

void CommandBatcher::setExecuteFunction(ExecuteFn fn) {
    m_execute = fn;
}

}
