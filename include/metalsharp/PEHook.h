/// @file PEHook.h
/// @brief DLL injection and environment setup for Wine prefixes.
///
/// Maps Windows DLL names to MetalSharp dylib equivalents (e.g.
/// d3d11.dll → libmetalsharp_d3d11.dylib) and configures the Wine
/// environment (WINEPREFIX, WINEDLLOVERRIDES) so that MetalSharp's
/// shims are loaded instead of Wine's built-in D3D implementations.

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

} // namespace metalsharp
