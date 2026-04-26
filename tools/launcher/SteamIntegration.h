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
    static std::string findSteamCMD();
    static std::string findSteamInstallDir();
    static std::vector<SteamGame> enumerateLibrary(const std::string& steamDir);
    static bool downloadDepot(const std::string& steamcmd, uint32_t appId, const std::string& depotId, const std::string& outputDir);
    static bool downloadWindowsGame(const std::string& steamcmd, uint32_t appId, const std::string& outputDir);
    static std::string findGameExecutable(const std::string& gameDir);
    static bool authenticate(const std::string& steamcmd);

    static std::string defaultDownloadDir();
};

}
