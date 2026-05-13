#pragma once

#include <string>
#include <vector>

namespace metalsharp {

class WinePrefix {
public:
    explicit WinePrefix(const std::string& path);
    ~WinePrefix();

    bool init();
    bool isValid() const;
    std::string dllPath() const;
    std::string winePrefixPath() const;
    std::string userRegPath() const;
    std::string systemRegPath() const;

    bool setDllOverride(const std::string& dllName);
    bool copyMetalSharpDlls();
    bool writeRegistryDllOverrides();
    bool createWinePrefix();

    static std::string defaultPrefixPath();

private:
    std::string m_path;
    bool m_initialized = false;
    std::vector<std::string> m_dllOverrides;
};

}
