#pragma once

#include <metalsharp/Platform.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <mutex>

namespace metalsharp {

struct CachedShader {
    void* library;
    void* vertexFunction;
    void* fragmentFunction;
    void* computeFunction;
    uint64_t hash;
    std::string mslSource;
};

class ShaderCache {
public:
    static ShaderCache& instance();

    bool init(const std::string& cacheDir);
    void shutdown();

    bool lookup(uint64_t hash, CachedShader& out);
    void store(uint64_t hash, const CachedShader& shader);

    static uint64_t computeHash(const uint8_t* data, size_t size);

    uint64_t hitCount() const { return m_hits; }
    uint64_t missCount() const { return m_misses; }
    uint64_t entryCount() const;
    bool saveToDisk();
    void clear();

private:
    ShaderCache() = default;
    ~ShaderCache() = default;

    bool loadFromDisk();
    bool loadEntry(const std::string& path, uint64_t hash);
    bool saveEntry(const std::string& path, uint64_t hash, const CachedShader& shader);

    std::unordered_map<uint64_t, CachedShader> m_cache;
    std::string m_cacheDir;
    bool m_initialized = false;
    uint64_t m_hits = 0;
    uint64_t m_misses = 0;
    std::mutex m_mutex;
};

}
