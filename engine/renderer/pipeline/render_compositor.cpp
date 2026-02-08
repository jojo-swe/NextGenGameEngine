#include "engine/renderer/pipeline/render_compositor.h"
#include "engine/core/logging/log.h"

namespace nge::renderer {

bool RenderCompositor::Init(rhi::IDevice* device, const CompositorConfig& config) {
    m_device = device;
    m_config = config;
    m_graph = RenderGraph(device);

    // Initialize GPU culling pipeline
    GPUCullingConfig cullConfig;
    cullConfig.useMeshShaders = true;
    m_culling.Init(device, cullConfig);

    // Create persistent output texture
    {
        rhi::TextureDesc desc;
        desc.width = config.outputWidth;
        desc.height = config.outputHeight;
        desc.format = rhi::Format::RGBA8_UNORM;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage | rhi::TextureUsage::ColorAttachment;
        desc.debugName = "CompositorOutput";
        m_outputTexture = device->CreateTexture(desc);
    }

    // TAA history buffer
    {
        rhi::TextureDesc desc;
        desc.width = config.renderWidth;
        desc.height = config.renderHeight;
        desc.format = rhi::Format::RGBA16_FLOAT;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::ColorAttachment;
        desc.debugName = "TAAHistory";
        m_historyColor = device->CreateTexture(desc);
    }

    // Previous frame depth
    {
        rhi::TextureDesc desc;
        desc.width = config.renderWidth;
        desc.height = config.renderHeight;
        desc.format = rhi::Format::D32_FLOAT;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::DepthAttachment;
        desc.debugName = "PrevDepth";
        m_prevDepth = device->CreateTexture(desc);
    }

    NGE_LOG_INFO("Render compositor initialized: {}x{} render, {}x{} output",
                 config.renderWidth, config.renderHeight, config.outputWidth, config.outputHeight);
    return true;
}

void RenderCompositor::Shutdown() {
    m_culling.Shutdown();

    if (m_outputTexture.IsValid()) m_device->DestroyTexture(m_outputTexture);
    if (m_historyColor.IsValid()) m_device->DestroyTexture(m_historyColor);
    if (m_prevDepth.IsValid()) m_device->DestroyTexture(m_prevDepth);

    m_outputTexture = {};
    m_historyColor = {};
    m_prevDepth = {};
}

void RenderCompositor::RenderFrame(const CompositorFrameData& frameData,
                                     rhi::ICommandList* graphicsCmd,
                                     rhi::ICommandList* asyncComputeCmd) {
    m_graph.Reset();

    FrameResources res;

    if (m_config.enablePathTracing) {
        BuildPathTracingPasses(m_graph, res, frameData);
    } else {
        BuildRasterizationPasses(m_graph, res, frameData);
    }

    BuildPostProcessPasses(m_graph, res, frameData);
    BuildUIPass(m_graph, res);

    m_graph.Compile();

    if (m_config.enableAsyncCompute && asyncComputeCmd) {
        m_graph.Execute(graphicsCmd, asyncComputeCmd);
    } else {
        m_graph.Execute(graphicsCmd);
    }

    m_frameCount++;
}

void RenderCompositor::BuildRasterizationPasses(RenderGraph& graph, FrameResources& res,
                                                   const CompositorFrameData& frameData) {
    u32 w = m_config.renderWidth;
    u32 h = m_config.renderHeight;

    // ── Depth Pre-pass ──────────────────────────────────────────────
    auto& depthPass = graph.AddPass("DepthPrepass", PassType::Graphics);
    res.depthBuffer = depthPass.CreateTexture("SceneDepth", {w, h, 1, 1, rhi::Format::D32_FLOAT,
        rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled, "SceneDepth"});
    res.visibilityBuffer = depthPass.CreateTexture("VisBuffer", {w, h, 1, 1, rhi::Format::R32_UINT,
        rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled, "VisBuffer"});
    depthPass.WriteDepth(res.depthBuffer);
    depthPass.WriteColor(res.visibilityBuffer, 0);
    depthPass.SetViewport(w, h);
    depthPass.SetExecute([this, &frameData](rhi::ICommandList* cmd) {
        // Bind early-z pipeline, draw visible meshlets from culling result
        // cmd->DrawIndexedIndirectCount(m_culling.GetDrawCommandBuffer(), 0,
        //                                m_culling.GetDrawCountBuffer(), 0, maxDraws, stride);
        (void)cmd; (void)frameData;
    });

    // ── HZB Build (Async Compute) ───────────────────────────────────
    auto& hzbPass = graph.AddPass("HZBBuild",
        m_config.enableAsyncCompute ? PassType::AsyncCompute : PassType::Compute);
    res.hzbTexture = hzbPass.CreateTexture("HZB", {w / 2, h / 2, 1, 10, rhi::Format::R32_FLOAT,
        rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled, "HZB"});
    hzbPass.Read(res.depthBuffer);
    hzbPass.Write(res.hzbTexture);
    hzbPass.SetExecute([](rhi::ICommandList* cmd) {
        // Hierarchical Z-buffer downscale via compute
        // Each mip reads previous mip, writes min/max depth
        (void)cmd;
    });

    // ── Material Resolve ────────────────────────────────────────────
    auto& materialPass = graph.AddPass("MaterialResolve", PassType::Graphics);
    res.gbufferNormals = materialPass.CreateTexture("GBufferNormals", {w, h, 1, 1,
        rhi::Format::RG16_FLOAT, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled, "GBufferNormals"});
    res.gbufferMotion = materialPass.CreateTexture("GBufferMotion", {w, h, 1, 1,
        rhi::Format::RG16_FLOAT, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled, "GBufferMotion"});
    res.hdrColor = materialPass.CreateTexture("HDRColor", {w, h, 1, 1, rhi::Format::RGBA16_FLOAT,
        rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled, "HDRColor"});
    materialPass.Read(res.depthBuffer);
    materialPass.Read(res.visibilityBuffer);
    materialPass.WriteColor(res.hdrColor, 0);
    materialPass.WriteColor(res.gbufferNormals, 1);
    materialPass.WriteColor(res.gbufferMotion, 2);
    materialPass.SetViewport(w, h);
    materialPass.SetExecute([](rhi::ICommandList* cmd) {
        // Full-screen pass: resolve visibility buffer → material shading
        // Samples material data from bindless structured buffer
        (void)cmd;
    });

    // ── SSAO (Async Compute) ────────────────────────────────────────
    if (m_config.enableSSAO) {
        auto& ssaoPass = graph.AddPass("SSAO",
            m_config.enableAsyncCompute ? PassType::AsyncCompute : PassType::Compute);
        res.ssaoTexture = ssaoPass.CreateTexture("SSAO", {w / 2, h / 2, 1, 1, rhi::Format::R8_UNORM,
            rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled, "SSAO"});
        ssaoPass.Read(res.depthBuffer);
        ssaoPass.Read(res.gbufferNormals);
        ssaoPass.Write(res.ssaoTexture);
        ssaoPass.SetExecute([](rhi::ICommandList* cmd) { (void)cmd; });
    }

    // ── Direct Lighting ─────────────────────────────────────────────
    auto& lightPass = graph.AddPass("DirectLighting", PassType::Compute);
    lightPass.Read(res.hdrColor);
    lightPass.Read(res.depthBuffer);
    lightPass.Read(res.gbufferNormals);
    if (m_config.enableSSAO && res.ssaoTexture.IsValid()) {
        lightPass.Read(res.ssaoTexture);
    }
    lightPass.Write(res.hdrColor, rhi::ResourceState::ShaderWrite);
    lightPass.SetExecute([](rhi::ICommandList* cmd) {
        // Apply direct lighting: sun + clustered point/spot lights
        (void)cmd;
    });

    // ── SSR ─────────────────────────────────────────────────────────
    if (m_config.enableSSR) {
        auto& ssrPass = graph.AddPass("SSR", PassType::Compute);
        res.ssrTexture = ssrPass.CreateTexture("SSR", {w / 2, h / 2, 1, 1, rhi::Format::RGBA16_FLOAT,
            rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled, "SSR"});
        ssrPass.Read(res.hdrColor);
        ssrPass.Read(res.depthBuffer);
        ssrPass.Read(res.gbufferNormals);
        ssrPass.Read(res.hzbTexture);
        ssrPass.Write(res.ssrTexture);
        ssrPass.SetExecute([](rhi::ICommandList* cmd) { (void)cmd; });
    }

    (void)frameData;
}

void RenderCompositor::BuildPathTracingPasses(RenderGraph& graph, FrameResources& res,
                                                const CompositorFrameData& frameData) {
    u32 w = m_config.renderWidth;
    u32 h = m_config.renderHeight;

    // ── Path Trace ──────────────────────────────────────────────────
    auto& ptPass = graph.AddPass("PathTrace", PassType::Compute);
    res.hdrColor = ptPass.CreateTexture("PTOutput", {w, h, 1, 1, rhi::Format::RGBA32_FLOAT,
        rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled, "PTOutput"});
    res.gbufferNormals = ptPass.CreateTexture("PTNormals", {w, h, 1, 1, rhi::Format::RG16_FLOAT,
        rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled, "PTNormals"});
    res.depthBuffer = ptPass.CreateTexture("PTDepth", {w, h, 1, 1, rhi::Format::R32_FLOAT,
        rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled, "PTDepth"});
    ptPass.Write(res.hdrColor);
    ptPass.Write(res.gbufferNormals);
    ptPass.Write(res.depthBuffer);
    ptPass.SetExecute([](rhi::ICommandList* cmd) {
        // Dispatch ray tracing / compute path tracer
        (void)cmd;
    });

    // ── SVGF Denoise ────────────────────────────────────────────────
    auto& denoisePass = graph.AddPass("SVGFDenoise", PassType::Compute);
    denoisePass.ReadWrite(res.hdrColor);
    denoisePass.Read(res.gbufferNormals);
    denoisePass.Read(res.depthBuffer);
    denoisePass.SetExecute([](rhi::ICommandList* cmd) {
        // SVGF temporal accumulation + A-trous wavelet filter
        (void)cmd;
    });

    (void)frameData;
}

void RenderCompositor::BuildPostProcessPasses(RenderGraph& graph, FrameResources& res,
                                                const CompositorFrameData& frameData) {
    u32 w = m_config.renderWidth;
    u32 h = m_config.renderHeight;

    // ── Auto Exposure (Async Compute) ───────────────────────────────
    if (m_config.enableAutoExposure) {
        auto& exposurePass = graph.AddPass("AutoExposure",
            m_config.enableAsyncCompute ? PassType::AsyncCompute : PassType::Compute);
        res.exposureBuffer = exposurePass.CreateBuffer("ExposureData", {sizeof(f32) * 4,
            rhi::BufferUsage::Storage, "ExposureData"});
        exposurePass.Read(res.hdrColor);
        exposurePass.Write(res.exposureBuffer);
        exposurePass.SetExecute([](rhi::ICommandList* cmd) { (void)cmd; });
    }

    // ── Bloom ───────────────────────────────────────────────────────
    if (m_config.enableBloom) {
        auto& bloomPass = graph.AddPass("Bloom", PassType::Compute);
        res.bloomTexture = bloomPass.CreateTexture("Bloom", {w / 2, h / 2, 1, 6, rhi::Format::R11G11B10_FLOAT,
            rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled, "Bloom"});
        bloomPass.Read(res.hdrColor);
        bloomPass.Write(res.bloomTexture);
        bloomPass.SetExecute([](rhi::ICommandList* cmd) { (void)cmd; });
    }

    // ── TAA ─────────────────────────────────────────────────────────
    if (m_config.enableTAA) {
        auto& taaPass = graph.AddPass("TAA", PassType::Compute);
        res.postProcessed = taaPass.CreateTexture("TAAOutput", {w, h, 1, 1, rhi::Format::RGBA16_FLOAT,
            rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled, "TAAOutput"});
        taaPass.Read(res.hdrColor);
        if (res.gbufferMotion.IsValid()) taaPass.Read(res.gbufferMotion);
        taaPass.Write(res.postProcessed);
        taaPass.SetExecute([](rhi::ICommandList* cmd) { (void)cmd; });
    } else {
        res.postProcessed = res.hdrColor;
    }

    // ── Tone Map + CAS + Output ─────────────────────────────────────
    auto& tonemapPass = graph.AddPass("ToneMap", PassType::Compute);
    res.outputLDR = tonemapPass.CreateTexture("OutputLDR", {m_config.outputWidth, m_config.outputHeight,
        1, 1, rhi::Format::RGBA8_UNORM, rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled, "OutputLDR"});
    tonemapPass.Read(res.postProcessed);
    if (m_config.enableBloom && res.bloomTexture.IsValid()) tonemapPass.Read(res.bloomTexture);
    if (m_config.enableAutoExposure && res.exposureBuffer.IsValid()) tonemapPass.Read(res.exposureBuffer);
    tonemapPass.Write(res.outputLDR);
    tonemapPass.SetExecute([](rhi::ICommandList* cmd) {
        // ACES tone mapping + bloom composite + CAS sharpen + LDR output
        (void)cmd;
    });

    // ── FXAA (optional, after tone map) ─────────────────────────────
    if (m_config.enableFXAA) {
        auto& fxaaPass = graph.AddPass("FXAA", PassType::Compute);
        fxaaPass.ReadWrite(res.outputLDR);
        fxaaPass.SetExecute([](rhi::ICommandList* cmd) { (void)cmd; });
    }

    (void)frameData;
}

void RenderCompositor::BuildUIPass(RenderGraph& graph, FrameResources& res) {
    auto& uiPass = graph.AddPass("UIOverlay", PassType::Graphics);
    uiPass.ReadWrite(res.outputLDR);
    uiPass.SetViewport(m_config.outputWidth, m_config.outputHeight);
    uiPass.SetExecute([](rhi::ICommandList* cmd) {
        // ImGui draw commands, debug text, profiler overlay
        (void)cmd;
    });
}

} // namespace nge::renderer
