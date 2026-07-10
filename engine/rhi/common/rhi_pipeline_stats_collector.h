#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Pipeline Statistics Collector ───────────────────────────────────
// Aggregates VkQueryPoolCreateInfo pipeline statistics queries across
// frames and draw calls. Provides per-pass and per-material stats for
// vertex/fragment invocation counts, clipping, tessellation, etc.
//
// Use cases:
//   - Identifying overdraw hotspots (fragment invocations vs pixels)
//   - Measuring geometry amplification (VS input vs clipped primitives)
//   - Tessellation factor analysis
//   - Compute shader occupancy tracking
//   - Per-pass cost attribution

struct CollectorPipelineStatistics {
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
    u64 meshShaderInvocations;
    u64 taskShaderInvocations;
};

struct PassStatistics {
    std::string passName;
    CollectorPipelineStatistics current;       // Current frame
    CollectorPipelineStatistics accumulated;   // Running total
    u32 sampleCount;                  // Frames sampled
    f32 overdrawRatio;                // fragment invocations / pixels
    f32 geometryAmplification;        // output prims / input prims
};

struct CollectorStatsConfig {
    u32  maxPasses = 64;
    u32  maxQueriesPerFrame = 128;
    u32  historyFrames = 60;          // Rolling average window
    bool enableMeshShaderStats = false;
};

struct PipelineStatsCollectorStats {
    u32 activePasses;
    u32 totalQueriesThisFrame;
    f32 averageOverdraw;
    f32 maxOverdraw;
    u64 totalFragmentInvocations;
    u64 totalVertexInvocations;
};

class PipelineStatsCollector {
public:
    bool Init(const CollectorStatsConfig& config = {});
    void Shutdown();

    // Begin/end statistics collection for a named pass
    u32 BeginPass(const std::string& passName);
    void EndPass(u32 queryId);

    // Submit readback results from GPU
    void SubmitResults(u32 queryId, const CollectorPipelineStatistics& stats);

    // End of frame: compute averages
    void EndFrame(u32 screenPixelCount);

    // Query
    const PassStatistics* GetPassStats(const std::string& passName) const;
    std::vector<PassStatistics> GetAllPassStats() const;

    // Get the pass with the worst overdraw
    std::string GetWorstOverdrawPass() const;

    // Reset accumulated stats
    void ResetAccumulators();

    PipelineStatsCollectorStats GetStats() const;

private:
    CollectorStatsConfig m_config;

    struct ActiveQuery {
        std::string passName;
        bool hasResults;
        CollectorPipelineStatistics results;
    };

    std::unordered_map<u32, ActiveQuery> m_activeQueries;
    std::unordered_map<std::string, PassStatistics> m_passStats;

    u32 m_nextQueryId = 0;
    u32 m_queriesThisFrame = 0;
    u32 m_lastPixelCount = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
