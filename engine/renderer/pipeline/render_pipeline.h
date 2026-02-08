#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/rhi/common/rhi_queries.h"
#include "engine/renderer/graph/render_graph.h"
#include "engine/scene/camera/camera.h"
#include "engine/core/math/pga.h"
#include <memory>

namespace nge::renderer {

// ─── Render Pipeline Mode ────────────────────────────────────────────────
enum class RenderMode : u8 {
    Rasterized,      // Tier 0: Traditional raster + deferred/forward
    GPUDriven,       // Tier 1: Mesh shaders + visibility buffer
    HybridGI,        // Tier 2: Raster + RT GI
    PathTraced,      // Tier 2: Full path tracing (production 1 SPP + denoiser)
    ReferencePathTraced, // Tier 2: Reference path tracer (progressive)
};

// ─── Frame Render Data ───────────────────────────────────────────────────
// Collected once per frame, passed to the render pipeline.
struct FrameRenderData {
    math::Mat4 viewMatrix;
    math::Mat4 projMatrix;
    math::Mat4 viewProjMatrix;
    math::Mat4 prevViewProjMatrix;
    math::Mat4 invViewProjMatrix;
    math::Vec3 cameraPosition;
    math::Vec3 cameraForward;
    math::Vec3 cameraRight;
    math::Vec3 cameraUp;
    f32        nearPlane;
    f32        farPlane;
    f32        time;
    f32        deltaTime;
    u32        frameIndex;
    u32        screenWidth;
    u32        screenHeight;
    f32        jitterX;
    f32        jitterY;
};

// ─── Render Pipeline ─────────────────────────────────────────────────────
// Orchestrates the full frame rendering sequence.
//
// GPU-Driven pipeline:
//   1. GPU Culling (frustum + occlusion via HZB)
//   2. Meshlet Culling (frustum + backface cone)
//   3. Visibility Buffer Generation (mesh shaders → vis buffer)
//   4. Material Resolve (full-screen compute → shade visible pixels)
//   5. Lighting (direct via RTXDI/ReSTIR + indirect via hybrid GI)
//   6. Post-processing (TSR, bloom, tone mapping, etc.)
//   7. Present
//
// Path Traced pipeline:
//   1. Ray Generation (camera rays)
//   2. Path Tracing (bounces with NEE + ReSTIR)
//   3. Denoising (separable diffuse/specular/shadow)
//   4. Temporal Accumulation
//   5. Post-processing (tone mapping, etc.)
//   6. Present

class RenderPipeline {
public:
    RenderPipeline() = default;
    ~RenderPipeline() = default;

    bool Init(rhi::IDevice* device, u32 width, u32 height);
    void Shutdown();

    void Resize(u32 width, u32 height);
    void SetMode(RenderMode mode);
    RenderMode GetMode() const { return m_mode; }

    // Main entry point: render one frame
    void RenderFrame(const FrameRenderData& frameData);

    // Render graph-based frame (preferred path)
    void RenderFrameGraph(const FrameRenderData& frameData);

    // GPU profiler access
    rhi::GPUProfiler& GetProfiler() { return m_profiler; }

private:
    // ─── Pipeline passes ──────────────────────────────────────────────

    // Phase 1: GPU culling
    void PassGPUCulling(rhi::ICommandList* cmd, const FrameRenderData& data);

    // Phase 2: Visibility buffer generation (mesh shader or vertex shader path)
    void PassVisibilityBuffer(rhi::ICommandList* cmd, const FrameRenderData& data);

    // Phase 3: HZB generation (mip chain of depth buffer)
    void PassHZBGeneration(rhi::ICommandList* cmd);

    // Phase 4: Material resolve (compute shader reads vis buffer, evaluates materials)
    void PassMaterialResolve(rhi::ICommandList* cmd, const FrameRenderData& data);

    // Phase 5: Direct lighting (ReSTIR/RTXDI or shadow maps)
    void PassDirectLighting(rhi::ICommandList* cmd, const FrameRenderData& data);

