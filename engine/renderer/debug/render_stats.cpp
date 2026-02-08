#include "engine/renderer/debug/render_stats.h"
#include <algorithm>
#include <numeric>

namespace nge::renderer {

bool RenderStatsCollector::Init(const Config& config) {
    m_config = config;
    m_history.resize(config.historySize);
    m_historyIndex = 0;
    m_historyFull = false;
    m_current = {};
    return true;
}

void RenderStatsCollector::Shutdown() {
    m_history.clear();
}

void RenderStatsCollector::BeginFrame(u32 frameNumber) {
    m_current = {};
    m_current.frameNumber = frameNumber;
}

void RenderStatsCollector::EndFrame() {
    std::lock_guard lock(m_mutex);
    m_history[m_historyIndex] = m_current;
    m_historyIndex = (m_historyIndex + 1) % m_config.historySize;
    if (m_historyIndex == 0) m_historyFull = true;
}

const FrameStats& RenderStatsCollector::GetPreviousStats() const {
    u32 prevIdx = (m_historyIndex == 0 ? m_config.historySize : m_historyIndex) - 1;
    return m_history[prevIdx];
}

f64 RenderStatsCollector::GetAverageCPUFrameTime() const {
    std::lock_guard lock(m_mutex);
    u32 count = m_historyFull ? m_config.historySize : m_historyIndex;
    if (count == 0) return 0;
    f64 sum = 0;
    for (u32 i = 0; i < count; ++i) sum += m_history[i].cpuFrameTimeMs;
    return sum / count;
}

f64 RenderStatsCollector::GetAverageGPUFrameTime() const {
    std::lock_guard lock(m_mutex);
    u32 count = m_historyFull ? m_config.historySize : m_historyIndex;
    if (count == 0) return 0;
    f64 sum = 0;
    for (u32 i = 0; i < count; ++i) sum += m_history[i].gpuFrameTimeMs;
    return sum / count;
}

f32 RenderStatsCollector::GetAverageFPS() const {
    std::lock_guard lock(m_mutex);
    u32 count = m_historyFull ? m_config.historySize : m_historyIndex;
    if (count == 0) return 0;
    f32 sum = 0;
    for (u32 i = 0; i < count; ++i) sum += m_history[i].fps;
    return sum / static_cast<f32>(count);
}

u32 RenderStatsCollector::GetAverageDrawCalls() const {
    std::lock_guard lock(m_mutex);
    u32 count = m_historyFull ? m_config.historySize : m_historyIndex;
    if (count == 0) return 0;
    u64 sum = 0;
    for (u32 i = 0; i < count; ++i) sum += m_history[i].drawCalls;
    return static_cast<u32>(sum / count);
}

u32 RenderStatsCollector::GetAverageTriangles() const {
    std::lock_guard lock(m_mutex);
    u32 count = m_historyFull ? m_config.historySize : m_historyIndex;
    if (count == 0) return 0;
    u64 sum = 0;
    for (u32 i = 0; i < count; ++i) sum += m_history[i].trianglesRendered;
    return static_cast<u32>(sum / count);
}

f64 RenderStatsCollector::GetPeakCPUFrameTime() const {
    std::lock_guard lock(m_mutex);
    u32 count = m_historyFull ? m_config.historySize : m_historyIndex;
    f64 peak = 0;
    for (u32 i = 0; i < count; ++i) peak = std::max(peak, m_history[i].cpuFrameTimeMs);
    return peak;
}

f64 RenderStatsCollector::GetPeakGPUFrameTime() const {
    std::lock_guard lock(m_mutex);
    u32 count = m_historyFull ? m_config.historySize : m_historyIndex;
    f64 peak = 0;
    for (u32 i = 0; i < count; ++i) peak = std::max(peak, m_history[i].gpuFrameTimeMs);
    return peak;
}

} // namespace nge::renderer
