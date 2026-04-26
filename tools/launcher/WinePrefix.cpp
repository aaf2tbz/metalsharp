#include "WinePrefix.h"
#include <sys/stat.h>
#include <cstdio>
#include <cstring>

namespace metalsharp {

WinePrefix::WinePrefix(const std::string& path) : m_path(path) {}
WinePrefix::~WinePrefix() = default;

bool WinePrefix::init() {
    struct stat st{};
    if (stat(m_path.c_str(), &st) != 0) {
        if (mkdir(m_path.c_str(), 0755) != 0) {
            return false;
        }
    }

    std::string dllDir = dllPath();
    if (stat(dllDir.c_str(), &st) != 0) {
        if (mkdir(dllDir.c_str(), 0755) != 0) {
            return false;
        }
    }

    m_initialized = true;
    return true;
}

bool WinePrefix::isValid() const {
    return m_initialized;
}

std::string WinePrefix::dllPath() const {
    return m_path + "/dlls";
}

std::string WinePrefix::winePrefixPath() const {
    return m_path;
}

bool WinePrefix::setDllOverride(const std::string& dllName) {
    printf("  DLL override: %s=native\n", dllName.c_str());
    return true;
}

bool WinePrefix::copyMetalSharpDlls() {
    printf("  Installing MetalSharp DLLs to %s\n", dllPath().c_str());
    return true;
}

}
