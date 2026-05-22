/// @file AntiCheatDB.h
/// @brief Static database of anti-cheat and DRM support status.
///
/// Compile-time constant table mapping known anti-cheat and DRM systems to their
/// support status under MetalSharp. This table is not a launch permission oracle:
/// anti-cheat support depends on vendor opt-in, shipped Unix/Mach-O module assets,
/// protected-launch logs, and runtime proof.

#pragma once

#include <cstdint>
#include <cstring>

namespace metalsharp {
namespace win32 {

struct AntiCheatEntry {
    const char* name;
    const char* type;
    bool kernelLevel;
    const char* status;
    const char* notes;
};

static const AntiCheatEntry kAntiCheatDatabase[] = {
    {"Easy Anti-Cheat (EAC)", "Anti-cheat", false, "blocked_pending_vendor_support",
     "Requires publisher/vendor enablement plus compatible Unix or vendor macOS module assets; do not treat string "
     "evidence as proof."},
    {"BattlEye", "Anti-cheat", false, "blocked_pending_vendor_support",
     "Requires title-specific BattlEye/Valve enablement plus compatible runtime assets; do not treat BERCon or DLL "
     "presence as service proof."},
    {"Valve Anti-Cheat (VAC)", "Anti-cheat", false, "user_mode_possible",
     "Signature/service based; still requires Steam runtime proof for the target title."},
    {"Ricochet (CoD)", "Anti-cheat", true, "unsupported_kernel_driver", "Kernel driver boundary remains unsupported."},
    {"PunkBuster", "Anti-cheat", false, "user_mode_possible",
     "Older user-mode/service stack; requires title-specific runtime proof."},
    {"nProtect GameGuard", "Anti-cheat", true, "unsupported_kernel_driver",
     "Kernel driver boundary remains unsupported."},
    {"XignCode3", "Anti-cheat", false, "user_mode_possible",
     "User-mode leaning, but requires timing/service proof per title."},
    {"Denuvo Anti-Tamper", "DRM", false, "user_mode_possible",
     "Code obfuscation plus hardware checks; CPUID/MAC/disk shims may be needed."},
    {"Denuvo Anti-Cheat", "Anti-cheat", false, "blocked_pending_vendor_support",
     "Requires protected-runtime evidence before launch claims."},
    {"Steam Stub DRM", "DRM", false, "user_mode_possible",
     "Basic Steam DRM wrapper; still needs Steam ownership/session proof."},
    {"SecuROM", "DRM", false, "user_mode_possible", "Older DRM; may need registry entries."},
    {"SafeDisc", "DRM", false, "user_mode_possible", "Legacy DRM; DEP-compatible shim may be needed."},
    {"VMProtect", "DRM", false, "user_mode_possible",
     "Code virtualization; Rosetta 2 handles x86, timing shims may help."},
    {"Themida/WinLicense", "DRM", false, "user_mode_possible", "Code protection; may need anti-debug shim evidence."},
    {"Arxan", "DRM", false, "user_mode_possible", "Code obfuscation; requires runtime proof."},
    {"Epic Online Services", "Anti-cheat", false, "blocked_pending_vendor_support",
     "EOS EAC support requires developer opt-in and module proof."},
    {"EA AntiCheat", "Anti-cheat", true, "unsupported_kernel_driver", "Kernel driver boundary remains unsupported."},
    {"ACE (Tencent)", "Anti-cheat", true, "unsupported_kernel_driver", "Kernel driver boundary remains unsupported."},
};

static constexpr size_t kAntiCheatEntryCount = sizeof(kAntiCheatDatabase) / sizeof(kAntiCheatDatabase[0]);

inline const AntiCheatEntry* findAntiCheat(const char* name) {
    for (size_t i = 0; i < kAntiCheatEntryCount; ++i) {
        if (strstr(kAntiCheatDatabase[i].name, name) != nullptr) {
            return &kAntiCheatDatabase[i];
        }
    }
    return nullptr;
}

inline size_t getStatusCount(const char* status) {
    size_t count = 0;
    for (size_t i = 0; i < kAntiCheatEntryCount; ++i) {
        if (strcmp(kAntiCheatDatabase[i].status, status) == 0)
            ++count;
    }
    return count;
}

inline size_t getRuntimeProofRequiredCount() {
    size_t count = 0;
    for (size_t i = 0; i < kAntiCheatEntryCount; ++i) {
        if (strcmp(kAntiCheatDatabase[i].status, "blocked_pending_vendor_support") == 0 ||
            strcmp(kAntiCheatDatabase[i].status, "user_mode_possible") == 0)
            ++count;
    }
    return count;
}

} // namespace win32
} // namespace metalsharp
