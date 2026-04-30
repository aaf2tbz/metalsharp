#pragma once

#include <cstdint>
#include <cstring>

namespace metalsharp {
namespace win32 {

struct AntiCheatEntry {
    const char* name;
    const char* type;
    bool kernelLevel;
    bool compatible;
    const char* notes;
};

static const AntiCheatEntry kAntiCheatDatabase[] = {
    {"Easy Anti-Cheat (EAC)",       "Anti-cheat",  true,  false, "Kernel driver integrity checks — requires real Windows kernel"},
    {"BattlEye",                     "Anti-cheat",  true,  false, "Kernel driver — similar to EAC, impossible without kernel access"},
    {"Valve Anti-Cheat (VAC)",       "Anti-cheat",  false, true,  "Signature-based, less aggressive — should work with PE loader"},
    {"Ricochet (CoD)",               "Anti-cheat",  true,  false, "Kernel driver — impossible"},
    {"PunkBuster",                   "Anti-cheat",  false, true,  "User-mode, older — should work"},
    {"nProtect GameGuard",           "Anti-cheat",  true,  false, "Kernel driver — impossible"},
    {"XignCode3",                    "Anti-cheat",  false, true,  "User-mode primarily — may work with timing shims"},
    {"Denuvo Anti-Tamper",           "DRM",         false, true,  "Code obfuscation + hardware checks — CPUID/MAC/disk shims needed"},
    {"Denuvo Anti-Cheat",            "Anti-cheat",  false, true,  "User-mode component of Denuvo — should work with shims"},
    {"Steam Stub DRM",               "DRM",         false, true,  "Basic Steam DRM wrapper — PE loader handles this"},
    {"SecuROM",                      "DRM",         false, true,  "Older DRM — may need registry entries"},
    {"SafeDisc",                     "DRM",         false, true,  "Legacy DRM — DEP-compatible shim needed"},
    {"VMProtect",                    "DRM",         false, true,  "Code virtualization — Rosetta 2 handles x86, timing shims help"},
    {"Themida/WinLicense",           "DRM",         false, true,  "Code protection — Rosetta 2 handles, may need anti-debug shims"},
    {"Arxan",                        "DRM",         false, true,  "Code obfuscation — similar to VMProtect"},
    {"Epic Online Services",         "Anti-cheat",  false, true,  "User-mode component — should work"},
    {"EA AntiCheat",                 "Anti-cheat",  true,  false, "Kernel driver — impossible"},
    {"ACE (Tencent)",                "Anti-cheat",  true,  false, "Kernel driver — impossible"},
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

inline size_t getCompatibleCount() {
    size_t count = 0;
    for (size_t i = 0; i < kAntiCheatEntryCount; ++i) {
        if (kAntiCheatDatabase[i].compatible) ++count;
    }
    return count;
}

inline size_t getIncompatibleCount() {
    size_t count = 0;
    for (size_t i = 0; i < kAntiCheatEntryCount; ++i) {
        if (!kAntiCheatDatabase[i].compatible) ++count;
    }
    return count;
}

}
}
