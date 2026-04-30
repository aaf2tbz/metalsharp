#pragma once

#include <metalsharp/Platform.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <deque>
#include <cstdint>
#include <mutex>
#include <chrono>

namespace metalsharp {

struct PipelineCacheEntry {
    uint64_t hash;
    void* pipelineState;
    std::string label;
    std::vector<uint8_t> serializedDescriptor;
    std::chrono::steady_clock::time_point lastAccess;
};

class PipelineCache {
public:
    static PipelineCache& instance();

    bool init(const std::string& cacheDir);
    void shutdown();

    void* lookup(uint64_t hash);
    void store(uint64_t hash, void* pipelineState, const std::string& label = "");
    void storeDescriptor(uint64_t hash, const void* desc, size_t descSize, const std::string& label = "");
    bool getDescriptor(uint64_t hash, std::vector<uint8_t>& outDesc, std::string& outLabel);

    static uint64_t computeDescriptorHash(const void* desc, size_t descSize);

    uint64_t hitCount() const { return m_hits; }
    uint64_t missCount() const { return m_misses; }
    uint64_t entryCount() const { return m_entries.size(); }
    void clear();

    void setMaxEntries(uint64_t max) { m_maxEntries = max; }

private:
    PipelineCache() = default;

    bool loadFromDisk();
    bool saveToDisk();
    void touchEntry(uint64_t hash);
    void evictIfNeeded();

    std::unordered_map<uint64_t, PipelineCacheEntry> m_entries;
    std::deque<uint64_t> m_lruOrder;
    std::string m_cacheDir;
    bool m_initialized = false;
    uint64_t m_hits = 0;
    uint64_t m_misses = 0;
    uint64_t m_maxEntries = 4096;
    std::mutex m_mutex;
};

}
