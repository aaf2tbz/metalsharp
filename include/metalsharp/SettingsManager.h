#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace metalsharp {

enum class UpscalingQuality {
    Off,
    Low,
    Medium,
    High,
    Ultra,
};

enum class WindowMode {
    Windowed,
    Borderless,
    Fullscreen,
};

struct RenderSettings {
    uint32_t renderWidth = 1920;
    uint32_t renderHeight = 1080;
    WindowMode windowMode = WindowMode::Fullscreen;
    UpscalingQuality upscaling = UpscalingQuality::Off;
    bool vsync = true;
    bool metalValidation = false;
    bool metalgpuCapture = false;
    uint32_t shaderCacheMaxMB = 2048;
    bool shaderCacheEnabled = true;
    bool pipelineCacheEnabled = true;
    uint32_t maxFrameRate = 0;
};

struct SettingsState {
    RenderSettings render;
    std::string winePrefix;
    std::string launchMode = "native";
    bool crashReporting = true;
    bool autoCheckUpdates = true;
    std::vector<std::string> dllOverrides;
};

class SettingsManager {
public:
    static SettingsManager& instance();

    bool load(const std::string& path = "");
    bool save(const std::string& path = "");

    SettingsState& state();
    const SettingsState& state() const;

    static std::string defaultSettingsPath();
    static std::string upscalingToString(UpscalingQuality q);
    static UpscalingQuality upscalingFromString(const std::string& s);
    static std::string windowModeToString(WindowMode w);
    static WindowMode windowModeFromString(const std::string& s);

    void clearShaderCache();
    void clearPipelineCache();
    uint64_t shaderCacheSize();

private:
    SettingsManager() = default;
    SettingsState m_state;
    std::string m_path;
};

}
