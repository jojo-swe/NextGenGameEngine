#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <mutex>

namespace nge::renderer {

// ─── GPU Occlusion Query Feedback System ─────────────────────────────────
// Reads back GPU occlusion culling results to the CPU for LOD decisions,
// streaming priority, and scene statistics. Uses a readback buffer with
// triple-buffering to avoid GPU stalls.
//
// Pipeline:
//   Frame N:   GPU writes visibility results to buffer
//   Frame N+2: CPU reads back results (2-frame latency)
//   CPU uses results for: LOD bias, streaming priority, cull stats

struct OcclusionResult {
    u32  instanceId;
    bool visible;
    u32  pixelCount;      // Approximate visible pixels (from occlusion query)
    f32  screenCoverage;  // Fraction of screen covered (0-1)
};

struct OcclusionFeedbackConfig {
    u32 maxInstances = 65536;
    u32 framesInFlight = 3;
};

struct OcclusionFeedbackStats {
    u32 totalInstances;
    u32 visibleInstances;
    u32 occludedInstances;
    f32 occlusionRate;       // occluded / total
    u32 frameLatency;
};

class OcclusionFeedback {
public:
    bool Init(rhi::IDevice* device, const OcclusionFeedbackConfig& config = {});
    void Shutdown();

    // Per-frame lifecycle
    void BeginFrame(u32 frameIndex);

    // Record GPU-side: write visibility results from culling pass
    void RecordResults(rhi::ICommandList* cmd, rhi::BufferHandle visibilityBuffer, u32 instanceCount);

    // Read back results from N-2 frames ago (call after BeginFrame)
    void ReadBack();

    // Query results
    const std::vector<OcclusionResult>& GetResults() const { return m_results; }
    bool IsVisible(u32 instanceId) const;
    f32  GetScreenCoverage(u32 instanceId) const;
    u32  GetVisibleCount() const { return m_visibleCount; }

    // Get LOD bias suggestion based on screen coverage
    f32 GetLODBias(u32 instanceId) const;

    // Get streaming priority (higher = more important to stream)
    f32 GetStreamingPriority(u32 instanceId) const;

    OcclusionFeedbackStats GetStats() const;

private:
    rhi::IDevice* m_device = nullptr;
    OcclusionFeedbackConfig m_config;

    // Per-frame readback buffers (triple-buffered)
    struct FrameData {
        rhi::BufferHandle readbackBuffer;
        u32               instanceCount = 0;
        bool              ready = false;
    };
    std::vector<FrameData> m_frames;

    u32 m_currentFrame = 0;

    // CPU-side results (from readback)
    std::vector<OcclusionResult> m_results;
    u32 m_visibleCount = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::renderer
