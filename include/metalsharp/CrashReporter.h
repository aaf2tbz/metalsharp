/// @file CrashReporter.h
/// @brief Crash report collection, persistence, and session-bound diagnostics.
///
/// Manages per-game crash reporting sessions, collecting the game log, Metal validation
/// log, import log, and system info into a structured CrashReport. Reports are saved to
/// disk and can be retrieved for recent crashes. Used by the launcher to present crash
/// information to the user and by CompatDatabase to record crash history per game.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace metalsharp {

struct CrashReport {
    std::string id;
    std::string timestamp;
    std::string gameName;
    std::string exePath;
    std::string crashLog;
    std::string metalLog;
    std::string importLog;
    std::string systemInfo;
    int exitCode = 0;
    uint64_t faultAddress = 0;
    bool collected = false;
};

class CrashReporter {
  public:
    static CrashReporter& instance();

    void beginSession(const std::string& gameName, const std::string& exePath);
    void endSession(int exitCode);

    CrashReport collectReport();
    std::vector<CrashReport> getRecentReports(int count = 10);
    bool saveReport(const CrashReport& report);
    bool deleteReport(const std::string& id);

    std::string getReportsDir();
    std::string collectSystemInfo();

    static std::string generateId();
    static std::string formatTimestamp(int64_t epoch);

  private:
    CrashReporter() = default;
    std::string m_currentGame;
    std::string m_currentExe;
    int64_t m_sessionStart = 0;
    bool m_inSession = false;
};

} // namespace metalsharp
