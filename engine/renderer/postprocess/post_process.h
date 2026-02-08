#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"

namespace nge::renderer {

// ─── Tone Mapping Operator ───────────────────────────────────────────────
enum class TonemapOperator : u32 {
    AgX = 0,
    TonyMcMapface = 1,
    ACES = 2,
    Reinhard = 3,
};

// ─── Post-Processing Stack ──────────────────────────────────────────────
// Manages the full post-processing chain:
//   1. Bloom (downsample/upsample chain with Karis average)
//   2. Temporal Super Resolution (upscale + temporal accumulation)
//   3. Tone mapping (AgX/ACES/Reinhard + color grading LUT)
//   4. Final composite to swapchain

struct PostProcessSettings {
    // Bloom
    bool  bloomEnabled    = true;
    f32   bloomThreshold  = 1.0f;
    f32   bloomIntensity  = 0.5f;
    f32   bloomSoftKnee   = 0.5f;
    u32   bloomMipCount   = 6;

    // TSR (Temporal Super Resolution)
    bool  tsrEnabled      = true;
    f32   tsrRenderScale  = 0.667f; // 67% of native
    f32   tsrBlendFactor  = 0.95f;
    f32   tsrSharpness    = 0.5f;

    // Tone mapping
    TonemapOperator tonemapOp = TonemapOperator::AgX;
    f32   exposure        = 0.0f;  // EV
    f32   contrast        = 1.0f;
    f32   saturation      = 1.0f;
    f32   whitePoint      = 4.0f;
    bool  enableColorLUT  = false;

    // Motion blur
    bool  motionBlurEnabled = false;
    f32   motionBlurStrength = 0.5f;
    u32   motionBlurSamples  = 8;

    // Depth of field
    bool  dofEnabled       = false;
    f32   dofFocusDistance  = 10.0f;
    f32   dofAperture      = 2.8f;
    f32   dofFocalLength   = 50.0f; // mm

    // Chromatic aberration
    bool  chromaticAberrationEnabled = false;
    f32   chromaticAberrationStrength = 0.5f;

    // Vignette
    bool  vignetteEnabled  = false;
    f32   vignetteStrength = 0.3f;
};

class PostProcessStack {
public:
    bool Init(rhi::IDevice* device, u32 width, u32 height);
    void Shutdown();
    void Resize(u32 width, u32 height);

    // Execute the full post-processing chain
    void Execute(rhi::ICommandList* cmd,
                 rhi::TextureHandle sceneColor,      // HDR input
                 rhi::TextureHandle motionVectors,    // For TSR + motion blur
                 rhi::TextureHandle depthBuffer,      // For DOF
                 rhi::TextureHandle outputTarget,     // LDR output (swapchain)
                 u32 frameIndex);

    PostProcessSettings& GetSettings() { return m_settings; }
    const PostProcessSettings& GetSettings() const { return m_settings; }

private:
    void PassBloom(rhi::ICommandList* cmd, rhi::TextureHandle sceneColor);
    void PassTSR(rhi::ICommandList* cmd, rhi::TextureHandle sceneColor,
                 rhi::TextureHandle motionVectors, u32 frameIndex);
    void PassTonemap(rhi::ICommandList* cmd, rhi::TextureHandle hdrInput,
                     rhi::TextureHandle ldrOutput);
    void PassMotionBlur(rhi::ICommandList* cmd, rhi::TextureHandle color,
                        rhi::TextureHandle motionVectors);

    void CreateBloomResources(u32 width, u32 height);
    void DestroyBloomResources();

    rhi::IDevice* m_device = nullptr;
    PostProcessSettings m_settings;

    u32 m_width  = 0;
    u32 m_height = 0;

    // Bloom mip chain
    static constexpr u32 MAX_BLOOM_MIPS = 8;
    rhi::TextureHandle m_bloomMips[MAX_BLOOM_MIPS];
    u32 m_bloomMipCount = 0;

    // TSR
    rhi::TextureHandle m_tsrHistory;     // Previous frame upscaled
    rhi::TextureHandle m_tsrOutput;      // Current upscaled output

    // Pipelines
    rhi::PipelineHandle m_bloomDownPipeline;
    rhi::PipelineHandle m_bloomUpPipeline;
    rhi::PipelineHandle m_tsrPipeline;
    rhi::PipelineHandle m_tonemapPipeline;
    rhi::PipelineHandle m_motionBlurPipeline;

    // Samplers
    rhi::SamplerHandle m_linearClampSampler;
    rhi::SamplerHandle m_pointClampSampler;
};

} // namespace nge::renderer
