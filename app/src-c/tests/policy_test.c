#include "../bottles.h"
#include "../launcher.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static const char* const profiles[] = {"m12", "m11", "m10", "m11_32", "m10_32"};
    for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); ++i) {
        const MetalsharpBottlePolicy* bottle = metalsharp_bottle_policy(profiles[i]);
        const MetalsharpLaunchPolicy* launch = metalsharp_launch_policy(profiles[i]);
        assert(metalsharp_bottle_policy_valid(bottle));
        assert(metalsharp_launch_policy_valid(launch));
        assert(strcmp(bottle->pipeline_id, launch->pipeline_id) == 0);
        assert(strcmp(launch->wine_binary, "bin/metalsharp-wine") == 0);
        assert(strcmp(launch->graphics_backend, "dxmt") == 0);
        assert(strcmp(launch->winemetal_unixlib, "winemetal.so") == 0);
    }

    const MetalsharpBottlePolicy* m12 = metalsharp_bottle_policy("m12");
    assert(m12->includes_d3d12);
    assert(metalsharp_bottle_artifact_required(m12, "x86_64-windows/d3d12.dll"));
    assert(metalsharp_bottle_artifact_required(m12, "x86_64-unix/winemetal.so"));
    assert(strstr(metalsharp_launch_policy("m12")->windows_dll_path, "dxmt_m12") != NULL);
    assert(metalsharp_launcher_reserved_env_key("m12", "MS_GRAPHICS_BACKEND"));
    assert(metalsharp_launcher_reserved_env_key("m12", "DXMT_WINEMETAL_UNIXLIB"));
    assert(!metalsharp_launcher_reserved_env_key("m11", "MS_GRAPHICS_BACKEND"));

    for (size_t i = 1; i < sizeof(profiles) / sizeof(profiles[0]); ++i) {
        const MetalsharpBottlePolicy* bottle = metalsharp_bottle_policy(profiles[i]);
        assert(!bottle->includes_d3d12);
        assert(!metalsharp_bottle_artifact_required(bottle, "x86_64-windows/d3d12.dll"));
        assert(strstr(metalsharp_launch_policy(profiles[i])->dll_overrides, "d3d12") == NULL);
    }

    assert(strcmp(metalsharp_bottle_policy("m11_32")->surface_id, METALSHARP_DXMT_I386_SURFACE_ID) == 0);
    assert(strcmp(metalsharp_bottle_policy("m10_32")->surface_id, METALSHARP_DXMT_I386_SURFACE_ID) == 0);
    assert(metalsharp_bottle_policy(NULL) == NULL);
    assert(metalsharp_launch_policy("unknown") == NULL);
    puts("maintained bottle/launcher policy tests passed");
    return 0;
}
