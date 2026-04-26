#pragma once

#include <dxgi/DXGI.h>
#include <vector>
#include <string>

namespace metalsharp {

struct DisplayMode {
    uint32_t width;
    uint32_t height;
    uint32_t refreshRate;
    uint32_t format;
};

class DXGIOutputImpl {
public:
    static std::vector<DisplayMode> enumerateDisplayModes();
    static DisplayMode getCurrentDisplayMode();
    static bool createWindow(void* parent, uint32_t width, uint32_t height, void** outWindow);
    static bool createMetalLayer(void* window, void** outLayer);
};

}
