/// @file DRMDetector.cpp
/// @brief DRM fingerprinting via byte-pattern signature matching across files and memory.
///
/// Maintains a table of 28 known DRM signatures (Denuvo, SteamStub, Arxan, VMProtect,
/// etc.) and scans game executables and mapped memory for matching byte sequences.
/// Returns a bitmask of detected DRM systems so the validation pipeline can assess
/// compatibility impact.

#include <cstring>
#include <fstream>
#include <metalsharp/DRMDetector.h>
#include <metalsharp/Logger.h>
#include <vector>

namespace metalsharp {

const DRMDetector::Signature DRMDetector::kSignatures[] = {
    {"Easy Anti-Cheat", "Anti-cheat", "EasyAntiCheat", 14, false, "blocked_pending_vendor_support"},
    {"BattlEye", "Anti-cheat", "BattlEye", 8, false, "blocked_pending_vendor_support"},
    {"Ricochet", "Anti-cheat", "Ricochet", 8, true, "unsupported_kernel_driver"},
    {"EA AntiCheat", "Anti-cheat", "EAAntiCheat", 11, true, "unsupported_kernel_driver"},
    {"ACE (Tencent)", "Anti-cheat", "AntiCheatExpert", 16, true, "unsupported_kernel_driver"},
    {"nProtect", "Anti-cheat", "nProtect", 8, true, "unsupported_kernel_driver"},
    {"GameGuard", "Anti-cheat", "GameGuard", 9, true, "unsupported_kernel_driver"},
    {"XIGNCODE3", "Anti-cheat", "XIGNCODE", 8, false, "user_mode_possible"},
    {"VAC", "Anti-cheat", "ValveAntiCheat", 14, false, "user_mode_possible"},
    {"VAC2", "Anti-cheat", "steamservice", 12, false, "user_mode_possible"},
    {"PunkBuster", "Anti-cheat", "PunkBuster", 10, false, "user_mode_possible"},

    {"Denuvo Anti-Tamper", "DRM", "Denuvo", 6, false, "user_mode_possible"},
    {"Denuvo Anti-Tamper", "DRM", "\x44\x65\x6E\x75\x76\x6F", 6, false, "user_mode_possible"},
    {"SecuROM", "DRM", "SecuROM", 7, false, "user_mode_possible"},
    {"SafeDisc", "DRM", "SafeDisc", 8, false, "user_mode_possible"},
    {"VMProtect", "DRM", "VMProtect", 9, false, "user_mode_possible"},
    {"Themida", "DRM", "Themida", 7, false, "user_mode_possible"},
    {"Arxan", "DRM", "Arxan", 5, false, "user_mode_possible"},
    {"Steam Stub", "DRM", "Steamstub", 9, false, "user_mode_possible"},
    {"Steam Stub", "DRM", "Steam.dll", 9, false, "user_mode_possible"},
    {"CEG (Custom Executable Generation)", "DRM", "CEG", 3, false, "user_mode_possible"},

    {".NET", "Runtime", "mscoree.dll", 10, false, "runtime_marker"},
    {"Unity", "Engine", "UnityPlayer", 11, false, "runtime_marker"},
    {"Unreal Engine", "Engine", "UnrealEngine", 12, false, "runtime_marker"},
    {"Unreal Engine", "Engine", "UE4", 3, false, "runtime_marker"},
    {"Unreal Engine 5", "Engine", "UE5", 3, false, "runtime_marker"},
    {"Mono", "Runtime", "mono-", 5, false, "runtime_marker"},
    {"XNA", "Runtime", "XNA", 3, false, "runtime_marker"},
    {"FNA", "Runtime", "FNA", 3, false, "runtime_marker"},
    {"Godot", "Engine", "Godot", 5, false, "runtime_marker"},
};

DRMDetector& DRMDetector::instance() {
    static DRMDetector inst;
    return inst;
}

bool DRMDetector::matchPattern(const uint8_t* data, size_t dataLen, const char* pattern, size_t patternLen) const {
    if (patternLen > dataLen)
        return false;

    for (size_t i = 0; i <= dataLen - patternLen; i++) {
        if (memcmp(data + i, pattern, patternLen) == 0) {
            return true;
        }
    }
    return false;
}

std::vector<DRMDetection> DRMDetector::scanFile(const std::string& exePath) {
    std::ifstream file(exePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        MS_WARN("DRMDetector: cannot open %s", exePath.c_str());
        return {};
    }

    auto fileSize = file.tellg();
    if (fileSize <= 0 || fileSize > 500 * 1024 * 1024)
        return {};

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(data.data()), fileSize))
        return {};

    auto results = scanMemory(data.data(), data.size());

    MS_INFO("DRMDetector: scanned %s (%zu bytes), %zu detections", exePath.c_str(), data.size(), results.size());
    return results;
}

std::vector<DRMDetection> DRMDetector::scanMemory(const uint8_t* data, size_t size) {
    std::vector<DRMDetection> results;
    if (!data || size == 0)
        return results;

    size_t sigCount = sizeof(kSignatures) / sizeof(kSignatures[0]);

    for (size_t i = 0; i < sigCount; i++) {
        const auto& sig = kSignatures[i];

        bool found = matchPattern(data, size, sig.bytePattern, sig.patternLen);

        bool alreadyDetected = false;
        for (const auto& r : results) {
            if (r.name == sig.name && r.type == sig.type) {
                alreadyDetected = true;
                break;
            }
        }
        if (alreadyDetected)
            continue;

        DRMDetection det;
        det.name = sig.name;
        det.type = sig.type;
        det.detected = found;
        det.confidence = found ? 0.9f : 0.0f;
        det.evidence = found ? ("matched signature: " + std::string(sig.bytePattern, sig.patternLen)) : "";
        det.supportStatus = sig.supportStatus;
        det.kernelOnly = sig.kernelOnly;
        results.push_back(det);
    }

    return results;
}

bool DRMDetector::hasKernelAntiCheat(const std::vector<DRMDetection>& results) const {
    for (const auto& r : results) {
        if (!r.detected)
            continue;
        if (r.type != "Anti-cheat")
            continue;

        size_t sigCount = sizeof(kSignatures) / sizeof(kSignatures[0]);
        for (size_t i = 0; i < sigCount; i++) {
            if (kSignatures[i].name == r.name && kSignatures[i].kernelOnly) {
                return true;
            }
        }
    }
    return false;
}

bool DRMDetector::requiresRuntimeProof(const std::vector<DRMDetection>& results) const {
    for (const auto& r : results) {
        if (!r.detected)
            continue;
        if (r.supportStatus == "blocked_pending_vendor_support" || r.supportStatus == "user_mode_possible") {
            return true;
        }
    }
    return false;
}

bool DRMDetector::isCompatible(const std::vector<DRMDetection>& results) const {
    if (hasKernelAntiCheat(results))
        return false;

    for (const auto& r : results) {
        if (!r.detected)
            continue;
        if (r.supportStatus == "blocked_pending_vendor_support")
            return false;
    }

    return true;
}

std::string DRMDetector::summary(const std::vector<DRMDetection>& results) const {
    std::string s;
    for (const auto& r : results) {
        if (!r.detected)
            continue;
        if (!s.empty())
            s += ", ";
        s += r.name + " [" + r.type + "; " + r.supportStatus + "]";
    }
    return s.empty() ? "No DRM/anti-cheat detected" : s;
}

} // namespace metalsharp
