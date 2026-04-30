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
    std::vector<uint8_t> metallib;
    std::string entryPoint;
};

class ShaderCache {
public:
    static ShaderCache& instance();

    bool init(const std::string& cacheDir);
    void shutdown();

    bool lookup(uint64_t hash, CachedShader& out);
    void store(uint64_t hash, const CachedShader& shader);
    bool storeMetallib(uint64_t hash, const uint8_t* data, size_t size, const std::string& entryPoint);
    bool lookupMetallib(uint64_t hash, std::vector<uint8_t>& outData, std::string& outEntryPoint);

    static uint64_t computeHash(const uint8_t* data, size_t size);

    uint64_t hitCount() const { return m_hits; }
    uint64_t missCount() const { return m_misses; }
    uint64_t entryCount() const;
    uint64_t totalCacheSize() const;
    bool saveToDisk();
    void clear();

    void setMaxCacheSize(uint64_t bytes) { m_maxCacheSize = bytes; }
    uint64_t maxCacheSize() const { return m_maxCacheSize; }

private:
    ShaderCache() = default;
    ~ShaderCache() = default;

    bool loadFromDisk();
    bool loadEntry(const std::string& path, uint64_t hash);
    bool saveEntry(const std::string& path, uint64_t hash, const CachedShader& shader);
    bool loadMetallibEntry(const std::string& path, uint64_t hash);
    bool saveMetallibEntry(const std::string& path, uint64_t hash);
    void evictIfNeeded();
    bool verifyHash(uint64_t hash, const std::vector<uint8_t>& data);

    std::unordered_map<uint64_t, CachedShader> m_cache;
    std::string m_cacheDir;
    bool m_initialized = false;
    uint64_t m_hits = 0;
    uint64_t m_misses = 0;
    uint64_t m_maxCacheSize = 2ULL * 1024 * 1024 * 1024;
    std::mutex m_mutex;
};

}
