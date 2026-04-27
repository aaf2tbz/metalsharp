#include <metalsharp/ShaderCache.h>
#include <metalsharp/Logger.h>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <functional>

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
}

uint64_t ShaderCache::entryCount() const {
    return m_cache.size();
}

bool ShaderCache::saveToDisk() {
    if (m_cacheDir.empty()) return false;

    for (auto& [hash, entry] : m_cache) {
        if (entry.mslSource.empty()) continue;
        std::string path = m_cacheDir + "/" + std::to_string(hash) + ".msl";
        saveEntry(path, hash, entry);
    }
    return true;
}

bool ShaderCache::loadFromDisk() {
    if (m_cacheDir.empty()) return false;

    std::string cmd = "ls \"" + m_cacheDir + "/\"*.msl 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return false;

    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::string path = buffer;
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
            path.pop_back();

        std::string filename = path;
        auto slash = filename.rfind('/');
        if (slash != std::string::npos) filename = filename.substr(slash + 1);
        auto dot = filename.rfind('.');
        if (dot != std::string::npos) filename = filename.substr(0, dot);

        uint64_t hash = std::stoull(filename);
        loadEntry(path, hash);
    }
    pclose(pipe);
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

void ShaderCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
    m_hits = 0;
    m_misses = 0;
}

}
