#include "Config.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <sys/stat.h>

namespace metalsharp {

Config::Config() {
    const char* home = getenv("HOME");
    if (home) {
        winePrefix = std::string(home) + "/.metalsharp/prefix";
    }
}

std::string Config::defaultConfigPath() {
    const char* home = getenv("HOME");
    if (home) return std::string(home) + "/.metalsharp/metalsharp.toml";
    return "/tmp/metalsharp/metalsharp.toml";
}

std::string Config::findSteamInstallDir() {
    const char* home = getenv("HOME");
    if (!home) return "";

    const char* candidates[] = {
        "/Applications/Steam.app/Contents/MacOS",
        ".steam/steam",
        ".local/share/Steam",
        "Library/Application Support/Steam"
    };

    for (const auto* rel : candidates) {
        std::string path = std::string(home) + "/" + rel;
        struct stat st{};
        if (stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFDIR)) {
            return path;
        }
    }
    return "";
}

bool Config::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    std::string currentSection;
    GameProfile* currentProfile = nullptr;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        if (line[0] == '[') {
            auto end = line.find(']');
            if (end == std::string::npos) continue;
            currentSection = line.substr(1, end - 1);

            if (currentSection != "general" && currentSection != "steam") {
                auto& profile = m_profiles[currentSection];
                profile.name = currentSection;
                currentProfile = &profile;
            } else {
                currentProfile = nullptr;
            }
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!val.empty() && val.front() == ' ') val = val.substr(1);

        if (currentSection == "general") {
            if (key == "width") width = (uint32_t)std::stoul(val);
            else if (key == "height") height = (uint32_t)std::stoul(val);
            else if (key == "fullscreen") fullscreen = (val == "true");
            else if (key == "vsync") vsync = (val == "true");
            else if (key == "debug_metal") debugMetal = (val == "true");
            else if (key == "wine_prefix") winePrefix = val;
        } else if (currentSection == "steam") {
            if (key == "user_id") steamUserId = val;
            else if (key == "install_dir") steamInstallDir = val;
        } else if (currentProfile) {
            if (key == "width") currentProfile->width = (uint32_t)std::stoul(val);
            else if (key == "height") currentProfile->height = (uint32_t)std::stoul(val);
            else if (key == "fullscreen") currentProfile->fullscreen = (val == "true");
            else if (key == "vsync") currentProfile->vsync = (val == "true");
            else if (key == "debug_metal") currentProfile->debugMetal = (val == "true");
            else if (key == "dxgi_format") currentProfile->dxgiFormat = val;
        }
    }

    return true;
}

bool Config::save(const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "[general]\n";
    file << "width = " << width << "\n";
    file << "height = " << height << "\n";
    file << "fullscreen = " << (fullscreen ? "true" : "false") << "\n";
    file << "vsync = " << (vsync ? "true" : "false") << "\n";
    file << "debug_metal = " << (debugMetal ? "true" : "false") << "\n";
    file << "wine_prefix = \"" << winePrefix << "\"\n";
    file << "\n[steam]\n";
    if (!steamUserId.empty()) file << "user_id = \"" << steamUserId << "\"\n";
    if (!steamInstallDir.empty()) file << "install_dir = \"" << steamInstallDir << "\"\n";

    for (auto& [name, profile] : m_profiles) {
        file << "\n[" << name << "]\n";
        file << "width = " << profile.width << "\n";
        file << "height = " << profile.height << "\n";
        file << "fullscreen = " << (profile.fullscreen ? "true" : "false") << "\n";
        file << "vsync = " << (profile.vsync ? "true" : "false") << "\n";
        file << "debug_metal = " << (profile.debugMetal ? "true" : "false") << "\n";
        if (profile.dxgiFormat != "auto") file << "dxgi_format = \"" << profile.dxgiFormat << "\"\n";
    }

    return true;
}

bool Config::loadGameProfile(const std::string& exeName) {
    auto it = m_profiles.find(exeName);
    if (it != m_profiles.end()) {
        activeProfile = &it->second;
        return true;
    }
    return false;
}

}
