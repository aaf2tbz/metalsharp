/// @file GameDetector.h
/// @brief Game detection from Steam, Epic Games Store, GOG, and local directories.
///
/// Scans installed game libraries by parsing Steam's libraryfolders.vdf and appinfo
/// manifests, Epic's .item manifest JSON files, and GOG's product database. Local
/// detection scans arbitrary directories for Windows executables. Returns DetectedGame
/// entries with platform IDs, install paths, and executable locations for use by the
/// launcher and compatibility testing infrastructure.

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace metalsharp {

enum class GamePlatform {
    Steam,
    EpicGamesStore,
    GOG,
    Local,
    Unknown,
};

struct DetectedGame {
    std::string id;
    std::string name;
    std::string exePath;
    std::string installDir;
    GamePlatform platform;
    uint32_t steamAppId = 0;
    std::string epicProductId;
    std::string gogProductId;
    int64_t sizeBytes = 0;
    bool exeExists = false;
};

class GameDetector {
public:
    static std::vector<DetectedGame> detectAll();
    static std::vector<DetectedGame> detectSteam();
    static std::vector<DetectedGame> detectEpic();
    static std::vector<DetectedGame> detectGOG();
    static std::vector<DetectedGame> detectLocal();

    static std::string platformToString(GamePlatform p);
    static GamePlatform platformFromString(const std::string& s);

    static std::string findSteamLibraryDirs(const std::string& steamInstallDir);
    static std::string findEpicManifestDir();
    static std::string findGOGDbPath();

private:
    static std::vector<DetectedGame> parseSteamLibraryFolders(const std::string& vdfPath);
    static std::vector<DetectedGame> scanSteamApps(const std::string& libraryDir);
    static std::vector<DetectedGame> parseEpicManifests(const std::string& manifestDir);
    static std::vector<DetectedGame> parseGOGDb(const std::string& dbPath);
    static std::vector<DetectedGame> scanLocalDir(const std::string& dir);
    static std::string findGameExe(const std::string& dir);
    static int64_t calculateDirSize(const std::string& dir);
    static bool fileExists(const std::string& path);
};

}
