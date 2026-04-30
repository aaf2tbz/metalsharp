#include "metalsharp/GameDetector.h"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <cstdlib>

#ifdef __APPLE__
#include <sys/stat.h>
#endif

namespace metalsharp {

namespace fs = std::filesystem;

std::string GameDetector::platformToString(GamePlatform p) {
    switch (p) {
        case GamePlatform::Steam: return "steam";
        case GamePlatform::EpicGamesStore: return "epic";
        case GamePlatform::GOG: return "gog";
        case GamePlatform::Local: return "local";
        default: return "unknown";
    }
}

GamePlatform GameDetector::platformFromString(const std::string& s) {
    if (s == "steam") return GamePlatform::Steam;
    if (s == "epic") return GamePlatform::EpicGamesStore;
    if (s == "gog") return GamePlatform::GOG;
    if (s == "local") return GamePlatform::Local;
    return GamePlatform::Unknown;
}

bool GameDetector::fileExists(const std::string& path) {
    return fs::exists(path);
}

int64_t GameDetector::calculateDirSize(const std::string& dir) {
    int64_t total = 0;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(dir,
                     fs::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file()) {
                total += static_cast<int64_t>(entry.file_size());
            }
        }
    } catch (...) {}
    return total;
}

std::string GameDetector::findGameExe(const std::string& dir) {
    try {
        for (const auto& entry : fs::recursive_directory_iterator(dir,
                     fs::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file()) {
                auto path = entry.path();
                if (path.extension() == ".exe") {
                    return path.string();
                }
            }
        }
    } catch (...) {}
    return "";
}

std::vector<DetectedGame> GameDetector::detectAll() {
    std::vector<DetectedGame> games;
    auto steam = detectSteam();
    games.insert(games.end(), steam.begin(), steam.end());
    auto epic = detectEpic();
    games.insert(games.end(), epic.begin(), epic.end());
    auto gog = detectGOG();
    games.insert(games.end(), gog.begin(), gog.end());
    auto local = detectLocal();
    games.insert(games.end(), local.begin(), local.end());
    return games;
}

std::string GameDetector::findSteamLibraryDirs(const std::string& steamInstallDir) {
    return steamInstallDir + "/steamapps/libraryfolders.vdf";
}

std::vector<DetectedGame> GameDetector::parseSteamLibraryFolders(const std::string& vdfPath) {
    std::vector<DetectedGame> games;
    if (!fileExists(vdfPath)) return games;

    std::ifstream f(vdfPath);
    if (!f.is_open()) return games;

    std::string line;
    std::vector<std::string> libraryPaths;

    while (std::getline(f, line)) {
        auto pos = line.find("\"path\"");
        if (pos != std::string::npos) {
            auto start = line.find('\"', pos + 7);
            if (start != std::string::npos) {
                auto end = line.find('\"', start + 1);
                if (end != std::string::npos) {
                    libraryPaths.push_back(line.substr(start + 1, end - start - 1));
                }
            }
        }
    }

    for (const auto& libDir : libraryPaths) {
        auto libGames = scanSteamApps(libDir);
        games.insert(games.end(), libGames.begin(), libGames.end());
    }

    return games;
}

std::vector<DetectedGame> GameDetector::scanSteamApps(const std::string& libraryDir) {
    std::vector<DetectedGame> games;
    std::string steamapps = libraryDir + "/steamapps";
    if (!fs::exists(steamapps)) return games;

    try {
        for (const auto& entry : fs::directory_iterator(steamapps)) {
            if (!entry.is_regular_file()) continue;
            auto name = entry.path().filename().string();
            if (name.find("appmanifest_") == 0 && name.find(".acf") != std::string::npos) {
                auto acfPath = entry.path().string();
                std::ifstream af(acfPath);
                if (!af.is_open()) continue;

                DetectedGame game;
                game.platform = GamePlatform::Steam;

                std::string aline;
                while (std::getline(af, aline)) {
                    auto trim = [](std::string s) {
                        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
                        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
                        return s;
                    };

                    auto extractValue = [&trim](const std::string& l, const std::string& key) -> std::string {
                        auto p = l.find(key);
                        if (p == std::string::npos) return "";
                        auto s = l.find('\"', p + key.length());
                        if (s == std::string::npos) return "";
                        auto e = l.find('\"', s + 1);
                        if (e == std::string::npos) return "";
                        return l.substr(s + 1, e - s - 1);
                    };

                    if (aline.find("\"appid\"") != std::string::npos) {
                        game.steamAppId = static_cast<uint32_t>(std::stoul(extractValue(aline, "\"appid\"")));
                        game.id = "steam_" + std::to_string(game.steamAppId);
                    }
                    if (aline.find("\"name\"") != std::string::npos) {
                        game.name = extractValue(aline, "\"name\"");
                    }
                    if (aline.find("\"installDir\"") != std::string::npos) {
                        game.installDir = extractValue(aline, "\"installDir\"");
                    }
                }

                if (game.steamAppId > 0 && !game.name.empty()) {
                    std::string fullPath = steamapps + "/common/" + game.installDir;
                    game.exePath = findGameExe(fullPath);
                    game.exeExists = !game.exePath.empty();
                    game.sizeBytes = calculateDirSize(fullPath);
                    games.push_back(game);
                }
            }
        }
    } catch (...) {}

    return games;
}

