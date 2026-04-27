#pragma once

#include <metalsharp/Platform.h>
#include <cstdint>
#include <functional>

namespace metalsharp {

struct FrameTiming {
    double targetFrameTime;
    double lastFrameTime;
    double averageFrameTime;
    uint64_t frameCount;
    uint32_t currentFPS;
    uint32_t averageFPS;
};

class FramePacer {
public:
    static FramePacer& instance();

    void init(double targetFPS = 60.0);
    void shutdown();

    void beginFrame();
    void endFrame();
    void waitForVSync();

    FrameTiming getTiming() const;
    void setTargetFPS(double fps);
    double targetFPS() const { return m_targetFPS; }
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    void setPresentCallback(std::function<void()> callback);

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
    std::function<void()> m_presentCallback;
};

}
