#pragma once

#include <metalsharp/Platform.h>
#include <cstdint>
#include <functional>
#include <vector>

namespace metalsharp {

struct FrameTiming {
    double targetFrameTime;
    double lastFrameTime;
    double averageFrameTime;
    uint64_t frameCount;
    uint32_t currentFPS;
    uint32_t averageFPS;
};

enum class PresentMode {
    VSync,
    Immediate,
    HalfRateVSync,
    Adaptive,
};

class FramePacer {
public:
    static FramePacer& instance();

    void init(double targetFPS = 60.0);
    void shutdown();

    void beginFrame();
    void endFrame();
    void waitForVSync();

    double computePresentTime() const;

    FrameTiming getTiming() const;
    void setTargetFPS(double fps);
    double targetFPS() const { return m_targetFPS; }
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    void setPresentCallback(std::function<void()> callback);

    void setPresentMode(PresentMode mode);
    PresentMode presentMode() const { return m_presentMode; }

    void setFrameHistorySize(uint32_t size) { m_frameTimeHistory.resize(size, 0); }
    double getFrameTimePercentile(double percentile) const;

    uint32_t tripleBufferCount() const { return m_tripleBufferCount; }
    void setTripleBufferCount(uint32_t count) { m_tripleBufferCount = count; }

private:
    FramePacer() = default;

    double m_targetFPS = 60.0;
    double m_targetFrameTime = 1.0 / 60.0;
    bool m_initialized = false;
    bool m_enabled = true;
    uint64_t m_frameCount = 0;
    double m_lastFrameStart = 0;
    double m_lastFrameEnd = 0;
    double m_frameTimeAccum = 0;
    double m_fpsAccum = 0;
    uint64_t m_fpsFrameCount = 0;
    uint32_t m_currentFPS = 0;
    PresentMode m_presentMode = PresentMode::VSync;
    uint32_t m_tripleBufferCount = 3;
    std::vector<double> m_frameTimeHistory;
    std::function<void()> m_presentCallback;
};

}
