#pragma once

#include "engine/core/types.h"
#include <string>
#include <vector>
#include <mutex>

namespace nge::renderer {

// ─── Render Statistics Collector ─────────────────────────────────────────
// Aggregates frame-level GPU and CPU metrics for profiling, debug overlay,
// and telemetry. Maintains a rolling history for graph visualization.

struct FrameStats {
    // Timing (milliseconds)
    f64 cpuFrameTimeMs = 0;
    f64 gpuFrameTimeMs = 0;
    f64 presentTimeMs = 0;
    f64 renderGraphBuildMs = 0;
    f64 renderGraphExecuteMs = 0;
    f64 cullingTimeMs = 0;
    f64 shadowTimeMs = 0;
    f64 lightingTimeMs = 0;
    f64 postProcessTimeMs = 0;

    // Draw calls
    u32 drawCalls = 0;
    u32 dispatchCalls = 0;
    u32 trianglesRendered = 0;
    u32 instancesRendered = 0;
    u32 meshletsCulled = 0;
    u32 meshletsRendered = 0;

    // Memory
    u64 gpuMemoryUsedBytes = 0;
    u64 gpuMemoryBudgetBytes = 0;
    u64 stagingBytesUploaded = 0;
    u64 transientMemoryUsed = 0;
    u64 transientMemoryAliased = 0;

    // Resources
    u32 textureCount = 0;
    u32 bufferCount = 0;
    u32 pipelineCount = 0;
    u32 descriptorSetCount = 0;
    u32 renderPassCount = 0;

    // Lights
    u32 visibleLights = 0;
    u32 shadowCastingLights = 0;

    // Misc
    u32 frameNumber = 0;
    f32 fps = 0;
    f32 cpuUtilization = 0; // 0-1
};

class RenderStatsCollector {
public:
    struct Config {
        u32 historySize = 300;      // Frames of history (5 sec at 60 fps)
        bool enableGPUQueries = true;
        bool enableDetailedTimers = false;
    };

    bool Init(const Config& config = {});
    void Shutdown();

    // Per-frame lifecycle
    void BeginFrame(u32 frameNumber);
    void EndFrame();

    // Record metrics during the frame
    void SetCPUFrameTime(f64 ms) { m_current.cpuFrameTimeMs = ms; }
    void SetGPUFrameTime(f64 ms) { m_current.gpuFrameTimeMs = ms; }
    void SetPresentTime(f64 ms) { m_current.presentTimeMs = ms; }
    void SetRenderGraphBuildTime(f64 ms) { m_current.renderGraphBuildMs = ms; }
    void SetRenderGraphExecuteTime(f64 ms) { m_current.renderGraphExecuteMs = ms; }
    void SetCullingTime(f64 ms) { m_current.cullingTimeMs = ms; }
    void SetShadowTime(f64 ms) { m_current.shadowTimeMs = ms; }
    void SetLightingTime(f64 ms) { m_current.lightingTimeMs = ms; }
    void SetPostProcessTime(f64 ms) { m_current.postProcessTimeMs = ms; }

    void AddDrawCalls(u32 count) { m_current.drawCalls += count; }
    void AddDispatchCalls(u32 count) { m_current.dispatchCalls += count; }
    void AddTriangles(u32 count) { m_current.trianglesRendered += count; }
    void AddInstances(u32 count) { m_current.instancesRendered += count; }
    void SetMeshletStats(u32 culled, u32 rendered) {
        m_current.meshletsCulled = culled;
        m_current.meshletsRendered = rendered;
    }

    void SetGPUMemory(u64 used, u64 budget) {
        m_current.gpuMemoryUsedBytes = used;
        m_current.gpuMemoryBudgetBytes = budget;
    }
    void SetStagingUploaded(u64 bytes) { m_current.stagingBytesUploaded = bytes; }
    void SetTransientMemory(u64 used, u64 aliased) {
        m_current.transientMemoryUsed = used;
        m_current.transientMemoryAliased = aliased;
    }

    void SetResourceCounts(u32 textures, u32 buffers, u32 pipelines, u32 descriptors, u32 passes) {
        m_current.textureCount = textures;
        m_current.bufferCount = buffers;
        m_current.pipelineCount = pipelines;
        m_current.descriptorSetCount = descriptors;
        m_current.renderPassCount = passes;
    }

    void SetLightCounts(u32 visible, u32 shadowCasting) {
        m_current.visibleLights = visible;
        m_current.shadowCastingLights = shadowCasting;
    }

    void SetFPS(f32 fps) { m_current.fps = fps; }

    // Query
    const FrameStats& GetCurrentStats() const { return m_current; }
    const FrameStats& GetPreviousStats() const;

    // History for graph visualization
    const std::vector<FrameStats>& GetHistory() const { return m_history; }
    u32 GetHistorySize() const { return static_cast<u32>(m_history.size()); }

    // Averages over history
    f64 GetAverageCPUFrameTime() const;
    f64 GetAverageGPUFrameTime() const;
    f32 GetAverageFPS() const;
    u32 GetAverageDrawCalls() const;
    u32 GetAverageTriangles() const;

    // Peak values over history
    f64 GetPeakCPUFrameTime() const;
    f64 GetPeakGPUFrameTime() const;

    const Config& GetConfig() const { return m_config; }

private:
    Config m_config;
    FrameStats m_current;
    std::vector<FrameStats> m_history;
    u32 m_historyIndex = 0;
    bool m_historyFull = false;
    mutable std::mutex m_mutex;
};

} // namespace nge::renderer
