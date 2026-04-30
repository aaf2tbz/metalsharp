#include <metalsharp/ShaderCache.h>
#include <metalsharp/Logger.h>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>

namespace metalsharp {

static uint64_t fnv1a64(const uint8_t* data, size_t size) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

ShaderCache& ShaderCache::instance() {
    static ShaderCache inst;
    return inst;
}

bool ShaderCache::init(const std::string& cacheDir) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cacheDir = cacheDir + "/shader_cache";
    mkdir(m_cacheDir.c_str(), 0755);
    m_initialized = true;
    m_hits = 0;
    m_misses = 0;

    loadFromDisk();
    MS_INFO("ShaderCache initialized: %zu entries loaded from %s", m_cache.size(), m_cacheDir.c_str());
    return true;
}

void ShaderCache::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        saveToDisk();
        m_cache.clear();
    }
    m_initialized = false;
}

uint64_t ShaderCache::computeHash(const uint8_t* data, size_t size) {
    return fnv1a64(data, size);
}

bool ShaderCache::lookup(uint64_t hash, CachedShader& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return false;

    auto it = m_cache.find(hash);
    if (it != m_cache.end()) {
        out = it->second;
        m_hits++;
        return true;
    }

    m_misses++;
    return false;
}

void ShaderCache::store(uint64_t hash, const CachedShader& shader) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return;

    m_cache[hash] = shader;
    m_cache[hash].hash = hash;
    evictIfNeeded();
}

bool ShaderCache::storeMetallib(uint64_t hash, const uint8_t* data, size_t size, const std::string& entryPoint) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return false;

    auto& entry = m_cache[hash];
    entry.hash = hash;
    entry.metallib.assign(data, data + size);
    entry.entryPoint = entryPoint;
    entry.library = nullptr;
    entry.vertexFunction = nullptr;
    entry.fragmentFunction = nullptr;
    entry.computeFunction = nullptr;

    evictIfNeeded();
    saveMetallibEntry(m_cacheDir + "/" + std::to_string(hash) + ".metallib", hash);
    return true;
}

bool ShaderCache::lookupMetallib(uint64_t hash, std::vector<uint8_t>& outData, std::string& outEntryPoint) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return false;

    auto it = m_cache.find(hash);
    if (it != m_cache.end() && !it->second.metallib.empty()) {
        outData = it->second.metallib;
        outEntryPoint = it->second.entryPoint;
        m_hits++;
        return true;
    }

    m_misses++;
    return false;
}

uint64_t ShaderCache::entryCount() const {
    return m_cache.size();
}

uint64_t ShaderCache::totalCacheSize() const {
    uint64_t total = 0;
    for (const auto& [hash, entry] : m_cache) {
        total += entry.mslSource.size();
        total += entry.metallib.size();
    }
    return total;
}

bool ShaderCache::saveToDisk() {
    if (m_cacheDir.empty()) return false;

    for (auto& [hash, entry] : m_cache) {
        if (!entry.mslSource.empty()) {
            saveEntry(m_cacheDir + "/" + std::to_string(hash) + ".msl", hash, entry);
        }
        if (!entry.metallib.empty()) {
            saveMetallibEntry(m_cacheDir + "/" + std::to_string(hash) + ".metallib", hash);
        }
    }
    return true;
}

bool ShaderCache::loadFromDisk() {
    if (m_cacheDir.empty()) return false;

    DIR* dir = opendir(m_cacheDir.c_str());
    if (!dir) return false;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name.size() < 5) continue;

        std::string ext = name.substr(name.size() - 4);
        auto dot = name.rfind('.');
        if (dot == std::string::npos) continue;

        std::string baseName = name.substr(0, dot);
        uint64_t hash = 0;
        try {
            hash = std::stoull(baseName);
        } catch (...) {
            continue;
        }

        std::string path = m_cacheDir + "/" + name;
        if (ext == ".msl") {
            loadEntry(path, hash);
        } else if (name.size() >= 9 && name.substr(name.size() - 9) == ".metallib") {
            loadMetallibEntry(path, hash);
        }
    }
    closedir(dir);
    return true;
}

bool ShaderCache::loadEntry(const std::string& path, uint64_t hash) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string source((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    if (source.empty()) return false;

    CachedShader entry;
    entry.hash = hash;
    entry.mslSource = source;
    entry.library = nullptr;
    entry.vertexFunction = nullptr;
    entry.fragmentFunction = nullptr;
    entry.computeFunction = nullptr;

    m_cache[hash] = entry;
    return true;
}

bool ShaderCache::saveEntry(const std::string& path, uint64_t hash, const CachedShader& shader) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << shader.mslSource;
    return true;
}

bool ShaderCache::loadMetallibEntry(const std::string& path, uint64_t hash) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    auto fileSize = file.tellg();
    if (fileSize <= 0) return false;
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(data.data()), fileSize)) return false;

    if (!verifyHash(hash, data)) {
        MS_WARN("ShaderCache: metallib hash verification failed for %s", path.c_str());
        return false;
    }

    auto& entry = m_cache[hash];
    entry.hash = hash;
    entry.metallib = std::move(data);
    return true;
}

bool ShaderCache::saveMetallibEntry(const std::string& path, uint64_t hash) {
    auto it = m_cache.find(hash);
    if (it == m_cache.end() || it->second.metallib.empty()) return false;

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char*>(it->second.metallib.data()),
               it->second.metallib.size());
    return true;
}

void ShaderCache::evictIfNeeded() {
    uint64_t totalSize = 0;
    for (const auto& [h, entry] : m_cache) {
        totalSize += entry.mslSource.size() + entry.metallib.size();
    }

    while (totalSize > m_maxCacheSize && m_cache.size() > 1) {
        uint64_t oldest = m_cache.begin()->first;
        auto& entry = m_cache.begin()->second;
        totalSize -= entry.mslSource.size() + entry.metallib.size();
        m_cache.erase(oldest);
    }
}

bool ShaderCache::verifyHash(uint64_t hash, const std::vector<uint8_t>& data) {
    return true;
}

void ShaderCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
    m_hits = 0;
    m_misses = 0;
}

}
