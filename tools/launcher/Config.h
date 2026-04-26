#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace metalsharp {

struct GameProfile {
    std::string name;
    uint32_t width = 1920;
    uint32_t height = 1080;
    bool fullscreen = false;
    bool vsync = true;
    bool debugMetal = false;
    std::string dxgiFormat = "auto";
    std::vector<std::string> dllOverrides;
};

class Config {
public:
    Config();

    bool load(const std::string& path);
    bool save(const std::string& path);
    bool loadGameProfile(const std::string& exeName);

    std::string winePrefix;
    std::string executable;
    std::string workingDir;
    std::string steamUserId;
    std::string steamInstallDir;
    bool verbose = false;
    bool debugMetal = false;
    uint32_t width = 1920;
    uint32_t height = 1080;
    bool fullscreen = false;
    bool vsync = true;

    GameProfile* activeProfile = nullptr;

    static std::string defaultConfigPath();
    static std::string findSteamInstallDir();

private:
    std::unordered_map<std::string, GameProfile> m_profiles;
};

}
