#include "launcher.h"

#include "bottles.h"

#include <string.h>

static const MetalsharpLaunchPolicy policies[] = {
    {
        "m12",
        "bin/metalsharp-wine",
        "dxmt",
        "lib/dxmt_m12/x86_64-windows",
        "lib/dxmt_m12/x86_64-unix:lib/wine/x86_64-unix",
        "winemetal.so",
        "winemetal,d3d12,dxgi,dxgi_dxmt,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d",
        true,
        true,
    },
    {
        "m11",
        "bin/metalsharp-wine",
        "dxmt",
        "lib/dxmt/x86_64-windows:lib/metalsharp/x86_64-windows",
        "lib/wine/x86_64-unix:lib/dxmt/x86_64-unix",
        "winemetal.so",
        "winemetal,dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d",
        true,
        true,
    },
    {
        "m10",
        "bin/metalsharp-wine",
        "dxmt",
        "lib/wine/x86_64-windows:lib/dxmt/x86_64-windows:lib/metalsharp/x86_64-windows",
        "lib/wine/x86_64-unix:lib/dxmt/x86_64-unix",
        "winemetal.so",
        "winemetal,d3d10,d3d10_1,dxgi,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d",
        true,
        true,
    },
    {
        "m11_32",
        "bin/metalsharp-wine",
        "dxmt",
        "lib/dxmt/i386-windows:lib/wine/i386-windows:lib/wine/x86_64-windows",
        "lib/wine/x86_64-unix:lib/dxmt/i386-unix:lib/wine",
        "winemetal.so",
        "d3d11,dxgi,winemetal=n,b;gameoverlayrenderer,gameoverlayrenderer64=d",
        true,
        true,
    },
    {
        "m10_32",
        "bin/metalsharp-wine",
        "dxmt",
        "lib/wine/i386-windows:lib/dxmt/i386-windows:lib/wine/x86_64-windows",
        "lib/wine/x86_64-unix:lib/dxmt/i386-unix:lib/wine",
        "winemetal.so",
        "d3d10,d3d10_1,d3d10core,d3d11,dxgi,winemetal=n,b;gameoverlayrenderer,gameoverlayrenderer64=d",
        true,
        true,
    },
};

const MetalsharpLaunchPolicy* metalsharp_launch_policy(const char* pipeline_id) {
    if (pipeline_id == NULL)
        return NULL;
    for (size_t i = 0; i < sizeof(policies) / sizeof(policies[0]); ++i)
        if (strcmp(pipeline_id, policies[i].pipeline_id) == 0)
            return &policies[i];
    return NULL;
}

bool metalsharp_launch_policy_valid(const MetalsharpLaunchPolicy* policy) {
    if (policy == NULL || metalsharp_bottle_policy(policy->pipeline_id) == NULL ||
        strcmp(policy->wine_binary, "bin/metalsharp-wine") != 0 || strcmp(policy->graphics_backend, "dxmt") != 0 ||
        strcmp(policy->winemetal_unixlib, "winemetal.so") != 0 || !policy->direct_executable ||
        !policy->steam_background_client || policy->windows_dll_path == NULL || policy->unix_library_path == NULL ||
        policy->dll_overrides == NULL)
        return false;
    if (strcmp(policy->pipeline_id, "m12") == 0)
        return strstr(policy->windows_dll_path, "dxmt_m12/x86_64-windows") != NULL &&
               strstr(policy->unix_library_path, "dxmt_m12/x86_64-unix") != NULL &&
               strstr(policy->dll_overrides, "d3d12") != NULL;
    return strstr(policy->dll_overrides, "d3d12") == NULL;
}

bool metalsharp_launcher_reserved_env_key(const char* pipeline_id, const char* key) {
    if (pipeline_id == NULL || key == NULL || strcmp(pipeline_id, "m12") != 0)
        return false;
    static const char* const reserved[] = {
        "WINEDLLOVERRIDES",       "WINEDLLPATH",      "DYLD_LIBRARY_PATH",   "DYLD_FALLBACK_LIBRARY_PATH",
        "DXMT_WINEMETAL_UNIXLIB", "DXMT_CONFIG_FILE", "MS_GRAPHICS_BACKEND", "WINEMSYNC",
        "DXMT_LOG_PATH",
    };
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i)
        if (strcmp(key, reserved[i]) == 0)
            return true;
    return false;
}

bool metalsharp_launcher_runtime_ready(const char* pipeline_id, const char* m12_root, size_t m12_root_len) {
    const MetalsharpLaunchPolicy* policy = metalsharp_launch_policy(pipeline_id);
    return metalsharp_launch_policy_valid(policy) &&
           metalsharp_bottle_runtime_ready(pipeline_id, m12_root, m12_root_len);
}
