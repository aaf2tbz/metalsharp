#pragma once

#include <metalsharp/Platform.h>
#include <string>
#include <cstdint>

namespace metalsharp {

struct CrashInfo {
    int signal = 0;
    uint64_t faultAddr = 0;
    uint64_t rip = 0;
    uint64_t rsp = 0;
    uint64_t rax = 0;
    uint64_t rbx = 0;
    uint64_t rcx = 0;
    uint64_t rdx = 0;
    std::string crashRVA;
    uint64_t moduleBase = 0;
    uint64_t moduleSize = 0;
    std::string modulePath;
};

class CrashDiagnostics {
public:
    static CrashDiagnostics& instance();

    void init(const std::string& diagDir);
    void shutdown();

    void setModuleInfo(uint64_t base, uint64_t size, const std::string& path);
    void setGameId(const std::string& gameId);

    void writeCrashDump(const CrashInfo& info);
    void writeDiagnosticsBundle();

    static std::string formatTimestamp(uint64_t epoch);
    std::string crashDir() const { return m_diagDir + "/crashes"; }

    uint32_t crashCount() const { return m_crashCount; }

private:
    CrashDiagnostics() = default;

    std::string m_diagDir;
    std::string m_gameId;
    uint64_t m_moduleBase = 0;
    uint64_t m_moduleSize = 0;
    std::string m_modulePath;
    uint32_t m_crashCount = 0;
    bool m_initialized = false;
};

}
