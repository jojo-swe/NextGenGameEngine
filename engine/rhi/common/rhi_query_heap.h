#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::rhi {

// ─── GPU Query Heap Manager ──────────────────────────────────────────────
// Manages VkQueryPool for timestamp, occlusion, and pipeline statistics.
// Triple-buffered: queries written in frame N are read back in frame N+2.
//
// Timestamp queries: measure GPU pass durations (nanoseconds).
// Occlusion queries: count samples that pass depth test.
// Pipeline stats: vertex/fragment invocation counts.

enum class QueryType : u8 {
    Timestamp,
    Occlusion,
    PipelineStatistics,
};

struct TimestampResult {
    std::string name;
    f64         durationMs;
    u64         beginTick;
    u64         endTick;
};

struct OcclusionResult {
    std::string name;
    u64         samplesPassed;
};

struct PipelineStatsResult {
    u64 inputAssemblyVertices;
    u64 inputAssemblyPrimitives;
    u64 vertexShaderInvocations;
    u64 fragmentShaderInvocations;
    u64 computeShaderInvocations;
    u64 clippingPrimitives;
};

class QueryHeapManager {
public:
    struct Config {
        u32 maxTimestampQueries = 256;
        u32 maxOcclusionQueries = 128;
        u32 maxPipelineStatQueries = 16;
        u32 framesInFlight = 3;
    };

    // No default argument: Config's default member initializers cannot be
    // used in a default argument while the enclosing class is incomplete.
    bool Init(IDevice* device, const Config& config);
    bool Init(IDevice* device) { return Init(device, Config{}); }
    void Shutdown();

    // Per-frame lifecycle
    void BeginFrame(u32 frameIndex);
    void EndFrame(ICommandList* cmd);

    // Timestamp queries
    u32 BeginTimestamp(ICommandList* cmd, const std::string& name);
    void EndTimestamp(ICommandList* cmd, u32 queryId);

    // Occlusion queries
    u32 BeginOcclusion(ICommandList* cmd, const std::string& name);
    void EndOcclusion(ICommandList* cmd, u32 queryId);

    // Pipeline statistics
    u32 BeginPipelineStats(ICommandList* cmd);
    void EndPipelineStats(ICommandList* cmd, u32 queryId);

    // Results (from N-2 frames ago)
    const std::vector<TimestampResult>& GetTimestampResults() const { return m_timestampResults; }
    const std::vector<OcclusionResult>& GetOcclusionResults() const { return m_occlusionResults; }
    const PipelineStatsResult& GetPipelineStats() const { return m_pipelineStats; }

    // Convenience: find a timestamp by name
    f64 GetTimestampMs(const std::string& name) const;

    // GPU timestamp period (nanoseconds per tick)
    void SetTimestampPeriod(f64 nanosecondsPerTick) { m_timestampPeriod = nanosecondsPerTick; }

private:
    struct FrameQueries {
        u32 timestampCount = 0;
        u32 occlusionCount = 0;
        u32 pipelineStatCount = 0;

        struct TimestampEntry {
            std::string name;
            u32 beginIndex;
            u32 endIndex;
        };
        std::vector<TimestampEntry> timestampEntries;

        struct OcclusionEntry {
            std::string name;
            u32 queryIndex;
        };
        std::vector<OcclusionEntry> occlusionEntries;
    };

    void ReadbackResults(u32 frameIndex);

    IDevice* m_device = nullptr;
    Config m_config;
    u32 m_currentFrame = 0;
    f64 m_timestampPeriod = 1.0; // ns per tick

    // Per-frame query pools (VkQueryPool as u64)
    struct FramePool {
        u64 timestampPool = 0;
        u64 occlusionPool = 0;
        u64 pipelineStatPool = 0;
    };
    std::vector<FramePool> m_pools;
    std::vector<FrameQueries> m_frameQueries;

    // Readback results
    std::vector<TimestampResult> m_timestampResults;
    std::vector<OcclusionResult> m_occlusionResults;
    PipelineStatsResult m_pipelineStats{};

    std::mutex m_mutex;
};

} // namespace nge::rhi
