#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/rhi/common/rhi_query_heap.h"
#include <vector>
#include <string>
#include <stack>

namespace nge::renderer {

// ─── GPU Timeline Profiler ───────────────────────────────────────────────
// Hierarchical scoped GPU timers that integrate with the query heap.
// Produces a tree of timing results suitable for profiler overlay display.
//
// Usage:
//   profiler.BeginFrame(cmd, frameIndex);
//   {
//     auto scope = profiler.Scope(cmd, "GBuffer");
//     // ... draw GBuffer
//     {
//       auto inner = profiler.Scope(cmd, "Opaque");
//       // ... draw opaque
//     }
//   }
//   profiler.EndFrame(cmd);
//   auto results = profiler.GetResults();

struct GPUTimerNode {
    std::string name;
    f64         durationMs = 0;
    u32         depth = 0;
    u32         parentIndex = UINT32_MAX;
    std::vector<u32> children;
    u32         queryId = UINT32_MAX;
};

class GPUTimerScope {
public:
    GPUTimerScope(class GPUTimerProfiler* profiler, rhi::ICommandList* cmd, u32 nodeIndex);
    ~GPUTimerScope();
    GPUTimerScope(const GPUTimerScope&) = delete;
    GPUTimerScope& operator=(const GPUTimerScope&) = delete;
    GPUTimerScope(GPUTimerScope&& other) noexcept;
    GPUTimerScope& operator=(GPUTimerScope&&) = delete;

private:
    GPUTimerProfiler* m_profiler = nullptr;
    rhi::ICommandList* m_cmd = nullptr;
    u32 m_nodeIndex = UINT32_MAX;
};

class GPUTimerProfiler {
public:
    struct Config {
        u32 maxTimers = 128;
        u32 framesInFlight = 3;
    };

    // No default argument: Config's default member initializers cannot be
    // used in a default argument while the enclosing class is incomplete.
    bool Init(rhi::IDevice* device, const Config& config);
    bool Init(rhi::IDevice* device) { return Init(device, Config{}); }
    void Shutdown();

    // Per-frame lifecycle
    void BeginFrame(rhi::ICommandList* cmd, u32 frameIndex);
    void EndFrame(rhi::ICommandList* cmd);

    // Scoped timer (RAII — ends when scope object destroyed)
    GPUTimerScope Scope(rhi::ICommandList* cmd, const std::string& name);

    // Manual begin/end (prefer Scope() instead)
    u32  BeginTimer(rhi::ICommandList* cmd, const std::string& name);
    void EndTimer(rhi::ICommandList* cmd, u32 timerIndex);

    // Results from N-2 frames ago
    const std::vector<GPUTimerNode>& GetResults() const { return m_results; }

    // Find a timer by name
    f64 GetTimerMs(const std::string& name) const;

    // Get total GPU frame time
    f64 GetTotalFrameMs() const;

    // Get flat list (depth-first order) for display
    std::vector<const GPUTimerNode*> GetFlatResults() const;

    u32 GetTimerCount() const { return static_cast<u32>(m_currentNodes.size()); }

private:
    friend class GPUTimerScope;

    void CollectResults();
    void FlattenDFS(u32 nodeIndex, std::vector<const GPUTimerNode*>& out) const;

    rhi::IDevice* m_device = nullptr;
    Config m_config;

    rhi::QueryHeapManager m_queryHeap;

    // Current frame's timer tree
    std::vector<GPUTimerNode> m_currentNodes;
    std::stack<u32> m_activeStack; // Stack of open timer indices
    u32 m_currentDepth = 0;

    // Results (from readback)
    std::vector<GPUTimerNode> m_results;
};

} // namespace nge::renderer
