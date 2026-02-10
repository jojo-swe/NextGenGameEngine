#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Pipeline Statistics Aggregator ──────────────────────────────────
// Collects and aggregates per-pass GPU pipeline statistics from hardware
// query results. Tracks vertex/fragment invocations, clipping, tessellation,
// and compute dispatches with rolling history for trend analysis.
//
// Use cases:
//   - Per-pass pipeline statistics collection
//   - Rolling average over N frames for stable readouts
//   - Min/max/avg tracking per stat type
//   - Bottleneck detection (vertex-bound vs fragment-bound)
//   - CSV export for offline analysis

struct PipelineStatistics {
    u64 inputAssemblyVertices;
    u64 inputAssemblyPrimitives;
    u64 vertexShaderInvocations;
    u64 geometryShaderInvocations;
    u64 geometryShaderPrimitives;
    u64 clippingInvocations;
    u64 clippingPrimitives;
    u64 fragmentShaderInvocations;
    u64 tessControlPatches;
    u64 tessEvalInvocations;
    u64 computeShaderInvocations;
};

struct PassStatEntry {
    std::string passName;
    PipelineStatistics stats;
    u32 frameIndex;
};

struct AggregatedPassStats {
    std::string passName;
    PipelineStatistics min;
    PipelineStatistics max;
    PipelineStatistics avg;
    u32 sampleCount;
};

struct PipelineStatsConfig {
    u32  maxPasses = 128;
    u32  historyFrames = 120;     // Rolling window size
    bool trackMinMax = true;
    bool detectBottlenecks = true;
};

struct PipelineStatsAggregatorStats {
    u32 totalPasses;
    u32 totalFramesRecorded;
    u64 totalVertexInvocations;
    u64 totalFragmentInvocations;
    u64 totalComputeInvocations;
    float avgVertexPerFrame;
    float avgFragmentPerFrame;
    float vertexToFragmentRatio;
};

class PipelineStatsAggregator {
public:
    bool Init(const PipelineStatsConfig& config = {});
    void Shutdown();

    // Record pipeline statistics for a pass in the current frame
    void RecordPass(const std::string& passName, const PipelineStatistics& stats, u32 frameIndex);

    // Get aggregated stats for a specific pass (averaged over history)
    AggregatedPassStats GetPassStats(const std::string& passName) const;

    // Get latest stats for a pass
    PipelineStatistics GetLatestPassStats(const std::string& passName) const;

    // Get all pass names
    std::vector<std::string> GetPassNames() const;

    // Get frame totals (sum of all passes in a frame)
    PipelineStatistics GetFrameTotal(u32 frameIndex) const;

    // Detect if rendering is vertex-bound or fragment-bound
    // Returns >1.0 if fragment-bound, <1.0 if vertex-bound
    float GetBottleneckRatio(const std::string& passName) const;

    // Get pass count
    u32 GetPassCount() const;

    // Get total frames recorded
    u32 GetFrameCount() const;

    // Clear history for a specific pass
    void ClearPassHistory(const std::string& passName);

    void Reset();

    PipelineStatsAggregatorStats GetStats() const;

private:
    PipelineStatsConfig m_config;

    struct PassHistory {
        std::vector<PassStatEntry> entries;
        PipelineStatistics minStats;
        PipelineStatistics maxStats;
    };

    std::unordered_map<std::string, PassHistory> m_passHistories;
    u32 m_totalFrames = 0;

    mutable std::mutex m_mutex;

    void UpdateMinMax(PassHistory& history, const PipelineStatistics& stats);
    PipelineStatistics ComputeAverage(const PassHistory& history) const;
};

} // namespace nge::rhi
