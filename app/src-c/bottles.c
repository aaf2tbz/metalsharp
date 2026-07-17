#include "bottles.h"

#include "runtime_surface.h"

#include <string.h>

static const char* const m12_artifacts[] = {
    "x86_64-windows/d3d12.dll",   "x86_64-windows/d3d11.dll",      "x86_64-windows/d3d10core.dll",
    "x86_64-windows/dxgi.dll",    "x86_64-windows/dxgi_dxmt.dll",  "x86_64-windows/winemetal.dll",
    "x86_64-windows/nvapi64.dll", "x86_64-windows/nvngx.dll",      "x86_64-unix/winemetal.so",
    "x86_64-unix/libc++.1.dylib", "x86_64-unix/libc++abi.1.dylib", "x86_64-unix/libunwind.1.dylib",
};

static const char* const m11_artifacts[] = {
    "x86_64-windows/d3d11.dll",     "x86_64-windows/d3d10core.dll", "x86_64-windows/dxgi.dll",
    "x86_64-windows/dxgi_dxmt.dll", "x86_64-windows/winemetal.dll", "x86_64-unix/winemetal.so",
};

static const char* const m10_artifacts[] = {
    "x86_64-windows/d3d10core.dll", "x86_64-windows/d3d11.dll",     "x86_64-windows/dxgi.dll",
    "x86_64-windows/dxgi_dxmt.dll", "x86_64-windows/winemetal.dll", "x86_64-unix/winemetal.so",
};

static const char* const m11_i386_artifacts[] = {
    "i386-windows/d3d11.dll",     "i386-windows/d3d10core.dll", "i386-windows/dxgi.dll",
    "i386-windows/dxgi_dxmt.dll", "i386-windows/winemetal.dll", "i386-unix/winemetal.so",
};

static const char* const m10_i386_artifacts[] = {
    "i386-windows/d3d10core.dll", "i386-windows/d3d11.dll",     "i386-windows/dxgi.dll",
    "i386-windows/dxgi_dxmt.dll", "i386-windows/winemetal.dll", "i386-unix/winemetal.so",
};

#define POLICY(profile, pipeline, lane, arch, surface, files, d3d12)                                                   \
    {profile, pipeline, lane, arch, surface, METALSHARP_DXMT_MANIFEST_SHA256, files, sizeof(files) / sizeof(files[0]), \
     d3d12}

static const MetalsharpBottlePolicy policies[] = {
    POLICY("m12", "m12", "dxmt_m12", METALSHARP_BOTTLE_ARCH_X86_64, METALSHARP_DXMT_SURFACE_ID, m12_artifacts, true),
    POLICY("m11", "m11", "dxmt", METALSHARP_BOTTLE_ARCH_X86_64, METALSHARP_DXMT_SURFACE_ID, m11_artifacts, false),
    POLICY("m10", "m10", "dxmt", METALSHARP_BOTTLE_ARCH_X86_64, METALSHARP_DXMT_SURFACE_ID, m10_artifacts, false),
    POLICY("m11_32", "m11_32", "dxmt", METALSHARP_BOTTLE_ARCH_I386, METALSHARP_DXMT_I386_SURFACE_ID, m11_i386_artifacts,
           false),
    POLICY("m10_32", "m10_32", "dxmt", METALSHARP_BOTTLE_ARCH_I386, METALSHARP_DXMT_I386_SURFACE_ID, m10_i386_artifacts,
           false),
};

const MetalsharpBottlePolicy* metalsharp_bottle_policy(const char* profile_id) {
    if (profile_id == NULL)
        return NULL;
    for (size_t i = 0; i < sizeof(policies) / sizeof(policies[0]); ++i)
        if (strcmp(profile_id, policies[i].profile_id) == 0)
            return &policies[i];
    return NULL;
}

bool metalsharp_bottle_artifact_required(const MetalsharpBottlePolicy* policy, const char* relative_path) {
    if (policy == NULL || relative_path == NULL)
        return false;
    for (size_t i = 0; i < policy->artifact_count; ++i)
        if (strcmp(relative_path, policy->artifacts[i]) == 0)
            return true;
    return false;
}

bool metalsharp_bottle_policy_valid(const MetalsharpBottlePolicy* policy) {
    if (policy == NULL || policy->profile_id == NULL || policy->pipeline_id == NULL || policy->runtime_lane == NULL ||
        policy->surface_id == NULL || policy->manifest_sha256 == NULL || policy->artifacts == NULL ||
        policy->artifact_count == 0 || strcmp(policy->profile_id, policy->pipeline_id) != 0 ||
        strcmp(policy->manifest_sha256, METALSHARP_DXMT_MANIFEST_SHA256) != 0)
        return false;
    const bool has_d3d12 = metalsharp_bottle_artifact_required(policy, "x86_64-windows/d3d12.dll");
    if (has_d3d12 != policy->includes_d3d12 || (policy->includes_d3d12 && strcmp(policy->profile_id, "m12") != 0))
        return false;
    if (policy->architecture == METALSHARP_BOTTLE_ARCH_I386)
        return strcmp(policy->runtime_lane, "dxmt") == 0 &&
               strcmp(policy->surface_id, METALSHARP_DXMT_I386_SURFACE_ID) == 0;
    return strcmp(policy->surface_id, METALSHARP_DXMT_SURFACE_ID) == 0;
}

bool metalsharp_bottle_runtime_ready(const char* profile_id, const char* m12_root, size_t m12_root_len) {
    const MetalsharpBottlePolicy* policy = metalsharp_bottle_policy(profile_id);
    if (!metalsharp_bottle_policy_valid(policy) || m12_root == NULL || m12_root_len == 0)
        return false;

    /* The frozen surface verifier proves both x64 roots, mirrors, receipts, and the preserved i386 lane. */
    return metalsharp_dxmt_surface_current(m12_root, m12_root_len);
}
