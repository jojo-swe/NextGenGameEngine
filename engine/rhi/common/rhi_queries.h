#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>

namespace nge::rhi {

// ─── GPU Query System ────────────────────────────────────────────────────
// Provides GPU-side measurement: timestamps, occlusion queries, pipeline stats.
// Used for profiling render passes and conditional rendering.

// ─── Query Types ─────────────────────────────────────────────────────────

enum class QueryType : u8 {
    Timestamp,          // GPU timestamp (nanoseconds)
    Occlusion,          // Number of samples that passed depth/stencil test
    OcclusionBinary,    // Boolean: any sample passed?
    PipelineStatistics, // Vertex/primitive/fragment invocation counts
};

// ─── Query Pool ──────────────────────────────────────────────────────────

using QueryPoolHandle = u32;
inline constexpr QueryPoolHandle INVALID_QUERY_POOL = UINT32_MAX;

struct QueryPoolDesc {
    QueryType type = QueryType::Timestamp;
    u32       count = 64;
    std::string debugName;
};

// ─── Timestamp Result ────────────────────────────────────────────────────

struct TimestampResult {
    u64 beginTicks = 0;
    u64 endTicks = 0;
    f64 elapsedMs = 0; // Computed from tick delta and GPU timestamp period
};

// ─── Pipeline Statistics Result ──────────────────────────────────────────

struct PipelineStatsResult {
    u64 inputAssemblyVertices = 0;
    u64 inputAssemblyPrimitives = 0;
    u64 vertexShaderInvocations = 0;
    u64 fragmentShaderInvocations = 0;
    u64 computeShaderInvocations = 0;
    u64 clippingPrimitives = 0;
};

// ─── GPU Profiler (high-level wrapper) ───────────────────────────────────
// Tracks per-pass GPU timing across frames with rolling averages.

class GPUProfiler {
public:
    struct ScopeResult {
        std::string name;
        f64         avgMs = 0;
        f64         minMs = 0;
        f64         maxMs = 0;
        u32         depth = 0; // Nesting level
    };

    bool Init(u32 maxScopes = 128, u32 historyFrames = 60);
    void Shutdown();

    // Call at frame boundaries
    void BeginFrame();
    void EndFrame();

    // Scope timing — wraps BeginQuery/EndQuery on a timestamp pool
    void BeginScope(const std::string& name);
    void EndScope();

    // Read results (available next frame due to GPU latency)
    const std::vector<ScopeResult>& GetResults() const { return m_results; }

    // Total GPU frame time
    f64 GetFrameTimeMs() const { return m_frameTimeMs; }

private:
    struct ScopeEntry {
        std::string name;
        u32         beginQueryIdx;
        u32         endQueryIdx;
        u32         depth;
    };

    struct FrameData {
        std::vector<ScopeEntry> scopes;
        u32 queryCount = 0;
    };

    static constexpr u32 FRAME_LATENCY = 3; // Frames before results are available

    FrameData    m_frames[FRAME_LATENCY + 1];
    u32          m_currentFrame = 0;
    u32          m_historyFrames = 60;

    std::vector<ScopeResult> m_results;
    f64 m_frameTimeMs = 0;

    // Per-scope rolling history
    struct ScopeHistory {
        std::vector<f64> samples;
        u32 writeIdx = 0;
    };
    std::vector<ScopeHistory> m_history;

    u32 m_scopeDepth = 0;
    u32 m_maxScopes = 0;
    bool m_initialized = false;
};

} // namespace nge::rhi
