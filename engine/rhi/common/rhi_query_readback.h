#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <functional>

namespace nge::rhi {

// ─── GPU Query Result Readback Manager ───────────────────────────────────
// Async collection of GPU query results (timestamp, occlusion, pipeline
// stats) with automatic readback buffer management and statistics
// aggregation. Triple-buffered to avoid stalls.
//
// Flow:
//   1. Register query groups (e.g., "ShadowPass", "GBufferPass")
//   2. Begin/end queries within passes
//   3. ReadbackResults() copies completed results from GPU readback buffer
//   4. Query aggregated statistics (min/max/avg over N frames)

enum class QueryType : u8 {
    Timestamp,
    Occlusion,
    BinaryOcclusion,
    PipelineStatistics,
};

struct QueryGroupDesc {
    std::string name;
    QueryType   type;
    u32         queryCount;
};

struct TimestampResult {
    u64    startTick;
    u64    endTick;
    f64    durationMs;
};

struct OcclusionResult {
    u64    samplesPassed;
    bool   visible;        // For binary occlusion
};

struct PipelineStatsResult {
    u64 inputAssemblyVertices;
    u64 inputAssemblyPrimitives;
    u64 vertexShaderInvocations;
    u64 fragmentShaderInvocations;
    u64 computeShaderInvocations;
    u64 clippingPrimitives;
};

struct QueryAggregation {
    f64 minMs;
    f64 maxMs;
    f64 avgMs;
    u32 sampleCount;
};

struct QueryReadbackConfig {
    u32 framesInFlight = 3;
    u32 maxQueryGroups = 64;
    u32 maxQueriesPerGroup = 256;
    u32 aggregationWindowFrames = 120;
    f64 timestampPeriodNs = 1.0;     // From device properties
};

struct QueryReadbackStats {
    u32 activeGroups;
    u32 totalQueries;
    u32 readbacksPending;
    u32 readbacksCompleted;
};

using QueryResultCallback = std::function<void(const std::string& groupName, u32 queryIndex, f64 durationMs)>;

class QueryReadbackManager {
public:
    bool Init(IDevice* device, const QueryReadbackConfig& config = {});
    void Shutdown();

    // Register a query group
    u32 RegisterGroup(const QueryGroupDesc& desc);

    // Begin/end a query within a group
    void BeginQuery(ICommandList* cmd, u32 groupId, u32 queryIndex);
    void EndQuery(ICommandList* cmd, u32 groupId, u32 queryIndex);

    // Write timestamp
    void WriteTimestamp(ICommandList* cmd, u32 groupId, u32 queryIndex);

    // Resolve queries to readback buffer (call after all queries in frame)
    void ResolveQueries(ICommandList* cmd);

    // Read back results from previous frame (call at frame start)
    void ReadbackResults(u64 frameNumber);

    // Get timestamp duration for a query pair (start, end)
    f64 GetTimestampMs(u32 groupId, u32 startQuery, u32 endQuery) const;

    // Get occlusion result
    OcclusionResult GetOcclusionResult(u32 groupId, u32 queryIndex) const;

    // Get pipeline stats
    PipelineStatsResult GetPipelineStats(u32 groupId, u32 queryIndex) const;

    // Get aggregated statistics over window
    QueryAggregation GetAggregation(u32 groupId, u32 queryIndex) const;

    // Register callback for results
    void OnResult(QueryResultCallback callback);

    // Per-frame update
    void BeginFrame(u64 frameNumber);
    void EndFrame();

    QueryReadbackStats GetStats() const;

private:
    struct QueryGroup {
        QueryGroupDesc desc;
        u32 heapOffset;          // Offset into query heap
        std::vector<u64> rawResults;
        std::vector<std::vector<f64>> aggregationWindow; // Per-query rolling history
    };

    struct FrameData {
        std::vector<bool> queryActive;
        bool resolved = false;
    };

    IDevice* m_device = nullptr;
    QueryReadbackConfig m_config;
    std::vector<QueryGroup> m_groups;
    std::vector<FrameData> m_frames;
    std::vector<QueryResultCallback> m_callbacks;

    u32 m_currentFrame = 0;
    u32 m_readbacksCompleted = 0;
    u64 m_frameNumber = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
