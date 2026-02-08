#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>

namespace nge::renderer {

// ─── GPU Culling Pipeline ────────────────────────────────────────────────
// Orchestrates the full GPU-driven culling pipeline:
//   1. Frustum + HZB occlusion cull (early pass, previous frame HZB)
//   2. Meshlet LOD selection (screen-space error)
//   3. Indirect draw argument build (MDI or mesh shader dispatch)
//   4. Render visible geometry → build new HZB
//   5. Late re-test (current frame HZB, catch disocclusion)
//   6. Second indirect draw for newly visible
//
// All passes run on the GPU with zero CPU readback in the critical path.

struct GPUCullingConfig {
    u32   maxMeshletGroups = 1 << 20;   // 1M meshlet groups
    u32   maxDrawCommands = 1 << 16;    // 64K draws
    u32   maxLODLevel = 7;
    f32   lodErrorThreshold = 1.0f;     // Pixels
    f32   occlusionThreshold = 0.0f;    // HZB conservative test margin
    bool  enableFrustumCull = true;
    bool  enableOcclusionCull = true;
    bool  enableLODSelection = true;
    bool  useMeshShaders = true;
};

struct GPUCullingFrameData {
    math::Mat4  viewProj;
    math::Mat4  prevViewProj;
    math::Vec4  cameraPos;
    math::Vec4  frustumPlanes[6];
    f32         screenHeight;
    f32         fovY;
    u32         meshletGroupCount;
};

struct GPUCullingStats {
    u32 totalMeshletGroups;
    u32 visibleAfterEarlyPass;
    u32 visibleAfterLatePass;
    u32 totalDrawCommands;
    u32 totalTriangles;
};

class GPUCullingPipeline {
public:
    bool Init(rhi::IDevice* device, const GPUCullingConfig& config = {});
    void Shutdown();

    // Per-frame execution
    void BeginFrame(const GPUCullingFrameData& frameData);

    // Pass 1: Early culling (frustum + previous HZB)
    void EarlyCull(rhi::ICommandList* cmd);

    // Pass 2: Meshlet LOD selection
    void SelectLODs(rhi::ICommandList* cmd);

    // Pass 3: Build indirect draw arguments
    void BuildDrawArgs(rhi::ICommandList* cmd);

    // Pass 4: Late re-test after HZB rebuild
    void LateCull(rhi::ICommandList* cmd);

    // Pass 5: Build draw args for late-visible geometry
    void BuildLateDrawArgs(rhi::ICommandList* cmd);

    // Reset draw counters (call before early cull)
    void ResetCounters(rhi::ICommandList* cmd);

    // Get indirect draw buffer for vkCmdDrawIndexedIndirect / vkCmdDrawMeshTasksIndirect
    rhi::BufferHandle GetDrawCommandBuffer() const { return m_drawCommandBuffer; }
    rhi::BufferHandle GetDrawCountBuffer() const { return m_drawCountBuffer; }
    rhi::BufferHandle GetLateDrawCommandBuffer() const { return m_lateDrawCommandBuffer; }
    rhi::BufferHandle GetLateDrawCountBuffer() const { return m_lateDrawCountBuffer; }

    // HZB texture (for binding to occlusion cull shader)
    void SetHZBTexture(rhi::TextureHandle hzb) { m_hzbTexture = hzb; }
    void SetPreviousHZBTexture(rhi::TextureHandle prevHzb) { m_prevHzbTexture = prevHzb; }

    // Meshlet group buffer (external, set by scene)
    void SetMeshletGroupBuffer(rhi::BufferHandle buffer) { m_meshletGroupBuffer = buffer; }

    // Stats (requires readback, 1-2 frame latency)
    GPUCullingStats GetStats() const { return m_stats; }

    const GPUCullingConfig& GetConfig() const { return m_config; }

private:
    rhi::IDevice* m_device = nullptr;
    GPUCullingConfig m_config;
    GPUCullingFrameData m_frameData;
    GPUCullingStats m_stats{};

    // GPU buffers
    rhi::BufferHandle m_meshletGroupBuffer;    // Input: meshlet group data
    rhi::BufferHandle m_visibilityBuffer;      // Intermediate: per-group visibility flags
    rhi::BufferHandle m_selectedMeshletBuffer; // Intermediate: LOD-selected meshlets
    rhi::BufferHandle m_selectedCountBuffer;   // Intermediate: count of selected meshlets
    rhi::BufferHandle m_drawCommandBuffer;     // Output: early pass draw commands
    rhi::BufferHandle m_drawCountBuffer;       // Output: early pass draw count
    rhi::BufferHandle m_lateDrawCommandBuffer; // Output: late pass draw commands
    rhi::BufferHandle m_lateDrawCountBuffer;   // Output: late pass draw count
    rhi::BufferHandle m_constantBuffer;        // Per-frame constants

    // HZB textures
    rhi::TextureHandle m_hzbTexture;
    rhi::TextureHandle m_prevHzbTexture;

    // Pipelines (compute)
    rhi::PipelineHandle m_earlyOcclusionPipeline;
    rhi::PipelineHandle m_lodSelectionPipeline;
    rhi::PipelineHandle m_drawBuildPipeline;
    rhi::PipelineHandle m_lateOcclusionPipeline;
    rhi::PipelineHandle m_counterResetPipeline;
};

} // namespace nge::renderer
