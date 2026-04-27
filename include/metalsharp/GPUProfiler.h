#pragma once

#include <metalsharp/Platform.h>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <functional>

namespace metalsharp {

struct GPUCounter {
    std::string name;
    uint64_t value;
    double timestamp;
};

class GPUProfiler {
public:
    static GPUProfiler& instance();

    void init();
    void shutdown();

    void beginFrame();
    void endFrame();

    void beginPass(const std::string& name);
    void endPass(const std::string& name);

    void recordDraw(const std::string& passName, uint32_t vertexCount, uint32_t instanceCount);
    void recordCompute(const std::string& passName, uint32_t threadgroupsX, uint32_t threadgroupsY, uint32_t threadgroupsZ);

    struct FrameStats {
        double frameTime;
        double gpuTime;
        uint32_t drawCalls;
        uint32_t computeCalls;
        uint32_t triangles;
        uint64_t frameIndex;
        std::unordered_map<std::string, double> passTimes;
    };

    FrameStats getLastFrameStats() const;
    FrameStats getAverageStats(uint32_t numFrames) const;

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }
    void setMaxFrameHistory(uint32_t count) { m_maxHistory = count; }

    using StatsCallback = std::function<void(const FrameStats&)>;
    void setStatsCallback(StatsCallback callback);

private:
    GPUProfiler() = default;

    struct PassTimer {
        std::string name;
        double startTime;
        double gpuStartTime;
        double duration;
        uint32_t draws = 0;
        uint32_t computes = 0;
        uint32_t triangles = 0;
    };

    struct FrameRecord {
        double startTime;
        double endTime;
        std::vector<PassTimer> passes;
    };

    FrameRecord m_currentFrame;
    std::vector<FrameRecord> m_history;
    std::vector<PassTimer*> m_activePasses;
    bool m_initialized = false;
    bool m_enabled = false;
    uint32_t m_maxHistory = 60;
    uint64_t m_frameIndex = 0;
    StatsCallback m_callback;
};

}
