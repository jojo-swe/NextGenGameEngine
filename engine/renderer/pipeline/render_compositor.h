#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/renderer/graph/render_graph.h"
#include "engine/renderer/materials/material_system.h"
#include "engine/renderer/pipeline/gpu_culling_pipeline.h"

namespace nge::renderer {

// ─── Render Compositor ───────────────────────────────────────────────────
// Assembles the final frame by orchestrating all render graph passes:
//   Early depth → HZB build → Occlusion cull → LOD → Material resolve →
//   Direct lighting → GI → Post-process chain → UI overlay → Present
//
// This is the top-level coordinator that builds the frame's render graph
// each frame, configuring resources, bindings, and pass dependencies.

struct CompositorConfig {
    u32  renderWidth = 1920;
    u32  renderHeight = 1080;
    u32  outputWidth = 1920;
    u32  outputHeight = 1080;
    bool enableTAA = true;
    bool enableFXAA = false;
    bool enableBloom = true;
    bool enableSSAO = true;
    bool enableSSR = true;
    bool enableVolumetricFog = true;
    bool enableMotionBlur = true;
    bool enableDOF = false;
    bool enableCAS = true;
    bool enableAutoExposure = true;
    bool enablePathTracing = false;     // Use path tracing instead of rasterization
    bool enableAsyncCompute = true;     // Overlap compute on async queue
    f32  renderScale = 1.0f;            // Internal render resolution scale
};

struct CompositorFrameData {
    math::Mat4 view;
    math::Mat4 proj;
    math::Mat4 viewProj;
    math::Mat4 prevViewProj;
    math::Vec4 cameraPos;
    math::Vec4 sunDirection;
    math::Vec4 sunColor;
    math::Vec2 jitter;        // TAA jitter offset
    f32        deltaTime;
    f32        totalTime;
    u32        frameIndex;
};

class RenderCompositor {
public:
    bool Init(rhi::IDevice* device, const CompositorConfig& config = {});
    void Shutdown();

    void SetConfig(const CompositorConfig& config) { m_config = config; }
    const CompositorConfig& GetConfig() const { return m_config; }

    // Build and execute the frame's render graph
    void RenderFrame(const CompositorFrameData& frameData,
                     rhi::ICommandList* graphicsCmd,
                     rhi::ICommandList* asyncComputeCmd = nullptr);

    // Access subsystems
    GPUCullingPipeline& GetCullingPipeline() { return m_culling; }

    // Get output texture for presentation
    rhi::TextureHandle GetOutputTexture() const { return m_outputTexture; }

    // Per-frame resources (transient, managed by render graph)
    struct FrameResources {
        RGResourceHandle depthBuffer;
        RGResourceHandle visibilityBuffer;
        RGResourceHandle hzbTexture;
        RGResourceHandle hdrColor;
        RGResourceHandle gbufferNormals;
        RGResourceHandle gbufferMotion;
        RGResourceHandle ssaoTexture;
        RGResourceHandle ssrTexture;
        RGResourceHandle bloomTexture;
        RGResourceHandle exposureBuffer;
        RGResourceHandle postProcessed;
        RGResourceHandle outputLDR;
    };

private:
    void BuildRasterizationPasses(RenderGraph& graph, FrameResources& res,
                                    const CompositorFrameData& frameData);
    void BuildPathTracingPasses(RenderGraph& graph, FrameResources& res,
                                  const CompositorFrameData& frameData);
    void BuildPostProcessPasses(RenderGraph& graph, FrameResources& res,
                                  const CompositorFrameData& frameData);
    void BuildUIPass(RenderGraph& graph, FrameResources& res);

    rhi::IDevice* m_device = nullptr;
    CompositorConfig m_config;
    GPUCullingPipeline m_culling;

    // Persistent resources
    rhi::TextureHandle m_outputTexture;
    rhi::TextureHandle m_historyColor;     // TAA history
    rhi::TextureHandle m_prevDepth;        // Previous frame depth

    RenderGraph m_graph;
    u32 m_frameCount = 0;
};

} // namespace nge::renderer
