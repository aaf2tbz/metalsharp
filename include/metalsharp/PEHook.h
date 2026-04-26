#pragma once

#include <string>
#include <vector>

namespace metalsharp {

struct DylibMapping {
    const char* windowsDll;
    const char* metalsharpDylib;
};

class PEHook {
public:
    static bool injectDylibs(const std::string& winePrefix);
    static bool setupEnvironment(const std::string& winePrefix, bool debugMetal);
    static std::vector<DylibMapping> getDylibMappings();

    static const char* DYLIB_MAPPINGS[];
};

}
