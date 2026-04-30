#include <metalsharp/FramePacer.h>
#include <metalsharp/Logger.h>
#include <chrono>
#include <thread>
#include <cmath>
#include <algorithm>

namespace metalsharp {

FramePacer& FramePacer::instance() {
    static FramePacer inst;
    return inst;
}

void FramePacer::init(double targetFPS) {
    m_targetFPS = targetFPS;
    m_targetFrameTime = 1.0 / targetFPS;
    m_initialized = true;
    m_frameCount = 0;
    m_lastFrameStart = 0;
    m_lastFrameEnd = 0;
    m_frameTimeAccum = 0;
    m_currentFPS = 0;
    m_frameTimeHistory.resize(120, 0);
    MS_INFO("FramePacer initialized: target %.1f FPS (%.2f ms), present mode: %s",
            targetFPS, m_targetFrameTime * 1000.0,
            m_presentMode == PresentMode::VSync ? "VSync" :
            m_presentMode == PresentMode::Immediate ? "Immediate" :
            m_presentMode == PresentMode::HalfRateVSync ? "HalfRateVSync" : "Adaptive");
}

void FramePacer::shutdown() {
    m_initialized = false;
}

void FramePacer::beginFrame() {
    if (!m_initialized || !m_enabled) return;

    auto now = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
    m_lastFrameStart = ns.count() / 1e9;
}

void FramePacer::endFrame() {
    if (!m_initialized || !m_enabled) return;

    auto now = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
    m_lastFrameEnd = ns.count() / 1e9;

    double frameTime = m_lastFrameEnd - m_lastFrameStart;
    m_frameTimeAccum += frameTime;
    m_frameCount++;

    if (m_frameCount <= m_frameTimeHistory.size()) {
        m_frameTimeHistory[m_frameCount - 1] = frameTime;
    } else {
        m_frameTimeHistory.erase(m_frameTimeHistory.begin());
        m_frameTimeHistory.push_back(frameTime);
    }

    m_fpsFrameCount++;
    m_fpsAccum += frameTime;

    if (m_fpsAccum >= 1.0) {
        m_currentFPS = static_cast<uint32_t>(m_fpsFrameCount / m_fpsAccum);
        m_fpsFrameCount = 0;
        m_fpsAccum = 0;
    }

    if (m_presentCallback) {
        m_presentCallback();
    }
}

void FramePacer::waitForVSync() {
    if (!m_initialized || !m_enabled) return;
    if (m_presentMode == PresentMode::Immediate) return;

    double elapsed = m_lastFrameEnd - m_lastFrameStart;
    double remaining = m_targetFrameTime - elapsed;

    if (m_presentMode == PresentMode::HalfRateVSync) {
        remaining = (m_targetFrameTime * 2.0) - elapsed;
    }

    if (remaining > 0.0005) {
        auto sleepDuration = std::chrono::duration<double>(remaining * 0.75);
        std::this_thread::sleep_for(sleepDuration);
    }
}

double FramePacer::computePresentTime() const {
    if (m_presentMode == PresentMode::Immediate) return 0;

    double target = m_targetFrameTime;
    if (m_presentMode == PresentMode::HalfRateVSync) {
        target *= 2.0;
    }

    auto now = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
    double currentTime = ns.count() / 1e9;

    double nextVSync = std::ceil(currentTime / target) * target;
    return nextVSync;
}

FrameTiming FramePacer::getTiming() const {
    FrameTiming timing;
    timing.targetFrameTime = m_targetFrameTime;
    timing.lastFrameTime = m_frameCount > 0 ? (m_lastFrameEnd - m_lastFrameStart) : 0;
    timing.averageFrameTime = m_frameCount > 0 ? (m_frameTimeAccum / m_frameCount) : 0;
    timing.frameCount = m_frameCount;
    timing.currentFPS = m_currentFPS;
    timing.averageFPS = m_frameCount > 0 ? static_cast<uint32_t>(m_frameCount / m_frameTimeAccum) : 0;
    return timing;
}

void FramePacer::setTargetFPS(double fps) {
    m_targetFPS = fps;
    m_targetFrameTime = 1.0 / fps;
}

void FramePacer::setPresentCallback(std::function<void()> callback) {
    m_presentCallback = callback;
}

void FramePacer::setPresentMode(PresentMode mode) {
    m_presentMode = mode;
}

double FramePacer::getFrameTimePercentile(double percentile) const {
    if (m_frameTimeHistory.empty()) return 0;

    std::vector<double> sorted = m_frameTimeHistory;
    std::sort(sorted.begin(), sorted.end());

    size_t idx = static_cast<size_t>(percentile / 100.0 * (sorted.size() - 1));
    return sorted[idx];
}

}
