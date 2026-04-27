#pragma once

#include <metalsharp/Platform.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <mutex>

namespace metalsharp {

struct PipelineCacheEntry {
    uint64_t hash;
    void* pipelineState;
    std::string label;
};

class PipelineCache {
public:
    static PipelineCache& instance();

    bool init(const std::string& cacheDir);
    void shutdown();

    void* lookup(uint64_t hash);
    void store(uint64_t hash, void* pipelineState, const std::string& label = "");

    static uint64_t computeDescriptorHash(const void* desc, size_t descSize);

    uint64_t hitCount() const { return m_hits; }
    uint64_t missCount() const { return m_misses; }
    uint64_t entryCount() const { return m_entries.size(); }
    void clear();

    void setMaxEntries(uint64_t max) { m_maxEntries = max; }

private:
    PipelineCache() = default;

    std::unordered_map<uint64_t, PipelineCacheEntry> m_entries;
    std::string m_cacheDir;
    bool m_initialized = false;
    uint64_t m_hits = 0;
    uint64_t m_misses = 0;
    uint64_t m_maxEntries = 4096;
    std::mutex m_mutex;
};

}
