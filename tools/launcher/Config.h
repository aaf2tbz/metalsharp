#pragma once

#include <string>

namespace metalsharp {

struct Config {
    std::string winePrefix;
    std::string executable;
    std::string workingDir;
    bool verbose = false;
    bool debugMetal = false;
    uint32_t width = 1920;
    uint32_t height = 1080;
};

}
