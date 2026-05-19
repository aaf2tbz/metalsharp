/// @file StubTelemetry.cpp
/// @brief Structured reporting for compatibility-critical stub behavior.

#include <cstdlib>
#include <cstring>
#include <metalsharp/Logger.h>
#include <metalsharp/StubTelemetry.h>

namespace metalsharp {

static const char* behaviorName(StubBehavior behavior) {
    switch (behavior) {
    case StubBehavior::HarmlessNoOp:
        return "harmless_noop";
    case StubBehavior::CompatibilityShim:
        return "compatibility_shim";
    case StubBehavior::UnsupportedHardFailure:
        return "unsupported_hard_failure";
    }
    return "unknown";
}

bool StubTelemetry::strictModeEnabled() {
    const char* value = std::getenv("METALSHARP_STRICT_STUBS");
    return value && (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0);
}

void StubTelemetry::record(const char* dllName, const char* functionName, StubBehavior behavior,
                           const char* returnBehavior, const char* detail) {
    MS_WARN("stub_api_hit dll=%s function=%s category=%s return=%s detail=%s", dllName ? dllName : "unknown",
            functionName ? functionName : "unknown", behaviorName(behavior),
            returnBehavior ? returnBehavior : "unknown", detail ? detail : "");
}

} // namespace metalsharp
