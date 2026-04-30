#pragma once

#include <string>
#include <vector>
#include <cstdint>

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

}
