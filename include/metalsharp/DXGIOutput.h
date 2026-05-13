/// @file DXGIOutput.h
/// @brief Display mode enumeration and Metal-backed window creation.
///
/// Implements IDXGIOutput functionality by querying macOS display modes via
/// CoreGraphics and creating native NSWindow/CAMetalLayer surfaces. The swap
/// chain depends on this module to resolve render targets and present frames.
/// Display modes are converted from CGDirectDisplayID to DXGI_FORMAT-compatible
/// MetalPixelFormat entries for use throughout the D3D→Metal pipeline.

#pragma once

#include <dxgi/DXGI.h>
#include <string>
#include <vector>

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

} // namespace metalsharp
