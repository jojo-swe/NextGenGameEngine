#include "engine/rhi/common/rhi_frame_timeline.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace nge::rhi {

void FrameTimelineManager::Init(const FrameTimelineConfig& config) {
    m_config = config;
    m_history.reserve(config.historySize);
    m_frameActive = false;
    m_current = {};
}

void FrameTimelineManager::Shutdown() {
    m_history.clear();
    m_frameActive = false;
}

void FrameTimelineManager::BeginCpuFrame(u64 frameNumber) {
    std::lock_guard lock(m_mutex);
    m_frameStartTime = Clock::now();
    m_current = {};
    m_current.frameNumber = frameNumber;
    m_current.cpuStartMs = 0.0;
    m_frameActive = true;

    TimelineEvent evt;
    evt.type = TimelineEventType::CpuFrameBegin;
    evt.timestampMs = 0.0;
    evt.label = "CPU Begin";
    evt.queueIndex = 0;
    m_current.events.push_back(evt);
}

void FrameTimelineManager::EndCpuFrame() {
    std::lock_guard lock(m_mutex);
    if (!m_frameActive) return;

    m_current.cpuEndMs = MsFromStart(Clock::now());
    m_current.cpuDurationMs = m_current.cpuEndMs - m_current.cpuStartMs;

    TimelineEvent evt;
    evt.type = TimelineEventType::CpuFrameEnd;
    evt.timestampMs = m_current.cpuEndMs;
    evt.label = "CPU End";
    evt.queueIndex = 0;
    m_current.events.push_back(evt);
}

void FrameTimelineManager::MarkCpuSubmit() {
    std::lock_guard lock(m_mutex);
    if (!m_frameActive) return;

    TimelineEvent evt;
    evt.type = TimelineEventType::CpuSubmit;
    evt.timestampMs = MsFromStart(Clock::now());
    evt.label = "Submit";
    evt.queueIndex = 0;
    m_current.events.push_back(evt);
}

void FrameTimelineManager::SetGpuFrameTime(f64 startMs, f64 endMs) {
    std::lock_guard lock(m_mutex);
    m_current.gpuStartMs = startMs;
    m_current.gpuEndMs = endMs;
    m_current.gpuDurationMs = endMs - startMs;

    TimelineEvent evtStart;
    evtStart.type = TimelineEventType::GpuFrameBegin;
    evtStart.timestampMs = startMs;
    evtStart.label = "GPU Begin";
    evtStart.queueIndex = 0;
    m_current.events.push_back(evtStart);

    TimelineEvent evtEnd;
    evtEnd.type = TimelineEventType::GpuFrameEnd;
    evtEnd.timestampMs = endMs;
    evtEnd.label = "GPU End";
    evtEnd.queueIndex = 0;
    m_current.events.push_back(evtEnd);
}

void FrameTimelineManager::AddGpuPassTime(const std::string& passName, f64 startMs, f64 endMs, u32 queueIndex) {
    std::lock_guard lock(m_mutex);

    TimelineEvent evtStart;
    evtStart.type = TimelineEventType::GpuPassBegin;
    evtStart.timestampMs = startMs;
    evtStart.label = passName;
    evtStart.queueIndex = queueIndex;
    m_current.events.push_back(evtStart);

    TimelineEvent evtEnd;
    evtEnd.type = TimelineEventType::GpuPassEnd;
    evtEnd.timestampMs = endMs;
    evtEnd.label = passName;
    evtEnd.queueIndex = queueIndex;
    m_current.events.push_back(evtEnd);
}

void FrameTimelineManager::MarkPresent() {
    std::lock_guard lock(m_mutex);
    if (!m_frameActive) return;

    m_current.presentMs = MsFromStart(Clock::now());

    TimelineEvent evt;
    evt.type = TimelineEventType::Present;
    evt.timestampMs = m_current.presentMs;
    evt.label = "Present";
    evt.queueIndex = 0;
    m_current.events.push_back(evt);
}

