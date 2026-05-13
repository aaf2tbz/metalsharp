#include "WinePrefix.h"
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace metalsharp {

WinePrefix::WinePrefix(const std::string& path) : m_path(path) {}
WinePrefix::~WinePrefix() = default;

std::string WinePrefix::defaultPrefixPath() {
    const char* home = getenv("HOME");
    if (home) return std::string(home) + "/.metalsharp/prefix";
    return "/tmp/metalsharp/prefix";
}

bool WinePrefix::init() {
    struct stat st{};
    if (stat(m_path.c_str(), &st) != 0) {
        if (!createWinePrefix()) return false;
    }

    std::string dllDir = dllPath();
    if (stat(dllDir.c_str(), &st) != 0) {
        mkdir(dllDir.c_str(), 0755);
    }

    std::string driveC = m_path + "/drive_c";
    if (stat(driveC.c_str(), &st) != 0) {
        mkdir(driveC.c_str(), 0755);
        mkdir((driveC + "/windows").c_str(), 0755);
        mkdir((driveC + "/windows/system32").c_str(), 0755);
        mkdir((driveC + "/windows/syswow64").c_str(), 0755);
    }

    m_initialized = true;
    return true;
}

bool WinePrefix::createWinePrefix() {
    mkdir(m_path.c_str(), 0755);
    mkdir((m_path + "/drive_c").c_str(), 0755);
    mkdir((m_path + "/drive_c/windows").c_str(), 0755);
    mkdir((m_path + "/drive_c/windows/system32").c_str(), 0755);
    mkdir((m_path + "/drive_c/windows/syswow64").c_str(), 0755);

    std::ofstream dosdev(m_path + "/dosdevices");
    dosdev << "# MetalSharp Wine prefix\n";

    std::ofstream ver(m_path + "/.update-timestamp");
    ver << "metalsharp-0.1.1\n";

    return true;
}

bool WinePrefix::isValid() const { return m_initialized; }
std::string WinePrefix::dllPath() const { return m_path + "/dlls"; }
std::string WinePrefix::winePrefixPath() const { return m_path; }
std::string WinePrefix::userRegPath() const { return m_path + "/user.reg"; }
std::string WinePrefix::systemRegPath() const { return m_path + "/system.reg"; }

bool WinePrefix::setDllOverride(const std::string& dllName) {
    m_dllOverrides.push_back(dllName);
    return true;
}

bool WinePrefix::writeRegistryDllOverrides() {
    if (m_dllOverrides.empty()) return true;

    std::ofstream reg(userRegPath(), std::ios::app);
    if (!reg.is_open()) return false;

    reg << "\n[Software\\\\Wine\\\\DllOverrides]\n";
    for (const auto& dll : m_dllOverrides) {
        reg << "\"" << dll << "\"=\"native\"\n";
        printf("  DLL override: %s=native\n", dll.c_str());
    }
    return true;
}

bool WinePrefix::copyMetalSharpDlls() {
    const char* buildDir = getenv("METALSHARP_BUILD_DIR");
    std::string sourceDir = buildDir ? buildDir : "";

    const char* dllNames[] = {"d3d11.dylib", "d3d12.dylib", "dxgi.dylib",
                              "xaudio2_9.dylib", "xinput1_4.dylib"};

    std::string dest = dllPath();
    printf("  Installing MetalSharp DLLs to %s\n", dest.c_str());

    for (const auto& dll : dllNames) {
        std::string src = sourceDir.empty() ? dll : sourceDir + "/" + dll;
        std::string dst = dest + "/" + dll;

        std::ifstream in(src, std::ios::binary);
        if (in.is_open()) {
            std::ofstream out(dst, std::ios::binary);
            out << in.rdbuf();
            printf("    Installed %s\n", dll);
        } else {
            printf("    Note: %s not found in build dir, will use built-in\n", dll);
        }
    }

    return true;
}

}
