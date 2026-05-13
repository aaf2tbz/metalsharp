#include "SteamIntegration.h"
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>

namespace metalsharp {

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

}
