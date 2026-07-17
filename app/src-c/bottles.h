#ifndef METALSHARP_BOTTLES_H
#define METALSHARP_BOTTLES_H

#include <stdbool.h>
#include <stddef.h>

#define METALSHARP_DXMT_MANIFEST_SHA256 "8e7f5dbbf1e13a7e9f421dfa53ce3f50522bb7d4550d3662d475697d19f2f2e7"
#define METALSHARP_DXMT_I386_SURFACE_ID "dxmt-preserved-i386-22dacc4d-v1"

typedef enum {
    METALSHARP_BOTTLE_ARCH_X86_64,
    METALSHARP_BOTTLE_ARCH_I386,
} MetalsharpBottleArch;

typedef struct {
    const char* profile_id;
    const char* pipeline_id;
    const char* runtime_lane;
    MetalsharpBottleArch architecture;
    const char* surface_id;
    const char* manifest_sha256;
    const char* const* artifacts;
    size_t artifact_count;
    bool includes_d3d12;
} MetalsharpBottlePolicy;

const MetalsharpBottlePolicy* metalsharp_bottle_policy(const char* profile_id);
bool metalsharp_bottle_policy_valid(const MetalsharpBottlePolicy* policy);
bool metalsharp_bottle_artifact_required(const MetalsharpBottlePolicy* policy, const char* relative_path);
bool metalsharp_bottle_runtime_ready(const char* profile_id, const char* m12_root, size_t m12_root_len);

#endif
