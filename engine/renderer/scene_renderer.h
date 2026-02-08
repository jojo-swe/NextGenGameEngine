#pragma once

#include "engine/core/types.h"
#include "engine/core/ecs/world.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/rhi/common/rhi_bindless.h"
#include "engine/rhi/common/rhi_buffer_pool.h"
#include "engine/rhi/common/rhi_staging.h"
#include "engine/rhi/common/rhi_timeline_fence.h"
#include "engine/renderer/materials/material_system.h"
#include "engine/renderer/pipeline/render_compositor.h"
#include "engine/renderer/pipeline/gpu_culling_pipeline.h"
#include "engine/renderer/debug/profiler_overlay.h"

namespace nge::renderer {

// ─── Scene Renderer ──────────────────────────────────────────────────────
// Top-level renderer that ties all engine subsystems together for a frame:
//   ECS world → mesh extraction → GPU upload → culling → render graph → present
//
// Owns all GPU-side infrastructure and orchestrates the render loop.

struct SceneRendererConfig {
    u32  windowWidth = 1920;
    u32  windowHeight = 1080;
    f32  renderScale = 1.0f;
    u32  framesInFlight = 3;
    bool enableDebugOverlay = true;
    bool enablePathTracing = false;
    bool enableAsyncCompute = true;
    bool vsync = true;
};

class SceneRenderer {
public:
    bool Init(rhi::IDevice* device, const SceneRendererConfig& config = {});
    void Shutdown();

    // Main render entry — call once per frame
    void RenderFrame(ecs::World& world, f32 deltaTime);

    // Resize
    void OnResize(u32 width, u32 height);

    // Access subsystems
    MaterialManager& GetMaterialManager() { return m_materials; }
    rhi::BindlessTable& GetBindlessTable() { return m_bindless; }
    rhi::StagingManager& GetStagingManager() { return m_staging; }
    RenderCompositor& GetCompositor() { return m_compositor; }
    GPUCullingPipeline& GetCullingPipeline() { return m_compositor.GetCullingPipeline(); }

    // Stats
    u32 GetFrameNumber() const { return m_frameNumber; }
    f32 GetLastFrameTimeMs() const { return m_lastFrameTimeMs; }

private:
    void ExtractRenderData(ecs::World& world);
    void UploadPerFrameData();
    void BuildJitterSequence();
    math::Vec2 GetTAAJitter(u32 frameIndex) const;

    rhi::IDevice* m_device = nullptr;
    SceneRendererConfig m_config;

    // Core subsystems
    MaterialManager      m_materials;
    rhi::BindlessTable   m_bindless;
    rhi::BufferPool      m_bufferPool;
    rhi::StagingManager  m_staging;
    rhi::FrameFence      m_frameFence;
    rhi::DeletionQueue   m_deletionQueue;
    RenderCompositor     m_compositor;
    ProfilerOverlay      m_profilerOverlay;

    // Per-frame data
    rhi::BufferHandle    m_perFrameConstantBuffer;
    CompositorFrameData  m_frameData;

    // TAA jitter (Halton 2,3 sequence)
    static constexpr u32 JITTER_SAMPLES = 16;
    math::Vec2 m_jitterSequence[JITTER_SAMPLES];

    u32 m_frameNumber = 0;
    f32 m_totalTime = 0;
    f32 m_lastFrameTimeMs = 0;
};

} // namespace nge::renderer
