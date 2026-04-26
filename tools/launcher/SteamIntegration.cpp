#include "SteamIntegration.h"
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>

namespace metalsharp {

std::string SteamIntegration::findSteamCMD() {
    const char* home = getenv("HOME");
    if (!home) return "";

    const char* candidates[] = {
        "/usr/local/bin/steamcmd",
        ".steam/steamcmd/steamcmd.sh",
        "steamcmd/steamcmd.sh",
    };

    for (const auto* rel : candidates) {
        std::string path = (rel[0] == '/') ? rel : std::string(home) + "/" + rel;
        struct stat st{};
        if (stat(path.c_str(), &st) == 0) return path;
    }

    return "";
}

std::string SteamIntegration::findSteamInstallDir() {
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
        if (stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFDIR)) return path;
    }
    return "";
}

std::vector<SteamGame> SteamIntegration::enumerateLibrary(const std::string& steamDir) {
    std::vector<SteamGame> games;

    std::string manifestDir = steamDir + "/steamapps";
    for (int i = 0; i < 100000; ++i) {
        std::string manifest = manifestDir + "/appmanifest_" + std::to_string(i) + ".acf";
        std::ifstream file(manifest);
        if (!file.is_open()) continue;

        SteamGame game;
        game.appId = i;

        std::string line;
        while (std::getline(file, line)) {
            auto quotes = line.find('"');
            if (quotes == std::string::npos) continue;

            if (line.find("\"name\"") != std::string::npos) {
                auto first = line.find('"', quotes + 1);
                auto second = line.find('"', first + 1);
                if (first != std::string::npos && second != std::string::npos)
                    game.name = line.substr(first + 1, second - first - 1);
            }
            if (line.find("\"installdir\"") != std::string::npos) {
                auto first = line.find('"', quotes + 1);
                auto second = line.find('"', first + 1);
                if (first != std::string::npos && second != std::string::npos)
                    game.installDir = line.substr(first + 1, second - first - 1);
            }
        }

        if (!game.name.empty()) {
            games.push_back(game);
        }
    }

    return games;
}

std::string SteamIntegration::defaultDownloadDir() {
    const char* home = getenv("HOME");
    if (home) return std::string(home) + "/.metalsharp/games";
    return "/tmp/metalsharp/games";
}

bool SteamIntegration::downloadWindowsGame(const std::string& steamcmd, uint32_t appId, const std::string& outputDir) {
    mkdir(outputDir.c_str(), 0755);

    std::string cmd = steamcmd +
        " +@sSteamCmdForcePlatformType windows"
        " +force_install_dir \"" + outputDir + "\""
        " +login anonymous"
        " +app_update " + std::to_string(appId) + " validate"
        " +quit";

    printf("Downloading app %u via SteamCMD...\n", appId);
    printf("  Command: %s\n", cmd.c_str());

    int result = system(cmd.c_str());
    if (result != 0) {
        printf("  SteamCMD failed with code %d\n", result);
        return false;
    }

    printf("  Download complete\n");
    return true;
}

bool SteamIntegration::downloadDepot(const std::string& steamcmd, uint32_t appId, const std::string& depotId, const std::string& outputDir) {
    mkdir(outputDir.c_str(), 0755);

    std::string cmd = steamcmd +
        " +@sSteamCmdForcePlatformType windows"
        " +login anonymous"
        " +download_depot " + std::to_string(appId) + " " + depotId +
        " \"" + outputDir + "\""
        " +quit";

    printf("Downloading depot %s for app %u...\n", depotId.c_str(), appId);
    int result = system(cmd.c_str());
    return result == 0;
}

std::string SteamIntegration::findGameExecutable(const std::string& gameDir) {
    std::string cmd = "find \"" + gameDir + "\" -maxdepth 2 -name \"*.exe\" -type f 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    char buffer[1024];
    std::string result;
    if (fgets(buffer, sizeof(buffer), pipe)) {
        result = buffer;
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
    }
    pclose(pipe);
    return result;
}

bool SteamIntegration::authenticate(const std::string& steamcmd) {
    std::string cmd = steamcmd + " +login anonymous +quit";
    return system(cmd.c_str()) == 0;
}

}
