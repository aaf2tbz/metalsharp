/// @file StubTelemetry.h
/// @brief Structured reporting for compatibility-critical stub behavior.

#pragma once

#include <cstdint>

namespace metalsharp {

enum class StubBehavior : uint32_t {
    HarmlessNoOp,
    CompatibilityShim,
    UnsupportedHardFailure,
};

class StubTelemetry {
  public:
    static void record(const char* dllName, const char* functionName, StubBehavior behavior, const char* returnBehavior,
                       const char* detail = nullptr);
    static bool strictModeEnabled();
};

} // namespace metalsharp
