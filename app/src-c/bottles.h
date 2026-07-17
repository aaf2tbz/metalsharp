#ifndef METALSHARP_BOTTLES_H
#define METALSHARP_BOTTLES_H

#include <stdbool.h>
#include <stddef.h>

#define METALSHARP_DXMT_LEGACY_MANIFEST_SHA256 "1aec2fac2575e9855268a397aabbaa7ee07f2f5be069a582fdea71db72dd14e2"
#define METALSHARP_DXMT_M12_MANIFEST_SHA256 "138e417392ecd015f6c9ec14207cc3e73276ad011acefe714bf4b976bd4c8ba6"
#define METALSHARP_DXMT_MANIFEST_SHA256 METALSHARP_DXMT_M12_MANIFEST_SHA256
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

/*
 * Reconcile the DXMT deployment bound to a freshly saved bottle manifest.
 * On failure, promoted files are rolled back and the persisted bottle health
 * is changed to needs_repair before this function returns false.
 */
bool metalsharp_reconcile_bottle_manifest(const char* manifest_path, size_t manifest_path_len);

#endif
