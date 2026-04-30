#pragma once

#include <metalsharp/Platform.h>
#include <string>
#include <vector>
#include <cstdint>

namespace metalsharp {

struct DRMDetection {
    std::string name;
    std::string type;
    bool detected;
    float confidence;
    std::string evidence;
};

class DRMDetector {
public:
    static DRMDetector& instance();

    std::vector<DRMDetection> scanFile(const std::string& exePath);
    std::vector<DRMDetection> scanMemory(const uint8_t* data, size_t size);

    bool hasKernelAntiCheat(const std::vector<DRMDetection>& results) const;
    bool isCompatible(const std::vector<DRMDetection>& results) const;
    std::string summary(const std::vector<DRMDetection>& results) const;

private:
    DRMDetector() = default;

    struct Signature {
        const char* name;
        const char* type;
        const char* bytePattern;
        size_t patternLen;
        bool kernelLevel;
    };

    static const Signature kSignatures[];

    bool matchPattern(const uint8_t* data, size_t dataLen,
                       const char* pattern, size_t patternLen) const;
};

}
