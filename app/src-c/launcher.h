#ifndef METALSHARP_LAUNCHER_H
#define METALSHARP_LAUNCHER_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char* pipeline_id;
    const char* wine_binary;
    const char* graphics_backend;
    const char* windows_dll_path;
    const char* unix_library_path;
    const char* winemetal_unixlib;
    const char* dll_overrides;
    bool direct_executable;
    bool steam_background_client;
} MetalsharpLaunchPolicy;

const MetalsharpLaunchPolicy* metalsharp_launch_policy(const char* pipeline_id);
bool metalsharp_launch_policy_valid(const MetalsharpLaunchPolicy* policy);
bool metalsharp_launcher_reserved_env_key(const char* pipeline_id, const char* key);
bool metalsharp_launcher_runtime_ready(const char* pipeline_id, const char* m12_root, size_t m12_root_len);

#endif
