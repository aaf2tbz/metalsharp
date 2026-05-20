/// @file HostRuntimeABI.cpp
/// @brief Minimal host runtime ABI query surface for bottle-aware runtime shims.
#include "metalsharp/HostRuntimeABI.h"

#include <cstring>

extern "C" MetalSharpHostAbiVersion metalsharp_host_get_abi_version(void) {
    return metalsharp_host_abi_version();
}

extern "C" MetalSharpHostResult metalsharp_host_query_capabilities(MetalSharpHostCapabilities* capabilities) {
    if (!capabilities)
        return METALSHARP_HOST_ERR_INVALID_ARGUMENT;

    std::memset(capabilities, 0, sizeof(*capabilities));
    capabilities->struct_size = sizeof(*capabilities);
    capabilities->services = METALSHARP_HOST_SERVICE_BIT(METALSHARP_HOST_SERVICE_PROCESS) |
                             METALSHARP_HOST_SERVICE_BIT(METALSHARP_HOST_SERVICE_PATHS) |
                             METALSHARP_HOST_SERVICE_BIT(METALSHARP_HOST_SERVICE_LOGGING) |
                             METALSHARP_HOST_SERVICE_BIT(METALSHARP_HOST_SERVICE_STEAM) |
                             METALSHARP_HOST_SERVICE_BIT(METALSHARP_HOST_SERVICE_GRAPHICS) |
                             METALSHARP_HOST_SERVICE_BIT(METALSHARP_HOST_SERVICE_AUDIO) |
                             METALSHARP_HOST_SERVICE_BIT(METALSHARP_HOST_SERVICE_INPUT) |
                             METALSHARP_HOST_SERVICE_BIT(METALSHARP_HOST_SERVICE_MANAGED_RUNTIME);
    capabilities->supports_wow64 = 1;
    capabilities->supports_steam_bridge = 1;
    capabilities->supports_managed_runtime = 1;
    capabilities->supports_metal_graphics = 1;
    return METALSHARP_HOST_OK;
}

extern "C" MetalSharpHostResult metalsharp_host_self_test(MetalSharpHostCapabilities* capabilities) {
    MetalSharpHostAbiVersion version = metalsharp_host_get_abi_version();
    if (!metalsharp_host_abi_is_compatible(&version))
        return METALSHARP_HOST_ERR_UNSUPPORTED;

    return metalsharp_host_query_capabilities(capabilities);
}
