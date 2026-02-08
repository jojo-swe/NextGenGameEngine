#include "engine/renderer/pipeline/render_pipeline.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"

namespace nge::renderer {

bool RenderPipeline::Init(rhi::IDevice* device, u32 width, u32 height) {
    m_device = device;
    m_width  = width;
    m_height = height;

    // Select render mode based on hardware tier
    switch (device->GetFeatureTier()) {
        case rhi::FeatureTier::Tier0_Baseline:
            m_mode = RenderMode::Rasterized;
            break;
        case rhi::FeatureTier::Tier1_GPUDriven:
            m_mode = RenderMode::GPUDriven;
            break;
        case rhi::FeatureTier::Tier2_RayTracing:
        case rhi::FeatureTier::Tier3_Neural:
            m_mode = RenderMode::HybridGI;
            break;
    }

    CreateRenderTargets(width, height);
    CreatePipelines();

    NGE_LOG_INFO("Render pipeline initialized: {}x{}, mode={}", width, height, static_cast<int>(m_mode));
    return true;
}

void RenderPipeline::Shutdown() {
    DestroyRenderTargets();
    // Pipelines are destroyed by the device
    m_device = nullptr;
}

void RenderPipeline::Resize(u32 width, u32 height) {
    if (width == m_width && height == m_height) return;
    m_width = width;
    m_height = height;
    DestroyRenderTargets();
    CreateRenderTargets(width, height);
}

void RenderPipeline::SetMode(RenderMode mode) {
    m_mode = mode;
    NGE_LOG_INFO("Render mode changed to {}", static_cast<int>(mode));
}

void RenderPipeline::RenderFrame(const FrameRenderData& frameData) {
    auto* cmd = m_device->GetCommandList();
    cmd->Begin();

    switch (m_mode) {
        case RenderMode::Rasterized:
        case RenderMode::GPUDriven:
            // GPU-driven visibility pipeline
            PassGPUCulling(cmd, frameData);
            PassVisibilityBuffer(cmd, frameData);
            PassHZBGeneration(cmd);
            PassMaterialResolve(cmd, frameData);
            PassDirectLighting(cmd, frameData);
            if (m_mode == RenderMode::GPUDriven) {
                PassIndirectLighting(cmd, frameData);
            }
            PassPostProcess(cmd, frameData);
            PassComposite(cmd);
            break;

        case RenderMode::HybridGI:
            // Raster geometry + RT global illumination
            PassGPUCulling(cmd, frameData);
            PassVisibilityBuffer(cmd, frameData);
            PassHZBGeneration(cmd);
            PassMaterialResolve(cmd, frameData);
            PassDirectLighting(cmd, frameData);
            PassIndirectLighting(cmd, frameData);
            PassShadows(cmd, frameData);
            PassDenoising(cmd);
            PassPostProcess(cmd, frameData);
            PassComposite(cmd);
            break;

        case RenderMode::PathTraced:
        case RenderMode::ReferencePathTraced:
            // Full path tracing
            PassPathTracing(cmd, frameData);
            if (m_mode == RenderMode::PathTraced) {
                PassDenoising(cmd);
            }
            PassPostProcess(cmd, frameData);
            PassComposite(cmd);
            break;
    }

    // Transition swapchain image for presentation
    rhi::TextureHandle swapchain = m_device->GetSwapchainTexture();
    cmd->TextureBarrier(swapchain, rhi::ResourceState::RenderTarget, rhi::ResourceState::Present);

    cmd->End();
    m_device->SubmitCommandList(cmd);
}

// ─── Pass Implementations (stubs — filled in as subsystems are built) ────

void RenderPipeline::PassGPUCulling(rhi::ICommandList* cmd, const FrameRenderData& /*data*/) {
    cmd->BeginDebugLabel("GPU Culling", 0.2f, 0.8f, 0.2f);
    // TODO: Bind culling compute pipeline, dispatch per-instance
    cmd->EndDebugLabel();
}

void RenderPipeline::PassVisibilityBuffer(rhi::ICommandList* cmd, const FrameRenderData& data) {
    cmd->BeginDebugLabel("Visibility Buffer", 0.3f, 0.3f, 0.9f);

    rhi::TextureHandle swapchain = m_device->GetSwapchainTexture();
    rhi::ClearValue clearColor = rhi::ClearValue::Color(0.0f, 0.0f, 0.0f, 1.0f);
    rhi::Viewport viewport{0, 0, static_cast<f32>(data.screenWidth), static_cast<f32>(data.screenHeight), 0, 1};
    rhi::Scissor scissor{0, 0, data.screenWidth, data.screenHeight};

    // For now, render directly to swapchain (vis buffer path will be added)
    cmd->TextureBarrier(swapchain, rhi::ResourceState::Present, rhi::ResourceState::RenderTarget);
    rhi::LoadOp loadOp = rhi::LoadOp::Clear;
    cmd->BeginRendering(&swapchain, 1, rhi::TextureHandle{}, &clearColor, viewport, scissor, &loadOp);

    // TODO: Bind mesh shader pipeline or fallback vertex pipeline, draw meshlets

    cmd->EndRendering();
    cmd->EndDebugLabel();
}

void RenderPipeline::PassHZBGeneration(rhi::ICommandList* cmd) {
    cmd->BeginDebugLabel("HZB Generation", 0.5f, 0.5f, 0.1f);
    // TODO: Downsample depth buffer into HZB mip chain via compute
    cmd->EndDebugLabel();
}

void RenderPipeline::PassMaterialResolve(rhi::ICommandList* cmd, const FrameRenderData& /*data*/) {
    cmd->BeginDebugLabel("Material Resolve", 0.8f, 0.4f, 0.1f);
    // TODO: Full-screen compute reads vis buffer + mesh data, evaluates materials
    cmd->EndDebugLabel();
}

void RenderPipeline::PassDirectLighting(rhi::ICommandList* cmd, const FrameRenderData& /*data*/) {
    cmd->BeginDebugLabel("Direct Lighting", 1.0f, 0.9f, 0.2f);
    // TODO: ReSTIR/RTXDI for many-light direct illumination
    cmd->EndDebugLabel();
}

void RenderPipeline::PassIndirectLighting(rhi::ICommandList* cmd, const FrameRenderData& /*data*/) {
    cmd->BeginDebugLabel("Indirect Lighting (GI)", 0.4f, 0.8f, 0.4f);
    // TODO: Screen-space + SDF + RT hybrid GI
    cmd->EndDebugLabel();
}

void RenderPipeline::PassShadows(rhi::ICommandList* cmd, const FrameRenderData& /*data*/) {
    cmd->BeginDebugLabel("Shadows", 0.3f, 0.3f, 0.3f);
    // TODO: Virtual shadow maps or RT shadows
    cmd->EndDebugLabel();
}

void RenderPipeline::PassPathTracing(rhi::ICommandList* cmd, const FrameRenderData& /*data*/) {
    cmd->BeginDebugLabel("Path Tracing", 0.9f, 0.2f, 0.9f);
    // TODO: Dispatch RT pipeline for path tracing
    cmd->EndDebugLabel();
}

void RenderPipeline::PassDenoising(rhi::ICommandList* cmd) {
    cmd->BeginDebugLabel("Denoising", 0.6f, 0.6f, 0.9f);
    // TODO: Neural denoiser or SVGF/A-SVGF denoiser
    cmd->EndDebugLabel();
}

void RenderPipeline::PassPostProcess(rhi::ICommandList* cmd, const FrameRenderData& /*data*/) {
    cmd->BeginDebugLabel("Post-Processing", 0.9f, 0.5f, 0.2f);
    // TODO: TSR upscaling, bloom, tone mapping, motion blur, DOF
    cmd->EndDebugLabel();
}

void RenderPipeline::PassComposite(rhi::ICommandList* cmd) {
    cmd->BeginDebugLabel("Composite", 0.2f, 0.9f, 0.9f);
    // TODO: Final composite to swapchain with UI overlay
    cmd->EndDebugLabel();
}

// ─── Render Targets ──────────────────────────────────────────────────────

void RenderPipeline::CreateRenderTargets(u32 width, u32 height) {
    using namespace rhi;
    auto createRT = [&](Format fmt, TextureUsage usage, const char* name) -> TextureHandle {
        TextureDesc desc{};
        desc.width     = width;
        desc.height    = height;
        desc.format    = fmt;
        desc.usage     = usage;
        desc.debugName = name;
        return m_device->CreateTexture(desc);
    };

    auto rtUsage = TextureUsage::RenderTarget | TextureUsage::ShaderRead;
    auto rwUsage = TextureUsage::ShaderRead | TextureUsage::ShaderWrite;

    m_visibilityBuffer  = createRT(Format::RG32_UINT,    rtUsage, "VisibilityBuffer");
    m_depthBuffer       = createRT(Format::D32_FLOAT,    TextureUsage::DepthStencil | TextureUsage::ShaderRead, "DepthBuffer");
    m_gbufferAlbedo     = createRT(Format::RGBA8_UNORM,  rtUsage, "GBuffer_Albedo");
    m_gbufferNormal     = createRT(Format::RG16_FLOAT,   rtUsage, "GBuffer_Normal");
    m_gbufferMaterial   = createRT(Format::RGBA8_UNORM,  rtUsage, "GBuffer_Material");
    m_sceneColor        = createRT(Format::RGBA16_FLOAT, rtUsage, "SceneColor");
    m_motionVectors     = createRT(Format::RG16_FLOAT,   rtUsage, "MotionVectors");
    m_directLighting    = createRT(Format::RGBA16_FLOAT, rwUsage, "DirectLighting");
    m_indirectLighting  = createRT(Format::RGBA16_FLOAT, rwUsage, "IndirectLighting");
    m_pathTracerOutput  = createRT(Format::RGBA16_FLOAT, rwUsage, "PathTracerOutput");
    m_denoisedOutput    = createRT(Format::RGBA16_FLOAT, rwUsage, "DenoisedOutput");
    m_historyColor      = createRT(Format::RGBA16_FLOAT, rwUsage, "HistoryColor");

    // HZB: half resolution with mip chain
    {
        TextureDesc hzbDesc{};
        hzbDesc.width     = width / 2;
        hzbDesc.height    = height / 2;
        hzbDesc.mipLevels = static_cast<u32>(math::Log2(math::Max(width / 2, height / 2))) + 1;
        hzbDesc.format    = Format::R32_FLOAT;
        hzbDesc.usage     = TextureUsage::ShaderRead | TextureUsage::ShaderWrite;
        hzbDesc.debugName = "HZB";
        m_hzbTexture = m_device->CreateTexture(hzbDesc);
    }

    NGE_LOG_INFO("Render targets created: {}x{}", width, height);
}

void RenderPipeline::DestroyRenderTargets() {
    if (!m_device) return;

    auto destroy = [&](rhi::TextureHandle& h) {
        if (h.IsValid()) { m_device->DestroyTexture(h); h = rhi::TextureHandle{}; }
    };

    destroy(m_visibilityBuffer);
    destroy(m_depthBuffer);
    destroy(m_hzbTexture);
    destroy(m_gbufferAlbedo);
    destroy(m_gbufferNormal);
    destroy(m_gbufferMaterial);
    destroy(m_sceneColor);
    destroy(m_motionVectors);
    destroy(m_directLighting);
    destroy(m_indirectLighting);
    destroy(m_pathTracerOutput);
    destroy(m_denoisedOutput);
    destroy(m_historyColor);
}

void RenderPipeline::CreatePipelines() {
    // TODO: Load shaders and create pipelines
    // This will be done when the shader compilation pipeline is integrated
    NGE_LOG_INFO("Render pipelines created (stubs)");
}

} // namespace nge::renderer
