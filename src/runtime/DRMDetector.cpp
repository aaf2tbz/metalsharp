#include <metalsharp/DRMDetector.h>
#include <metalsharp/Logger.h>
#include <fstream>
#include <vector>
#include <cstring>

namespace metalsharp {

const DRMDetector::Signature DRMDetector::kSignatures[] = {
    {"Easy Anti-Cheat", "Anti-cheat", "EasyAntiCheat", 14, true},
    {"BattlEye", "Anti-cheat", "BattlEye", 8, true},
    {"Ricochet", "Anti-cheat", "Ricochet", 8, true},
    {"EA AntiCheat", "Anti-cheat", "EAAntiCheat", 11, true},
    {"ACE (Tencent)", "Anti-cheat", "AntiCheatExpert", 16, true},
    {"nProtect", "Anti-cheat", "nProtect", 8, true},
    {"GameGuard", "Anti-cheat", "GameGuard", 9, true},
    {"XIGNCODE3", "Anti-cheat", "XIGNCODE", 8, true},
    {"VAC", "Anti-cheat", "ValveAntiCheat", 14, false},
    {"VAC2", "Anti-cheat", "steamservice", 12, false},
    {"PunkBuster", "Anti-cheat", "PunkBuster", 10, false},

    {"Denuvo Anti-Tamper", "DRM", "Denuvo", 6, false},
    {"Denuvo Anti-Tamper", "DRM", "\x44\x65\x6E\x75\x76\x6F", 6, false},
    {"SecuROM", "DRM", "SecuROM", 7, false},
    {"SafeDisc", "DRM", "SafeDisc", 8, false},
    {"VMProtect", "DRM", "VMProtect", 9, false},
    {"Themida", "DRM", "Themida", 7, false},
    {"Arxan", "DRM", "Arxan", 5, false},
    {"Steam Stub", "DRM", "Steamstub", 9, false},
    {"Steam Stub", "DRM", "Steam.dll", 9, false},
    {"CEG (Custom Executable Generation)", "DRM", "CEG", 3, false},

    {".NET", "Runtime", "mscoree.dll", 10, false},
    {"Unity", "Engine", "UnityPlayer", 11, false},
    {"Unreal Engine", "Engine", "UnrealEngine", 12, false},
    {"Unreal Engine", "Engine", "UE4", 3, false},
    {"Unreal Engine 5", "Engine", "UE5", 3, false},
    {"Mono", "Runtime", "mono-", 5, false},
    {"XNA", "Runtime", "XNA", 3, false},
    {"FNA", "Runtime", "FNA", 3, false},
    {"Godot", "Engine", "Godot", 5, false},
};

DRMDetector& DRMDetector::instance() {
    static DRMDetector inst;
    return inst;
}

bool DRMDetector::matchPattern(const uint8_t* data, size_t dataLen,
                                 const char* pattern, size_t patternLen) const {
    if (patternLen > dataLen) return false;

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
    if (fileSize <= 0 || fileSize > 500 * 1024 * 1024) return {};

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(data.data()), fileSize)) return {};

    auto results = scanMemory(data.data(), data.size());

    MS_INFO("DRMDetector: scanned %s (%zu bytes), %zu detections",
            exePath.c_str(), data.size(), results.size());
    return results;
}

std::vector<DRMDetection> DRMDetector::scanMemory(const uint8_t* data, size_t size) {
    std::vector<DRMDetection> results;
    if (!data || size == 0) return results;

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
        if (alreadyDetected) continue;

        DRMDetection det;
        det.name = sig.name;
        det.type = sig.type;
        det.detected = found;
        det.confidence = found ? 0.9f : 0.0f;
        det.evidence = found ? ("matched signature: " + std::string(sig.bytePattern, sig.patternLen)) : "";
        results.push_back(det);
    }

    return results;
}

bool DRMDetector::hasKernelAntiCheat(const std::vector<DRMDetection>& results) const {
    for (const auto& r : results) {
        if (!r.detected) continue;
        if (r.type != "Anti-cheat") continue;

        size_t sigCount = sizeof(kSignatures) / sizeof(kSignatures[0]);
        for (size_t i = 0; i < sigCount; i++) {
            if (kSignatures[i].name == r.name && kSignatures[i].kernelLevel) {
                return true;
            }
        }
    }
    return false;
}

bool DRMDetector::isCompatible(const std::vector<DRMDetection>& results) const {
    return !hasKernelAntiCheat(results);
}

std::string DRMDetector::summary(const std::vector<DRMDetection>& results) const {
    std::string s;
    for (const auto& r : results) {
        if (!r.detected) continue;
        if (!s.empty()) s += ", ";
        s += r.name + " [" + r.type + "]";
    }
    return s.empty() ? "No DRM/anti-cheat detected" : s;
}

}
