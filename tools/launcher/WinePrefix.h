#pragma once

#include <string>

namespace metalsharp {

class WinePrefix {
public:
    explicit WinePrefix(const std::string& path);
    ~WinePrefix();

    bool init();
    bool isValid() const;
    std::string dllPath() const;
    std::string winePrefixPath() const;

    bool setDllOverride(const std::string& dllName);
    bool copyMetalSharpDlls();

private:
    std::string m_path;
    bool m_initialized = false;
};

}
