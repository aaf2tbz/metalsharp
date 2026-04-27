#include <metalsharp/GPUProfiler.h>
#include <metalsharp/Logger.h>
#include <chrono>
#include <algorithm>
#include <cstring>

namespace metalsharp {

GPUProfiler& GPUProfiler::instance() {
    static GPUProfiler inst;
    return inst;
}

void GPUProfiler::init() {
    m_initialized = true;
    m_frameIndex = 0;
    m_enabled = false;
    MS_INFO("GPUProfiler initialized (disabled by default, enable at runtime)");
}

void GPUProfiler::shutdown() {
    m_initialized = false;
    m_history.clear();
    m_activePasses.clear();
}

void GPUProfiler::beginFrame() {
    if (!m_initialized || !m_enabled) return;

    auto now = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());

    m_currentFrame = FrameRecord();
    m_currentFrame.startTime = ns.count() / 1e9;
}

void GPUProfiler::endFrame() {
    if (!m_initialized || !m_enabled) return;

    auto now = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
    m_currentFrame.endTime = ns.count() / 1e9;

    m_history.push_back(std::move(m_currentFrame));
    if (m_history.size() > m_maxHistory) {
        m_history.erase(m_history.begin());
    }

    m_frameIndex++;

    if (m_callback) {
        m_callback(getLastFrameStats());
    }
}

void GPUProfiler::beginPass(const std::string& name) {
    if (!m_initialized || !m_enabled) return;

    auto now = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());

    PassTimer timer;
    timer.name = name;
    timer.startTime = ns.count() / 1e9;
    m_currentFrame.passes.push_back(std::move(timer));
    m_activePasses.push_back(&m_currentFrame.passes.back());
}

void GPUProfiler::endPass(const std::string& name) {
    if (!m_initialized || !m_enabled) return;

    auto now = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());

    for (auto it = m_activePasses.rbegin(); it != m_activePasses.rend(); ++it) {
        if ((*it)->name == name) {
            (*it)->duration = (ns.count() / 1e9) - (*it)->startTime;
            m_activePasses.erase(std::next(it).base());
            break;
        }
    }
}

void GPUProfiler::recordDraw(const std::string& passName, uint32_t vertexCount, uint32_t instanceCount) {
    if (!m_initialized || !m_enabled) return;

    for (auto* pass : m_activePasses) {
        if (pass->name == passName) {
            pass->draws++;
            pass->triangles += vertexCount / 3;
            return;
        }
    }
}

void GPUProfiler::recordCompute(const std::string& passName, uint32_t tgX, uint32_t tgY, uint32_t tgZ) {
    if (!m_initialized || !m_enabled) return;

    for (auto* pass : m_activePasses) {
        if (pass->name == passName) {
            pass->computes++;
            return;
        }
    }
}

GPUProfiler::FrameStats GPUProfiler::getLastFrameStats() const {
    FrameStats stats;
    stats.frameTime = 0;
    stats.gpuTime = 0;
    stats.drawCalls = 0;
    stats.computeCalls = 0;
    stats.triangles = 0;
    stats.frameIndex = 0;

    if (m_history.empty()) return stats;

    const auto& frame = m_history.back();
    stats.frameTime = frame.endTime - frame.startTime;
    stats.frameIndex = m_frameIndex;

    for (const auto& pass : frame.passes) {
        stats.drawCalls += pass.draws;
        stats.computeCalls += pass.computes;
        stats.triangles += pass.triangles;
        stats.passTimes[pass.name] = pass.duration;
    }

    return stats;
}

GPUProfiler::FrameStats GPUProfiler::getAverageStats(uint32_t numFrames) const {
    FrameStats avg;
    avg.frameTime = 0;
    avg.gpuTime = 0;
    avg.drawCalls = 0;
    avg.computeCalls = 0;
    avg.triangles = 0;
    avg.frameIndex = 0;

    uint32_t count = std::min(numFrames, static_cast<uint32_t>(m_history.size()));
    if (count == 0) return avg;

    auto start = m_history.end() - count;
    for (auto it = start; it != m_history.end(); ++it) {
        avg.frameTime += it->endTime - it->startTime;
        for (const auto& pass : it->passes) {
            avg.drawCalls += pass.draws;
            avg.computeCalls += pass.computes;
            avg.triangles += pass.triangles;
        }
    }

    avg.frameTime /= count;
    avg.drawCalls /= count;
    avg.computeCalls /= count;
    avg.triangles /= count;
    avg.frameIndex = m_frameIndex;
    return avg;
}

void GPUProfiler::setStatsCallback(StatsCallback callback) {
    m_callback = callback;
}

}
