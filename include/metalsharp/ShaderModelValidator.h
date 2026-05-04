/// @file ShaderModelValidator.h
/// @brief Shader Model 6.6 compute shader validation and warning emitter.
///
/// Validates SM 6.6 compute shader parameters against Apple GPU constraints. Emits
/// warnings when compute shaders use derivatives, quad operations, or texture samples
/// with multi-dimensional threadgroup sizes (only 1D threadgroups are supported for
/// these operations on Metal). Also validates that requested wave sizes match Apple's
/// 32-lane wavefront. All checks are inline and compile-time conditional on SM version.

#pragma once

#include <metalsharp/Logger.h>
#include <cstdint>

namespace metalsharp {

class ShaderModelValidator {
public:
    struct ValidationWarning {
        uint32_t smVersion;
        const char* message;
    };

    static void validateComputeShader(
        uint32_t smVersion,
        uint32_t threadgroupSizeX,
        uint32_t threadgroupSizeY,
        uint32_t threadgroupSizeZ,
        bool usesDerivatives,
        bool usesQuadOps,
        bool usesTextureSamples
    ) {
        if (smVersion < 66) return;

        if (usesDerivatives && (threadgroupSizeY > 1 || threadgroupSizeZ > 1)) {
            MS_WARN("SM 6.6: Compute derivatives only supported with 1D threadgroup size "
                     "(got %ux%ux%u) — may produce incorrect results",
                     threadgroupSizeX, threadgroupSizeY, threadgroupSizeZ);
        }

        if (usesQuadOps && (threadgroupSizeY > 1 || threadgroupSizeZ > 1)) {
            MS_WARN("SM 6.6: Compute quad ops only supported with 1D threadgroup size "
                     "(got %ux%ux%u) — may produce incorrect results",
                     threadgroupSizeX, threadgroupSizeY, threadgroupSizeZ);
        }

        if (usesTextureSamples && (threadgroupSizeY > 1 || threadgroupSizeZ > 1)) {
            MS_WARN("SM 6.6: Compute texture samples only supported with 1D threadgroup size "
                     "(got %ux%ux%u) — may produce incorrect results",
                     threadgroupSizeX, threadgroupSizeY, threadgroupSizeZ);
        }
    }

    static void validateWaveSize(uint32_t smVersion, uint32_t requestedWaveSize) {
        if (smVersion < 66) return;

        if (requestedWaveSize != 32) {
            MS_WARN("SM 6.6: Wave size must be 32 on Apple GPU (requested %u) — "
                     "behavior undefined with other sizes", requestedWaveSize);
        }
    }
};

}