void FrameTimelineManager::EndFrame() {
    std::lock_guard lock(m_mutex);
    if (!m_frameActive) return;

    m_current.frameDurationMs = MsFromStart(Clock::now());

    // Calculate overlap: time when both CPU and GPU are working
    f64 cpuStart = m_current.cpuStartMs;
    f64 cpuEnd = m_current.cpuEndMs;
    f64 gpuStart = m_current.gpuStartMs;
    f64 gpuEnd = m_current.gpuEndMs;

    f64 overlapStart = std::max(cpuStart, gpuStart);
    f64 overlapEnd = std::min(cpuEnd, gpuEnd);
    m_current.overlapMs = std::max(0.0, overlapEnd - overlapStart);

    m_current.cpuBound = m_current.cpuDurationMs > m_current.gpuDurationMs;

    // Sort events by timestamp
    std::sort(m_current.events.begin(), m_current.events.end(),
        [](const TimelineEvent& a, const TimelineEvent& b) {
            return a.timestampMs < b.timestampMs;
        });

    // Add to history
    m_history.push_back(m_current);
    if (m_history.size() > m_config.historySize) {
        m_history.erase(m_history.begin());
    }

    m_frameActive = false;
}

std::vector<FrameTimeline> FrameTimelineManager::GetHistory(u32 count) const {
    std::lock_guard lock(m_mutex);
    if (count == 0 || count >= m_history.size()) return m_history;
    return std::vector<FrameTimeline>(m_history.end() - count, m_history.end());
}

const FrameTimeline& FrameTimelineManager::GetLatestFrame() const {
    std::lock_guard lock(m_mutex);
    if (m_history.empty()) {
        static FrameTimeline empty{};
        return empty;
    }
    return m_history.back();
}

FramePacingStats FrameTimelineManager::GetPacingStats() const {
    std::lock_guard lock(m_mutex);
    FramePacingStats stats{};
    stats.targetFrameTimeMs = 1000.0 / m_config.targetFps;

    if (m_history.empty()) return stats;

    std::vector<f64> frameTimes;
    frameTimes.reserve(m_history.size());
    for (const auto& frame : m_history) {
        frameTimes.push_back(frame.frameDurationMs);
    }

    stats.avgFrameTimeMs = std::accumulate(frameTimes.begin(), frameTimes.end(), 0.0) / frameTimes.size();
    stats.minFrameTimeMs = *std::min_element(frameTimes.begin(), frameTimes.end());
    stats.maxFrameTimeMs = *std::max_element(frameTimes.begin(), frameTimes.end());

    // Standard deviation
    f64 sumSqDiff = 0.0;
    for (f64 t : frameTimes) {
        f64 diff = t - stats.avgFrameTimeMs;
        sumSqDiff += diff * diff;
    }
    stats.stdDevMs = std::sqrt(sumSqDiff / frameTimes.size());

    // P99
    auto sorted = frameTimes;
    std::sort(sorted.begin(), sorted.end());
    size_t p99Idx = static_cast<size_t>(sorted.size() * 0.99);
    p99Idx = std::min(p99Idx, sorted.size() - 1);
    stats.p99FrameTimeMs = sorted[p99Idx];

    // Stutter and dropped frames
    f64 stutterThresholdMs = stats.avgFrameTimeMs * m_config.stutterThreshold;
    u32 stutterCount = 0;
    u32 droppedCount = 0;
    for (f64 t : frameTimes) {
        if (t > stutterThresholdMs) stutterCount++;
        if (t > stats.targetFrameTimeMs) droppedCount++;
    }
    stats.stutterPercent = static_cast<f64>(stutterCount) / frameTimes.size() * 100.0;
    stats.droppedFrames = droppedCount;

    return stats;
}

bool FrameTimelineManager::IsCpuBound() const {
    std::lock_guard lock(m_mutex);
    if (m_history.empty()) return false;

    // Check last few frames
    u32 cpuBoundCount = 0;
    u32 checkCount = std::min(static_cast<u32>(m_history.size()), 10u);
    for (u32 i = 0; i < checkCount; ++i) {
        if (m_history[m_history.size() - 1 - i].cpuBound) cpuBoundCount++;
    }
    return cpuBoundCount > checkCount / 2;
}

std::string FrameTimelineManager::GetBottleneckDescription() const {
    auto stats = GetPacingStats();
    auto latest = GetLatestFrame();

    if (latest.cpuDurationMs <= 0.01 && latest.gpuDurationMs <= 0.01) {
        return "No data";
    }

    if (IsCpuBound()) {
        return "CPU-bound (CPU: " + std::to_string(latest.cpuDurationMs) +
               "ms, GPU: " + std::to_string(latest.gpuDurationMs) + "ms)";
    }

    return "GPU-bound (CPU: " + std::to_string(latest.cpuDurationMs) +
           "ms, GPU: " + std::to_string(latest.gpuDurationMs) + "ms)";
}

f64 FrameTimelineManager::MsFromStart(TimePoint tp) const {
    auto duration = tp - m_frameStartTime;
    return std::chrono::duration<f64, std::milli>(duration).count();
}

} // namespace nge::rhi
