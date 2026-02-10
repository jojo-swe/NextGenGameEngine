#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Timestamp Query Pool Manager ────────────────────────────────────
// Manages frame-scoped GPU timestamp queries with a ring buffer for
// multi-frame latency. Provides per-pass GPU timing data.
//
// Use cases:
//   - Per-pass GPU timing for profiler overlay
//   - Frame-over-frame GPU time comparison
//   - Bottleneck detection (identify slowest passes)
//   - GPU time budget tracking

struct TimestampPair {
    u32         beginQuery;
    u32         endQuery;
    std::string passName;
    f64         gpuTimeMs;    // Resolved time in milliseconds
    bool        resolved;
};

struct TimestampFrame {
    u32                        frameIndex;
    std::vector<TimestampPair> pairs;
    bool                       submitted;
    bool                       resolved;
    f64                        totalGpuTimeMs;
};

struct TimestampQueryPoolConfig {
    u32  queriesPerFrame = 256;       // Max timestamp queries per frame
    u32  frameLatency = 3;            // Ring buffer depth (2-4 typical)
    f64  timestampPeriodNs = 1.0;     // Device timestamp period in nanoseconds
    bool enableMovingAverage = true;
    u32  movingAverageWindow = 30;    // Frames for smoothed timing
};

struct TimestampQueryPoolStats {
    u32 totalFramesProfiled;
    u32 totalPairsResolved;
    u32 activeFrame;
    u32 queriesUsedThisFrame;
    f64 lastFrameGpuTimeMs;
    f64 avgFrameGpuTimeMs;
    f64 peakFrameGpuTimeMs;
};

class TimestampQueryPool {
public:
    bool Init(const TimestampQueryPoolConfig& config = {});
    void Shutdown();

    // Begin a new frame of profiling
    void BeginFrame(u32 frameIndex);

    // End the current frame (submit queries)
    void EndFrame();

    // Insert a begin/end timestamp pair for a pass
    u32 BeginPass(const std::string& passName);
    void EndPass(u32 pairId);

    // Resolve queries from completed frames (call after GPU fence)
    void ResolveFrame(u32 frameIndex, const u64* queryResults, u32 resultCount);

    // Get resolved timing for a specific pass in the most recent resolved frame
    f64 GetPassTimeMs(const std::string& passName) const;

    // Get all resolved pass timings from the most recent resolved frame
    std::vector<std::pair<std::string, f64>> GetAllPassTimings() const;

    // Get the slowest pass from the most recent resolved frame
    std::string GetSlowestPass() const;

    // Get moving average for a pass
    f64 GetPassMovingAvgMs(const std::string& passName) const;

    // Get total GPU time for most recent resolved frame
    f64 GetLastFrameGpuTimeMs() const;

    // Reset all state
    void Reset();

    TimestampQueryPoolStats GetStats() const;

private:
    u32 GetRingIndex(u32 frameIndex) const;

    TimestampQueryPoolConfig m_config;
    std::vector<TimestampFrame> m_ringBuffer;

    u32 m_currentFrameRing = 0;
    u32 m_nextQueryIndex = 0;
    u32 m_totalFrames = 0;
    u32 m_totalPairs = 0;
    f64 m_peakGpuTime = 0.0;

    // Moving average tracking: passName -> recent timings
    std::unordered_map<std::string, std::vector<f64>> m_movingAvgData;

    // Most recent resolved frame index in ring
    i32 m_lastResolvedRing = -1;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
