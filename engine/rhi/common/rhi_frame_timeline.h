#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <chrono>
#include <mutex>

namespace nge::rhi {

// ─── GPU Frame Timeline Manager ──────────────────────────────────────────
// Tracks CPU/GPU overlap, frame pacing, and generates timeline data for
// visualization. Records when CPU and GPU work starts/ends each frame
// to diagnose bottlenecks (CPU-bound vs GPU-bound).
//
// Provides data for the profiler overlay's frame timeline graph.

enum class TimelineEventType : u8 {
    CpuFrameBegin,
    CpuFrameEnd,
    CpuSubmit,
    GpuFrameBegin,
    GpuFrameEnd,
    GpuPassBegin,
    GpuPassEnd,
    Present,
};

struct TimelineEvent {
    TimelineEventType type;
    f64               timestampMs;  // Relative to frame start
    std::string       label;
    u32               queueIndex;   // 0=graphics, 1=compute, 2=transfer
};

struct FrameTimeline {
    u64  frameNumber;
    f64  cpuStartMs;
    f64  cpuEndMs;
    f64  gpuStartMs;
    f64  gpuEndMs;
    f64  presentMs;
    f64  frameDurationMs;
    f64  cpuDurationMs;
    f64  gpuDurationMs;
    f64  overlapMs;             // CPU/GPU parallel execution time
    bool cpuBound;              // CPU duration > GPU duration
    std::vector<TimelineEvent> events;
};

struct FramePacingStats {
    f64 avgFrameTimeMs;
    f64 minFrameTimeMs;
    f64 maxFrameTimeMs;
    f64 stdDevMs;
    f64 p99FrameTimeMs;         // 99th percentile
    f64 stutterPercent;          // % of frames > 2x average
    u32 droppedFrames;           // Frames exceeding target
    f64 targetFrameTimeMs;
};

struct FrameTimelineConfig {
    u32 historySize = 300;       // Rolling history
    f64 targetFps = 60.0;
    f64 stutterThreshold = 2.0;  // Multiplier of avg to count as stutter
};

class FrameTimelineManager {
public:
    void Init(const FrameTimelineConfig& config = {});
    void Shutdown();

    // CPU-side markers
    void BeginCpuFrame(u64 frameNumber);
    void EndCpuFrame();
    void MarkCpuSubmit();

    // GPU-side markers (from timestamp queries)
    void SetGpuFrameTime(f64 startMs, f64 endMs);
    void AddGpuPassTime(const std::string& passName, f64 startMs, f64 endMs, u32 queueIndex = 0);

    // Present marker
    void MarkPresent();

    // Finalize the current frame timeline
    void EndFrame();

    // Get the latest N frame timelines
    std::vector<FrameTimeline> GetHistory(u32 count = 0) const;

    // Get the most recent frame
    const FrameTimeline& GetLatestFrame() const;

    // Frame pacing analysis
    FramePacingStats GetPacingStats() const;

    // Is the application currently CPU-bound?
    bool IsCpuBound() const;

    // Get the current bottleneck description
    std::string GetBottleneckDescription() const;

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    f64 MsFromStart(TimePoint tp) const;

    FrameTimelineConfig m_config;
    std::vector<FrameTimeline> m_history;
    FrameTimeline m_current;
    TimePoint m_frameStartTime;
    bool m_frameActive = false;
    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
