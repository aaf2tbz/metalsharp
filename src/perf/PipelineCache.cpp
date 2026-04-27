#include <metalsharp/PipelineCache.h>
#include <metalsharp/Logger.h>
#include <cstring>
#include <fstream>
#include <sys/stat.h>

namespace metalsharp {

PipelineCache& PipelineCache::instance() {
    static PipelineCache inst;
    return inst;
}

bool PipelineCache::init(const std::string& cacheDir) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cacheDir = cacheDir + "/pipeline_cache";
    mkdir(m_cacheDir.c_str(), 0755);
    m_initialized = true;
    m_hits = 0;
    m_misses = 0;

    loadFromDisk();
    MS_INFO("PipelineCache initialized (max %llu entries, %zu loaded)", m_maxEntries, m_entries.size());
    return true;
}

void PipelineCache::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        saveToDisk();
        m_entries.clear();
    }
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

bool PipelineCache::saveToDisk() {
    if (m_cacheDir.empty()) return false;

    std::string indexPath = m_cacheDir + "/index.bin";
    std::ofstream idx(indexPath, std::ios::binary);
    if (!idx.is_open()) return false;

    uint32_t count = static_cast<uint32_t>(m_entries.size());
    idx.write(reinterpret_cast<const char*>(&count), 4);

    for (auto& [hash, entry] : m_entries) {
        idx.write(reinterpret_cast<const char*>(&hash), 8);
        uint32_t labelLen = static_cast<uint32_t>(entry.label.size());
        idx.write(reinterpret_cast<const char*>(&labelLen), 4);
        if (labelLen > 0) idx.write(entry.label.data(), labelLen);
    }

    MS_INFO("PipelineCache: saved %u entries to %s", count, indexPath.c_str());
    return true;
}

bool PipelineCache::loadFromDisk() {
    if (m_cacheDir.empty()) return false;

    std::string indexPath = m_cacheDir + "/index.bin";
    std::ifstream idx(indexPath, std::ios::binary);
    if (!idx.is_open()) return false;

    uint32_t count = 0;
    idx.read(reinterpret_cast<char*>(&count), 4);
    if (count > m_maxEntries) return false;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t hash = 0;
        idx.read(reinterpret_cast<char*>(&hash), 8);
        uint32_t labelLen = 0;
        idx.read(reinterpret_cast<char*>(&labelLen), 4);
        std::string label;
        if (labelLen > 0 && labelLen < 4096) {
            label.resize(labelLen);
            idx.read(&label[0], labelLen);
        }

        PipelineCacheEntry entry;
        entry.hash = hash;
        entry.pipelineState = nullptr;
        entry.label = label;
        m_entries[hash] = entry;
    }

    MS_INFO("PipelineCache: loaded %u entries from disk", count);
    return true;
}

}
