#include <metalsharp/PipelineCache.h>
#include <metalsharp/Logger.h>
#include <cstring>

namespace metalsharp {

PipelineCache& PipelineCache::instance() {
    static PipelineCache inst;
    return inst;
}

bool PipelineCache::init(const std::string& cacheDir) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cacheDir = cacheDir;
    m_initialized = true;
    m_hits = 0;
    m_misses = 0;
    MS_INFO("PipelineCache initialized (max %llu entries)", m_maxEntries);
    return true;
}

void PipelineCache::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
    m_initialized = false;
}

void* PipelineCache::lookup(uint64_t hash) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return nullptr;

    auto it = m_entries.find(hash);
    if (it != m_entries.end()) {
        m_hits++;
        return it->second.pipelineState;
    }

    m_misses++;
    return nullptr;
}

void PipelineCache::store(uint64_t hash, void* pipelineState, const std::string& label) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return;

    if (m_entries.size() >= m_maxEntries) {
        uint64_t oldest = m_entries.begin()->first;
        m_entries.erase(oldest);
    }

    PipelineCacheEntry entry;
    entry.hash = hash;
    entry.pipelineState = pipelineState;
    entry.label = label;
    m_entries[hash] = entry;
}

uint64_t PipelineCache::computeDescriptorHash(const void* desc, size_t descSize) {
    const uint8_t* data = static_cast<const uint8_t*>(desc);
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < descSize; i++) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

void PipelineCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
    m_hits = 0;
    m_misses = 0;
}

}
