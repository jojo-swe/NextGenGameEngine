#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/renderer/pipeline/gpu_scene_buffer.h"
#include "engine/renderer/pipeline/occlusion_feedback.h"
#include <mutex>

namespace nge::renderer {

// ─── Indirect Cull Pipeline ──────────────────────────────────────────────
// Orchestrates the full GPU-driven culling pipeline:
//   1. Frustum cull (compute)
//   2. HZB occlusion cull early pass (against prev-frame HZB)
//   3. Rasterize early survivors → build current HZB
//   4. HZB occlusion cull late pass (disocclusion recovery)
//   5. Build indirect draw/dispatch arguments
//   6. Readback visibility for CPU feedback
//
// This ties together the GPU scene buffer, occlusion feedback,
// HiZ builder, and indirect command generation.

struct IndirectCullConfig {
    u32 maxInstances = 65536;
    u32 maxDrawCommands = 65536;
    bool enableEarlyLateCull = true;
    bool enableFrustumCull = true;
    bool enableOcclusionCull = true;
    bool enableFeedbackReadback = true;
};

struct IndirectCullStats {
    u32 totalInstances;
    u32 frustumVisible;
    u32 earlyPassVisible;
    u32 latePassRecovered;
    u32 finalVisible;
    u32 drawCommandsGenerated;
    f32 cullEfficiency; // 1 - (finalVisible / total)
};

class IndirectCullPipeline {
public:
    bool Init(rhi::IDevice* device, const IndirectCullConfig& config = {});
    void Shutdown();

    // Per-frame execution
    void BeginFrame(rhi::ICommandList* cmd, u32 frameIndex);

    // Set scene data for this frame
    void SetSceneBuffer(const GPUSceneBuffer* sceneBuffer);

    // Execute frustum + early occlusion cull pass
    void ExecuteEarlyCull(rhi::ICommandList* cmd);

    // Build HiZ from early pass depth (call after rasterizing early survivors)
    void BuildHiZ(rhi::ICommandList* cmd);

    // Execute late occlusion cull pass (disocclusion recovery)
    void ExecuteLateCull(rhi::ICommandList* cmd);

    // Generate indirect draw arguments from visible list
    void BuildDrawArgs(rhi::ICommandList* cmd);

    // Readback visibility results to CPU
    void ReadbackVisibility(rhi::ICommandList* cmd);

    void EndFrame(rhi::ICommandList* cmd);

    // Query
    rhi::BufferHandle GetVisibleInstanceBuffer() const { return m_visibleBuffer; }
    rhi::BufferHandle GetDrawArgsBuffer() const { return m_drawArgsBuffer; }
    rhi::BufferHandle GetCounterBuffer() const { return m_counterBuffer; }

    u32 GetVisibleCount() const { return m_lastVisibleCount; }
    IndirectCullStats GetStats() const;

    OcclusionFeedback& GetOcclusionFeedback() { return m_occlusionFeedback; }

private:
    rhi::IDevice* m_device = nullptr;
    IndirectCullConfig m_config;

    // GPU buffers
    rhi::BufferHandle m_visibleBuffer;     // Visible instance indices
    rhi::BufferHandle m_occludedBuffer;    // Early-pass occluded (deferred to late)
    rhi::BufferHandle m_drawArgsBuffer;    // Indirect draw arguments
    rhi::BufferHandle m_counterBuffer;     // Atomic counters
    rhi::BufferHandle m_hizTexture;        // HiZ pyramid (managed separately)

    // Compute pipelines
    rhi::PipelineHandle m_frustumCullPipeline;
    rhi::PipelineHandle m_earlyCullPipeline;
    rhi::PipelineHandle m_lateCullPipeline;
    rhi::PipelineHandle m_buildArgsPipeline;
    rhi::PipelineHandle m_hizBuildPipeline;

    // Scene reference
    const GPUSceneBuffer* m_sceneBuffer = nullptr;

    // Feedback
    OcclusionFeedback m_occlusionFeedback;

    // Stats
    u32 m_lastVisibleCount = 0;
    IndirectCullStats m_stats{};
    mutable std::mutex m_mutex;
};

} // namespace nge::renderer
