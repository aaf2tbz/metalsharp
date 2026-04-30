#include "metalsharp/SettingsManager.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>

namespace metalsharp {

namespace fs = std::filesystem;

SettingsManager& SettingsManager::instance() {
    static SettingsManager inst;
    return inst;
}

std::string SettingsManager::defaultSettingsPath() {
    const char* home = std::getenv("HOME");
    if (!home) return "metalsharp.json";
    return std::string(home) + "/.metalsharp/settings.json";
}

std::string SettingsManager::upscalingToString(UpscalingQuality q) {
    switch (q) {
        case UpscalingQuality::Off: return "off";
        case UpscalingQuality::Low: return "low";
        case UpscalingQuality::Medium: return "medium";
        case UpscalingQuality::High: return "high";
        case UpscalingQuality::Ultra: return "ultra";
        default: return "off";
    }
}

UpscalingQuality SettingsManager::upscalingFromString(const std::string& s) {
    if (s == "low") return UpscalingQuality::Low;
    if (s == "medium") return UpscalingQuality::Medium;
    if (s == "high") return UpscalingQuality::High;
    if (s == "ultra") return UpscalingQuality::Ultra;
    return UpscalingQuality::Off;
}

std::string SettingsManager::windowModeToString(WindowMode w) {
    switch (w) {
        case WindowMode::Windowed: return "windowed";
        case WindowMode::Borderless: return "borderless";
        case WindowMode::Fullscreen: return "fullscreen";
        default: return "fullscreen";
    }
}

WindowMode SettingsManager::windowModeFromString(const std::string& s) {
    if (s == "windowed") return WindowMode::Windowed;
    if (s == "borderless") return WindowMode::Borderless;
    return WindowMode::Fullscreen;
}

bool SettingsManager::load(const std::string& path) {
    std::string p = path.empty() ? defaultSettingsPath() : path;
    m_path = p;

    std::ifstream f(p);
    if (!f.is_open()) return false;

    std::ostringstream buf;
    buf << f.rdbuf();
    std::string json = buf.str();

    auto extractValue = [](const std::string& j, const std::string& key) -> std::string {
        std::string search = "\"" + key + "\"";
        auto pos = j.find(search);
        if (pos == std::string::npos) return "";
        auto colon = j.find(':', pos + search.length());
        if (colon == std::string::npos) return "";
        auto openQuote = j.find('"', colon + 1);
        if (openQuote == std::string::npos) return "";
        auto closeQuote = j.find('"', openQuote + 1);
        if (closeQuote == std::string::npos) return "";
        return j.substr(openQuote + 1, closeQuote - openQuote - 1);
    };

    auto extractBool = [&extractValue](const std::string& j, const std::string& key, bool def) -> bool {
        std::string search = "\"" + key + "\"";
        auto pos = j.find(search);
        if (pos == std::string::npos) return def;
        auto colon = j.find(':', pos + search.length());
        if (colon == std::string::npos) return def;
        auto after = j.substr(colon + 1, 10);
        if (after.find("true") != std::string::npos) return true;
        if (after.find("false") != std::string::npos) return false;
        return def;
    };

    auto extractInt = [&extractValue](const std::string& j, const std::string& key, uint32_t def) -> uint32_t {
        std::string search = "\"" + key + "\"";
        auto pos = j.find(search);
        if (pos == std::string::npos) return def;
        auto colon = j.find(':', pos + search.length());
        if (colon == std::string::npos) return def;
        std::string rest = j.substr(colon + 1, 20);
        try {
            size_t idx = 0;
            while (idx < rest.size() && (rest[idx] == ' ' || rest[idx] == '\t')) idx++;
            std::string num;
            while (idx < rest.size() && rest[idx] >= '0' && rest[idx] <= '9') {
                num += rest[idx++];
            }
            if (!num.empty()) return static_cast<uint32_t>(std::stoul(num));
        } catch (...) {}
        return def;
    };

    m_state.render.renderWidth = extractInt(json, "render_width", 1920);
    m_state.render.renderHeight = extractInt(json, "render_height", 1080);
    m_state.render.vsync = extractBool(json, "vsync", true);
    m_state.render.metalValidation = extractBool(json, "metal_validation", false);
    m_state.render.metalgpuCapture = extractBool(json, "metal_gpu_capture", false);
    m_state.render.shaderCacheMaxMB = extractInt(json, "shader_cache_max_mb", 2048);
    m_state.render.shaderCacheEnabled = extractBool(json, "shader_cache_enabled", true);
    m_state.render.pipelineCacheEnabled = extractBool(json, "pipeline_cache_enabled", true);
    m_state.render.maxFrameRate = extractInt(json, "max_frame_rate", 0);

    m_state.render.windowMode = windowModeFromString(extractValue(json, "window_mode"));
    m_state.render.upscaling = upscalingFromString(extractValue(json, "upscaling_quality"));
    m_state.winePrefix = extractValue(json, "wine_prefix");
    m_state.launchMode = extractValue(json, "launch_mode");
    m_state.crashReporting = extractBool(json, "crash_reporting", true);
    m_state.autoCheckUpdates = extractBool(json, "auto_check_updates", true);

    return true;
}

bool SettingsManager::save(const std::string& path) {
    std::string p = path.empty() ? (m_path.empty() ? defaultSettingsPath() : m_path) : path;
    m_path = p;

    fs::create_directories(fs::path(p).parent_path());

    std::ofstream f(p);
    if (!f.is_open()) return false;

    f << "{\n";
    f << "  \"render_width\": " << m_state.render.renderWidth << ",\n";
    f << "  \"render_height\": " << m_state.render.renderHeight << ",\n";
    f << "  \"window_mode\": \"" << windowModeToString(m_state.render.windowMode) << "\",\n";
    f << "  \"upscaling_quality\": \"" << upscalingToString(m_state.render.upscaling) << "\",\n";
    f << "  \"vsync\": " << (m_state.render.vsync ? "true" : "false") << ",\n";
    f << "  \"metal_validation\": " << (m_state.render.metalValidation ? "true" : "false") << ",\n";
    f << "  \"metal_gpu_capture\": " << (m_state.render.metalgpuCapture ? "true" : "false") << ",\n";
    f << "  \"shader_cache_max_mb\": " << m_state.render.shaderCacheMaxMB << ",\n";
    f << "  \"shader_cache_enabled\": " << (m_state.render.shaderCacheEnabled ? "true" : "false") << ",\n";
    f << "  \"pipeline_cache_enabled\": " << (m_state.render.pipelineCacheEnabled ? "true" : "false") << ",\n";
    f << "  \"max_frame_rate\": " << m_state.render.maxFrameRate << ",\n";
    f << "  \"wine_prefix\": \"" << m_state.winePrefix << "\",\n";
    f << "  \"launch_mode\": \"" << m_state.launchMode << "\",\n";
    f << "  \"crash_reporting\": " << (m_state.crashReporting ? "true" : "false") << ",\n";
    f << "  \"auto_check_updates\": " << (m_state.autoCheckUpdates ? "true" : "false") << "\n";
    f << "}\n";

    return true;
}

SettingsState& SettingsManager::state() { return m_state; }
const SettingsState& SettingsManager::state() const { return m_state; }

void SettingsManager::clearShaderCache() {
    const char* home = std::getenv("HOME");
    if (!home) return;
    std::string cacheDir = std::string(home) + "/.metalsharp/cache/shader_cache";
    fs::remove_all(cacheDir);
    fs::create_directories(cacheDir);
}

void SettingsManager::clearPipelineCache() {
    const char* home = std::getenv("HOME");
    if (!home) return;
    std::string cacheDir = std::string(home) + "/.metalsharp/cache/pipeline_cache";
    fs::remove_all(cacheDir);
    fs::create_directories(cacheDir);
}

uint64_t SettingsManager::shaderCacheSize() {
    const char* home = std::getenv("HOME");
    if (!home) return 0;
    std::string cacheDir = std::string(home) + "/.metalsharp/cache/shader_cache";
    if (!fs::exists(cacheDir)) return 0;

    uint64_t total = 0;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(cacheDir,
                     fs::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file()) {
                total += entry.file_size();
            }
        }
    } catch (...) {}
    return total;
}

}
