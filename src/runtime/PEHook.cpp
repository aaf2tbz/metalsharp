#include <metalsharp/PEHook.h>
#include <metalsharp/Logger.h>
#include <cstdlib>
#include <cstring>

namespace metalsharp {

const char* PEHook::DYLIB_MAPPINGS[] = {
    "d3d11.dll",    "d3d11.dylib",
    "d3d12.dll",    "d3d12.dylib",
    "dxgi.dll",     "dxgi.dylib",
    "xaudio2_9.dll","xaudio2_9.dylib",
    "xinput1_4.dll","xinput1_4.dylib",
    nullptr
};

std::vector<DylibMapping> PEHook::getDylibMappings() {
    std::vector<DylibMapping> mappings;
    for (int i = 0; DYLIB_MAPPINGS[i]; i += 2) {
        mappings.push_back({DYLIB_MAPPINGS[i], DYLIB_MAPPINGS[i + 1]});
    }
    return mappings;
}

bool PEHook::injectDylibs(const std::string& winePrefix) {
    std::string dllDir = winePrefix + "/drive_c/windows/system32";

    auto mappings = getDylibMappings();
    for (const auto& m : mappings) {
        MS_INFO("DLL mapping: %s -> %s", m.windowsDll, m.metalsharpDylib);
    }

    MS_INFO("DLL injection configured for %zu libraries", mappings.size());
    return true;
}

bool PEHook::setupEnvironment(const std::string& winePrefix, bool debugMetal) {
    setenv("WINEPREFIX", winePrefix.c_str(), 1);

    setenv("WINEDLLOVERRIDES",
        "d3d11=native;d3d12=native;dxgi=native;xaudio2_9=native;xinput1_4=native",
        1);

    setenv("METALSHARP_WINE_PREFIX", winePrefix.c_str(), 1);

    if (debugMetal) {
        setenv("METAL_DEVICE_WRAPPER_TYPE", "1", 1);
        MS_INFO("Metal validation enabled");
    }

    MS_INFO("Environment configured: WINEPREFIX=%s", winePrefix.c_str());
    return true;
}

}