std::vector<DetectedGame> GameDetector::detectSteam() {
    std::vector<DetectedGame> games;

    const char* home = std::getenv("HOME");
    if (!home) return games;

    std::string steamDir;
    std::string possiblePaths[] = {
        std::string(home) + "/Library/Application Support/Steam",
        std::string(home) + "/.steam/steam",
        std::string(home) + "/.local/share/Steam",
    };

    for (const auto& p : possiblePaths) {
        if (fileExists(p)) {
            steamDir = p;
            break;
        }
    }

    if (steamDir.empty()) return games;

    std::string vdfPath = findSteamLibraryDirs(steamDir);
    if (fileExists(vdfPath)) {
        return parseSteamLibraryFolders(vdfPath);
    }

    std::string steamapps = steamDir + "/steamapps";
    if (fileExists(steamapps)) {
        return scanSteamApps(steamDir);
    }

    return games;
}

std::string GameDetector::findEpicManifestDir() {
    const char* home = std::getenv("HOME");
    if (!home) return "";

    std::string path = std::string(home) + "/Library/Application Support/Epic/Epic Games Launcher/Data/Manifests";
    if (fileExists(path)) return path;
    return "";
}

std::vector<DetectedGame> GameDetector::parseEpicManifests(const std::string& manifestDir) {
    std::vector<DetectedGame> games;
    if (manifestDir.empty()) return games;

    try {
        for (const auto& entry : fs::directory_iterator(manifestDir)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".item") continue;

            std::ifstream f(entry.path());
            if (!f.is_open()) continue;

            DetectedGame game;
            game.platform = GamePlatform::EpicGamesStore;

            std::string line;
            while (std::getline(f, line)) {
                auto extractString = [](const std::string& l, const std::string& key) -> std::string {
                    auto p = l.find(key);
                    if (p == std::string::npos) return "";
                    auto s = l.find('\"', p + key.length());
                    if (s == std::string::npos) return "";
                    auto e = l.find('\"', s + 1);
                    if (e == std::string::npos) return "";
                    return l.substr(s + 1, e - s - 1);
                };

                if (line.find("\"AppName\"") != std::string::npos) {
                    game.epicProductId = extractString(line, "\"AppName\"");
                    game.id = "epic_" + game.epicProductId;
                }
                if (line.find("\"DisplayName\"") != std::string::npos) {
                    game.name = extractString(line, "\"DisplayName\"");
                }
                if (line.find("\"InstallLocation\"") != std::string::npos) {
                    game.installDir = extractString(line, "\"InstallLocation\"");
                }
            }

            if (!game.epicProductId.empty() && !game.name.empty()) {
                game.exePath = findGameExe(game.installDir);
                game.exeExists = !game.exePath.empty();
                game.sizeBytes = calculateDirSize(game.installDir);
                games.push_back(game);
            }
        }
    } catch (...) {}

    return games;
}

std::vector<DetectedGame> GameDetector::detectEpic() {
    return parseEpicManifests(findEpicManifestDir());
}

std::string GameDetector::findGOGDbPath() {
    const char* home = std::getenv("HOME");
    if (!home) return "";

    std::string path = std::string(home) + "/Library/Application Support/GOG.com/Galaxy/Communities";
    if (fileExists(path)) return path;
    return "";
}

std::vector<DetectedGame> GameDetector::parseGOGDb(const std::string& dbPath) {
    return {};
}

std::vector<DetectedGame> GameDetector::detectGOG() {
    return parseGOGDb(findGOGDbPath());
}

std::vector<DetectedGame> GameDetector::scanLocalDir(const std::string& dir) {
    std::vector<DetectedGame> games;
    if (!fileExists(dir)) return games;

    try {
        for (const auto& entry : fs::directory_iterator(dir,
                     fs::directory_options::skip_permission_denied)) {
            if (!entry.is_directory()) continue;

            DetectedGame game;
            game.id = "local_" + entry.path().filename().string();
            game.name = entry.path().filename().string();
            game.platform = GamePlatform::Local;
            game.installDir = entry.path().string();
            game.exePath = findGameExe(entry.path().string());
            game.exeExists = !game.exePath.empty();
            if (game.exeExists) {
                game.sizeBytes = calculateDirSize(entry.path().string());
                games.push_back(game);
            }
        }
    } catch (...) {}

    return games;
}

std::vector<DetectedGame> GameDetector::detectLocal() {
    const char* home = std::getenv("HOME");
    if (!home) return {};

    std::string gameDir = std::string(home) + "/.metalsharp/games";
    return scanLocalDir(gameDir);
}

}
