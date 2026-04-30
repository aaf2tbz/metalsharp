#include <metalsharp/CrashDiagnostics.h>
#include <metalsharp/Logger.h>
#include <fstream>
#include <sstream>
#include <ctime>
#include <sys/stat.h>

namespace metalsharp {

CrashDiagnostics& CrashDiagnostics::instance() {
    static CrashDiagnostics inst;
    return inst;
}

void CrashDiagnostics::init(const std::string& diagDir) {
    m_diagDir = diagDir;
    mkdir(m_diagDir.c_str(), 0755);
    mkdir((m_diagDir + "/crashes").c_str(), 0755);
    m_initialized = true;
    m_crashCount = 0;
    MS_INFO("CrashDiagnostics initialized: %s", m_diagDir.c_str());
}

void CrashDiagnostics::shutdown() {
    m_initialized = false;
}

void CrashDiagnostics::setModuleInfo(uint64_t base, uint64_t size, const std::string& path) {
    m_moduleBase = base;
    m_moduleSize = size;
    m_modulePath = path;
}

void CrashDiagnostics::setGameId(const std::string& gameId) {
    m_gameId = gameId;
}

std::string CrashDiagnostics::formatTimestamp(uint64_t epoch) {
    time_t t = static_cast<time_t>(epoch);
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm_buf);
    return buf;
}

void CrashDiagnostics::writeCrashDump(const CrashInfo& info) {
    if (!m_initialized) return;

    m_crashCount++;

    uint64_t now = static_cast<uint64_t>(time(nullptr));
    std::string ts = formatTimestamp(now);
    std::string filename = crashDir() + "/crash_" + ts + ".txt";

    std::ofstream file(filename);
    if (!file.is_open()) {
        MS_ERROR("CrashDiagnostics: failed to write crash dump to %s", filename.c_str());
        return;
    }

    file << "=== MetalSharp Crash Dump ===\n";
    file << "Timestamp:  " << ts << "\n";
    file << "Game ID:    " << m_gameId << "\n";
    file << "Module:     " << m_modulePath << "\n";
    file << "Signal:     " << info.signal << " (";

    switch (info.signal) {
        case 11: file << "SIGSEGV"; break;
        case 10: file << "SIGBUS"; break;
        case 6:  file << "SIGABRT"; break;
        case 8:  file << "SIGFPE"; break;
        default: file << "unknown"; break;
    }
    file << ")\n";

    file << "\n--- Registers ---\n";
    file << "Fault addr: 0x" << std::hex << info.faultAddr << std::dec << "\n";
    file << "RIP:        0x" << std::hex << info.rip << std::dec;
    if (info.rip >= m_moduleBase && info.rip < m_moduleBase + m_moduleSize) {
        file << "  (RVA 0x" << std::hex << (info.rip - m_moduleBase) << std::dec << ")";
    }
    file << "\n";
    file << "RSP:        0x" << std::hex << info.rsp << std::dec << "\n";
    file << "RAX:        0x" << std::hex << info.rax << std::dec << "\n";
    file << "RBX:        0x" << std::hex << info.rbx << std::dec << "\n";
    file << "RCX:        0x" << std::hex << info.rcx << std::dec << "\n";
    file << "RDX:        0x" << std::hex << info.rdx << std::dec << "\n";

    if (info.rip == 0) {
        file << "\n--- Analysis ---\n";
        file << "RIP is NULL — likely called unresolved import (null IAT entry)\n";
    } else if (info.rip >= m_moduleBase && info.rip < m_moduleBase + m_moduleSize) {
        file << "\n--- Analysis ---\n";
        file << "Crash inside PE image. Check missing imports or unsupported instruction.\n";
    } else {
        file << "\n--- Analysis ---\n";
        file << "Crash outside PE image — possible bad code pointer or stack corruption.\n";
    }

    file.close();
    MS_INFO("CrashDiagnostics: crash dump written to %s", filename.c_str());
}

void CrashDiagnostics::writeDiagnosticsBundle() {
    if (!m_initialized) return;

    std::string bundlePath = m_diagDir + "/diagnostics.txt";
    std::ofstream file(bundlePath);
    if (!file.is_open()) return;

    file << "=== MetalSharp Diagnostics Bundle ===\n";
    file << "Generated: " << formatTimestamp(static_cast<uint64_t>(time(nullptr))) << "\n\n";

    file << "--- Module ---\n";
    file << "Path: " << m_modulePath << "\n";
    file << "Base: 0x" << std::hex << m_moduleBase << std::dec << "\n";
    file << "Size: " << m_moduleSize << " bytes\n\n";

    file << "--- Crash Summary ---\n";
    file << "Total crashes: " << m_crashCount << "\n\n";

    file.close();
    MS_INFO("CrashDiagnostics: diagnostics bundle written to %s", bundlePath.c_str());
}

}