    // Phase 6: Indirect lighting (screen-space + SDF + RT + probes)
    void PassIndirectLighting(rhi::ICommandList* cmd, const FrameRenderData& data);

    // Phase 7: Shadows (virtual shadow maps or RT shadows)
    void PassShadows(rhi::ICommandList* cmd, const FrameRenderData& data);

    // Phase 8: Path tracing (alternative to phases 2-7)
    void PassPathTracing(rhi::ICommandList* cmd, const FrameRenderData& data);

    // Phase 9: Denoising (for path-traced or RT results)
    void PassDenoising(rhi::ICommandList* cmd);

    // Phase 10: Post-processing
    void PassPostProcess(rhi::ICommandList* cmd, const FrameRenderData& data);

    // Phase 11: Final composite to swapchain
    void PassComposite(rhi::ICommandList* cmd);

    // ─── Resource management ──────────────────────────────────────────
    void CreateRenderTargets(u32 width, u32 height);
    void DestroyRenderTargets();
    void CreatePipelines();

    rhi::IDevice* m_device = nullptr;
    RenderMode    m_mode   = RenderMode::Rasterized;

    u32 m_width  = 0;
    u32 m_height = 0;

    // ─── Render targets ───────────────────────────────────────────────
    rhi::TextureHandle m_visibilityBuffer;    // R32G32_UINT: meshlet/tri/material IDs
    rhi::TextureHandle m_depthBuffer;         // D32_FLOAT
    rhi::TextureHandle m_hzbTexture;          // Hierarchical Z-Buffer mip chain
    rhi::TextureHandle m_gbufferAlbedo;       // RGBA8: base color + AO
    rhi::TextureHandle m_gbufferNormal;       // RG16F: octahedral encoded normals
    rhi::TextureHandle m_gbufferMaterial;     // RGBA8: metallic, roughness, emissive mask
    rhi::TextureHandle m_sceneColor;          // RGBA16F: HDR scene color
    rhi::TextureHandle m_motionVectors;       // RG16F: screen-space motion
    rhi::TextureHandle m_directLighting;      // RGBA16F
    rhi::TextureHandle m_indirectLighting;    // RGBA16F
    rhi::TextureHandle m_pathTracerOutput;    // RGBA16F: path tracer raw output
    rhi::TextureHandle m_denoisedOutput;      // RGBA16F: denoised result
    rhi::TextureHandle m_historyColor;        // RGBA16F: previous frame for temporal

    // ─── Pipelines ────────────────────────────────────────────────────
    rhi::PipelineHandle m_cullingPipeline;
    rhi::PipelineHandle m_visBufferPipeline;  // Mesh shader or vertex pipeline
    rhi::PipelineHandle m_hzbPipeline;
    rhi::PipelineHandle m_materialResolvePipeline;
    rhi::PipelineHandle m_directLightPipeline;
    rhi::PipelineHandle m_indirectLightPipeline;
    rhi::PipelineHandle m_pathTracePipeline;
    rhi::PipelineHandle m_denoisePipeline;
    rhi::PipelineHandle m_bloomDownPipeline;
    rhi::PipelineHandle m_bloomUpPipeline;
    rhi::PipelineHandle m_tonemapPipeline;
    rhi::PipelineHandle m_compositePipeline;

    // ─── Buffers ──────────────────────────────────────────────────────
    rhi::BufferHandle m_visibleListBuffer;     // Visible instance indices
    rhi::BufferHandle m_drawCommandBuffer;     // Indirect draw commands
    rhi::BufferHandle m_instanceDataBuffer;    // Per-instance transforms + bounds

    // ─── Render graph ────────────────────────────────────────────────
    std::unique_ptr<RenderGraph> m_renderGraph;
    rhi::GPUProfiler m_profiler;
    bool m_useRenderGraph = true;
};

} // namespace nge::renderer
