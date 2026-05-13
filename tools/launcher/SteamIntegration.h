#pragma once

#include <string>
#include <vector>

namespace metalsharp {

struct SteamGame {
    uint32_t appId;
    std::string name;
    std::string installDir;
    std::string platform;
};

class SteamIntegration {
  public:
    static std::string findSteamInstallDir();
    static std::vector<SteamGame> enumerateLibrary(const std::string& steamDir);
    static std::string findGameExecutable(const std::string& gameDir);

    static std::string defaultDownloadDir();
};

} // namespace metalsharp
