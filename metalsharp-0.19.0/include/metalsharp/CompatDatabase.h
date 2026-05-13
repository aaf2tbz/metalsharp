/// @file CompatDatabase.h
/// @brief Persistent game compatibility database with Platinum–Broken ratings.
///
/// Stores per-game compatibility records including D3D version, anti-cheat/DRM status,
/// missing imports, crash history, average FPS, tester notes, and workarounds. Backed
/// by a disk-persisted map keyed by game ID. Supports querying by status tier, adding
/// missing import and crash records, and generating human-readable compatibility reports.
/// Populated by GameValidator and consumed by the launcher for pre-launch compatibility
/// checks.

#pragma once

#include <metalsharp/Platform.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace metalsharp {

enum class CompatStatus {
    Platinum,
    Gold,
    Silver,
    Bronze,
    Broken,
    Untested,
};

struct MissingImport {
    std::string dll;
    std::string function;
    bool isOrdinal;
    uint16_t ordinal;
};

struct CrashRecord {
    uint64_t timestamp;
    int signal;
    uint64_t faultAddr;
    uint64_t rip;
    uint64_t rsp;
    std::string crashRVA;
    std::string notes;
};

struct GameEntry {
    std::string gameId;
    std::string name;
    std::string exePath;
    std::string platform;
    CompatStatus status;
    std::string statusStr() const;
    uint32_t d3dVersion;
    std::string antiCheat;
    std::string drm;
    std::vector<MissingImport> missingImports;
    std::vector<CrashRecord> crashes;
    std::string notes;
    std::string workarounds;
    uint64_t lastTested;
    uint32_t avgFPS;
    std::string tester;
};

class CompatDatabase {
public:
    static CompatDatabase& instance();

    bool init(const std::string& dbPath);
    void shutdown();

    bool addOrUpdate(const GameEntry& entry);
    bool remove(const std::string& gameId);
    GameEntry* find(const std::string& gameId);
    const GameEntry* find(const std::string& gameId) const;

    std::vector<GameEntry> queryByStatus(CompatStatus status) const;
    std::vector<GameEntry> queryAll() const;
    size_t count() const;
    size_t countByStatus(CompatStatus status) const;

    bool saveToDisk();
    bool loadFromDisk();

    static CompatStatus statusFromString(const std::string& s);
    static const char* statusToString(CompatStatus s);

    void addMissingImport(const std::string& gameId, const MissingImport& imp);
    void addCrashRecord(const std::string& gameId, const CrashRecord& crash);

    std::string generateReport(const std::string& gameId) const;

private:
    CompatDatabase() = default;

    std::unordered_map<std::string, GameEntry> m_entries;
    std::string m_dbPath;
    bool m_initialized = false;
};

}
