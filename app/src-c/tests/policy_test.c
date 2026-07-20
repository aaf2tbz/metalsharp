#include "../bottles.h"
#include "../launcher.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static const char* const profiles[] = {"m12", "m11", "m10", "m11_32", "m10_32", "m9", "opengl"};
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
    const MetalsharpLaunchPolicy* m12_launch = metalsharp_launch_policy("m12");
    assert(strcmp(m12_launch->windows_dll_path, "lib/dxmt_m12/x86_64-windows") == 0);
    assert(strcmp(m12_launch->unix_library_path, "lib/dxmt_m12/x86_64-unix:lib/wine/x86_64-unix") == 0);
    assert(strcmp(m12_launch->dll_overrides,
                  "winemetal,d3d12,dxgi,dxgi_dxmt,d3d11,d3d10core=n,b;gameoverlayrenderer,gameoverlayrenderer64=d") ==
           0);
    assert(m12_launch->direct_executable);
    assert(m12_launch->steam_background_client);
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

    /* exe_args validation */
    const char* valid_args[] = {"-force-glcore", "+set", "r_renderer"};
    const char* invalid_empty[] = {""};
    const char* invalid_null[] = {NULL};
    const char* invalid_shell[] = {"; rm -rf /"};
    const char* invalid_traversal[] = {"../../../etc/passwd"};
    const char* invalid_control[] = {"foo\nbar"};
    const char* invalid_backtick[] = {"`id`"};

    assert(metalsharp_launcher_validate_exe_args(107100, 4, valid_args, 3));
    assert(!metalsharp_launcher_validate_exe_args(107100, 4, invalid_empty, 1));
    assert(!metalsharp_launcher_validate_exe_args(107100, 4, invalid_null, 1));
    assert(!metalsharp_launcher_validate_exe_args(107100, 4, invalid_shell, 1));
    assert(!metalsharp_launcher_validate_exe_args(107100, 4, invalid_traversal, 1));
    assert(!metalsharp_launcher_validate_exe_args(107100, 4, invalid_control, 1));
    assert(!metalsharp_launcher_validate_exe_args(107100, 4, invalid_backtick, 1));
    /* empty list always passes */
    assert(metalsharp_launcher_validate_exe_args(107100, 4, NULL, 0));

    puts("exe_args validation tests passed");
    puts("maintained bottle/launcher policy tests passed");
    return 0;
}
