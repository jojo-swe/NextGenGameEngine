#include "engine/renderer/pipeline/render_pipeline.h"
#include "engine/assets/shader/shader_compiler.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct DemoVertex {
    nge::math::Vec3 position;
    nge::math::Vec3 normal;
    nge::math::Vec3 color;
};

struct DemoInstanceData {
    nge::math::Vec4 offsetScale;  // xyz = offset, w = scale
    nge::math::Vec4 colorTint;    // rgb = tint, a = unused
};

struct DemoPushConstants {
    nge::math::Mat4 viewProj;
    nge::math::Vec4 cameraPos;
    nge::u32 frameIndex;
    nge::u32 screenWidth;
    nge::u32 screenHeight;
    nge::u32 pad0;
    float time;
    float deltaTime;
    nge::u32 pad1;
    nge::u32 pad2;
};

nge::math::Mat4 Transpose(const nge::math::Mat4& matrix) {
    nge::math::Mat4 result{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            result.m[row][column] = matrix.m[column][row];
        }
    }
    return result;
}

fs::path FindShaderRoot() {
    fs::path current = fs::current_path();

    while (!current.empty()) {
        const fs::path candidate = current / "shaders";
        if (fs::exists(candidate / "mesh" / "triangle.vert.hlsl") &&
            fs::exists(candidate / "mesh" / "triangle.frag.hlsl")) {
            return candidate;
        }

        if (current == current.root_path()) {
            break;
        }

        current = current.parent_path();
    }

    return {};
}

template <typename HandleT, typename DestroyFn>
void ResetHandle(HandleT& handle, DestroyFn&& destroyFn) {
    if (handle.IsValid()) {
        destroyFn(handle);
        handle = HandleT{};
    }
}

} 

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

    m_renderGraph = std::make_unique<RenderGraph>(device);
    m_profiler.Init(128, 60);

    NGE_LOG_INFO("Render pipeline initialized: {}x{}, mode={}", width, height, static_cast<int>(m_mode));
    return true;
}

void RenderPipeline::Shutdown() {
    m_profiler.Shutdown();
    if (m_renderGraph) { m_renderGraph->Reset(); m_renderGraph.reset(); }
    ResetHandle(m_demoVertexBuffer, [this](rhi::BufferHandle handle) { m_device->DestroyBuffer(handle); });
    ResetHandle(m_demoIndexBuffer, [this](rhi::BufferHandle handle) { m_device->DestroyBuffer(handle); });
    ResetHandle(m_demoInstanceBuffer, [this](rhi::BufferHandle handle) { m_device->DestroyBuffer(handle); });
    ResetHandle(m_visBufferPipeline, [this](rhi::PipelineHandle handle) { m_device->DestroyPipeline(handle); });
    ResetHandle(m_demoVertexShader, [this](rhi::ShaderHandle handle) { m_device->DestroyShader(handle); });
    ResetHandle(m_demoFragmentShader, [this](rhi::ShaderHandle handle) { m_device->DestroyShader(handle); });
    m_demoIndexCount = 0;
    m_demoInstanceCount = 0;
    DestroyRenderTargets();
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

    // Post-render callback (e.g. ImGui overlay)
    if (m_postRenderCallback) {
        m_postRenderCallback(cmd);
    }

    // Transition swapchain image for presentation
    rhi::TextureHandle swapchain = m_device->GetSwapchainTexture();
    cmd->TextureBarrier(swapchain, rhi::ResourceState::RenderTarget, rhi::ResourceState::Present);

    cmd->End();
    m_device->SubmitCommandList(cmd);
}

// ─── Render Graph-Based Frame ────────────────────────────────────────────

void RenderPipeline::RenderFrameGraph(const FrameRenderData& frameData) {
    m_profiler.BeginFrame();
    m_renderGraph->Reset();

    using namespace rhi;
    auto fmt16F = Format::RGBA16_FLOAT;
    auto rwUsage = TextureUsage::ShaderRead | TextureUsage::ShaderWrite;

    // Import external resources
    RGTextureDesc swapDesc;
    swapDesc.width = m_width;
    swapDesc.height = m_height;
    swapDesc.format = Format::RGBA8_UNORM;

    // ── Depth Prepass / Visibility Buffer ─────────────────────────────
    auto& visPass = m_renderGraph->AddPass("VisibilityBuffer", PassType::Graphics);
    RGTextureDesc depthDesc{m_width, m_height, 1, 1, Format::D32_FLOAT, TextureUsage::DepthStencil | TextureUsage::ShaderRead, "SceneDepth"};
    RGTextureDesc visDesc{m_width, m_height, 1, 1, Format::RG32_UINT, TextureUsage::RenderTarget | TextureUsage::ShaderRead, "VisBuffer"};
    auto rgDepth = visPass.CreateTexture("SceneDepth", depthDesc);
    auto rgVisBuffer = visPass.CreateTexture("VisBuffer", visDesc);
    visPass.WriteDepth(rgDepth);
    visPass.WriteColor(rgVisBuffer);
    visPass.SetViewport(m_width, m_height);
    visPass.SetExecute([this, &frameData](ICommandList* cmd) {
        m_profiler.BeginScope("VisibilityBuffer");
        PassVisibilityBuffer(cmd, frameData);
        m_profiler.EndScope();
    });

    // ── HZB Build ────────────────────────────────────────────────────
    auto& hzbPass = m_renderGraph->AddPass("HZBBuild", PassType::Compute);
    RGTextureDesc hzbDesc{m_width / 2, m_height / 2, 1, 8, Format::R32_FLOAT, rwUsage, "HZB"};
    auto rgHZB = hzbPass.CreateTexture("HZB", hzbDesc);
    hzbPass.Read(rgDepth);
    hzbPass.Write(rgHZB);
    hzbPass.SetExecute([this](ICommandList* cmd) {
        m_profiler.BeginScope("HZBBuild");
        PassHZBGeneration(cmd);
        m_profiler.EndScope();
    });

    // ── Material Resolve ─────────────────────────────────────────────
    auto& matPass = m_renderGraph->AddPass("MaterialResolve", PassType::Compute);
    RGTextureDesc albedoDesc{m_width, m_height, 1, 1, Format::RGBA8_UNORM, rwUsage, "GBuffer_Albedo"};
    RGTextureDesc normalDesc{m_width, m_height, 1, 1, Format::RG16_FLOAT, rwUsage, "GBuffer_Normal"};
    auto rgAlbedo = matPass.CreateTexture("GBuffer_Albedo", albedoDesc);
    auto rgNormal = matPass.CreateTexture("GBuffer_Normal", normalDesc);
    matPass.Read(rgVisBuffer);
    matPass.Read(rgDepth);
    matPass.Write(rgAlbedo);
    matPass.Write(rgNormal);
    matPass.SetExecute([this, &frameData](ICommandList* cmd) {
        m_profiler.BeginScope("MaterialResolve");
        PassMaterialResolve(cmd, frameData);
        m_profiler.EndScope();
    });

    // ── Direct Lighting ──────────────────────────────────────────────
    auto& directPass = m_renderGraph->AddPass("DirectLighting", PassType::Compute);
    RGTextureDesc directDesc{m_width, m_height, 1, 1, fmt16F, rwUsage, "DirectLight"};
    auto rgDirect = directPass.CreateTexture("DirectLight", directDesc);
    directPass.Read(rgAlbedo);
    directPass.Read(rgNormal);
    directPass.Read(rgDepth);
    directPass.Write(rgDirect);
    directPass.SetExecute([this, &frameData](ICommandList* cmd) {
        m_profiler.BeginScope("DirectLighting");
        PassDirectLighting(cmd, frameData);
        m_profiler.EndScope();
    });

    // ── Indirect Lighting (GI) ───────────────────────────────────────
    RGResourceHandle rgIndirect;
    if (m_mode == RenderMode::GPUDriven || m_mode == RenderMode::HybridGI) {
        auto& indirectPass = m_renderGraph->AddPass("IndirectLighting", PassType::Compute);
        RGTextureDesc indirectDesc{m_width, m_height, 1, 1, fmt16F, rwUsage, "IndirectLight"};
        rgIndirect = indirectPass.CreateTexture("IndirectLight", indirectDesc);
        indirectPass.Read(rgAlbedo);
        indirectPass.Read(rgNormal);
        indirectPass.Read(rgDepth);
        indirectPass.Write(rgIndirect);
        indirectPass.SetExecute([this, &frameData](ICommandList* cmd) {
            m_profiler.BeginScope("IndirectLighting");
            PassIndirectLighting(cmd, frameData);
            m_profiler.EndScope();
        });
    }

    // ── Post-Processing ──────────────────────────────────────────────
    auto& postPass = m_renderGraph->AddPass("PostProcess", PassType::Compute);
    RGTextureDesc sceneDesc{m_width, m_height, 1, 1, fmt16F, rwUsage, "SceneColor"};
    auto rgScene = postPass.CreateTexture("SceneColor", sceneDesc);
    postPass.Read(rgDirect);
    if (rgIndirect.IsValid()) postPass.Read(rgIndirect);
    postPass.Write(rgScene);
    postPass.SetExecute([this, &frameData](ICommandList* cmd) {
        m_profiler.BeginScope("PostProcess");
        PassPostProcess(cmd, frameData);
        m_profiler.EndScope();
    });

    // ── Composite to Swapchain ───────────────────────────────────────
    auto& compositePass = m_renderGraph->AddPass("Composite", PassType::Graphics);
    auto rgSwapchain = compositePass.ImportTexture("Swapchain", m_device->GetSwapchainTexture(), swapDesc);
    compositePass.Read(rgScene);
    compositePass.Write(rgSwapchain, ResourceState::RenderTarget);
    compositePass.SetExecute([this](ICommandList* cmd) {
        m_profiler.BeginScope("Composite");
        PassComposite(cmd);
        m_profiler.EndScope();
    });

    // Compile and execute
    m_renderGraph->Compile();

    auto* cmd = m_device->GetCommandList();
    cmd->Begin();
    m_renderGraph->Execute(cmd);

    // Post-render callback (e.g. ImGui overlay)
    if (m_postRenderCallback) {
        m_postRenderCallback(cmd);
    }

    // Present barrier
    TextureHandle swapchain = m_device->GetSwapchainTexture();
    cmd->TextureBarrier(swapchain, ResourceState::RenderTarget, ResourceState::Present);
    cmd->End();
    m_device->SubmitCommandList(cmd);

    m_profiler.EndFrame();
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
    rhi::ClearValue clearColor = rhi::ClearValue::Color(1.0f, 0.0f, 1.0f, 1.0f);
    rhi::Viewport viewport{0, 0, static_cast<f32>(data.screenWidth), static_cast<f32>(data.screenHeight), 0, 1};
    rhi::Scissor scissor{0, 0, data.screenWidth, data.screenHeight};

    cmd->TextureBarrier(swapchain, rhi::ResourceState::Present, rhi::ResourceState::RenderTarget);
    cmd->TextureBarrier(m_depthBuffer, rhi::ResourceState::Undefined, rhi::ResourceState::DepthWrite);
    rhi::LoadOp loadOp = rhi::LoadOp::Clear;
    cmd->BeginRendering(&swapchain, 1, m_depthBuffer, &clearColor, viewport, scissor, &loadOp);

    if (m_visBufferPipeline.IsValid() && m_demoVertexBuffer.IsValid() && m_demoIndexBuffer.IsValid() && m_demoIndexCount > 0) {
        DemoPushConstants pushConstants{};
        pushConstants.viewProj = data.viewMatrix * data.projMatrix;
        pushConstants.cameraPos = {data.cameraPosition.x, data.cameraPosition.y, data.cameraPosition.z, 1.0f};
        pushConstants.frameIndex = data.frameIndex;
        pushConstants.screenWidth = data.screenWidth;
        pushConstants.screenHeight = data.screenHeight;
        pushConstants.time = data.time;
        pushConstants.deltaTime = data.deltaTime;

        cmd->BindGraphicsPipeline(m_visBufferPipeline);
        cmd->BindVertexBuffer(m_demoVertexBuffer, 0);
        if (m_demoInstanceBuffer.IsValid()) {
            cmd->BindVertexBuffer(m_demoInstanceBuffer, 1);
        }
        cmd->BindIndexBuffer(m_demoIndexBuffer, rhi::IndexType::UInt16);
        cmd->SetPushConstants(&pushConstants, static_cast<u32>(sizeof(pushConstants)));
        cmd->DrawIndexed(m_demoIndexCount, m_demoInstanceCount);
    }

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

    m_depthBufferInitialized = false;

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
    assets::ShaderCompiler::Init();

    const fs::path shaderRoot = FindShaderRoot();
    if (shaderRoot.empty()) {
        NGE_LOG_ERROR("Failed to locate shader directory for render pipeline");
        return;
    }

    const fs::path cacheDir = shaderRoot / "cache";

    auto createShader = [&](const fs::path& path, rhi::ShaderStage stage) -> rhi::ShaderHandle {
        assets::ShaderCompileOptions options;
        options.sourcePath = path.string();
        options.stage = stage;
        options.includeDirs.push_back(shaderRoot.string());
        options.includeDirs.push_back((shaderRoot / "common").string());

        const std::string spvPath = assets::ShaderCompiler::CompileAndCache(options, cacheDir.string());
        if (spvPath.empty()) {
            return {};
        }

        std::vector<byte> bytecode;
        if (!assets::ShaderCompiler::LoadSPIRV(spvPath, bytecode)) {
            NGE_LOG_ERROR("Failed to load compiled shader bytecode: {}", spvPath);
            return {};
        }

        rhi::ShaderDesc desc{};
        desc.bytecode = bytecode.data();
        desc.bytecodeSize = bytecode.size();
        desc.stage = stage;
        desc.debugName = path.filename().string();
        return m_device->CreateShader(desc);
    };

    m_demoVertexShader = createShader(shaderRoot / "mesh" / "triangle.vert.hlsl", rhi::ShaderStage::Vertex);
    m_demoFragmentShader = createShader(shaderRoot / "mesh" / "triangle.frag.hlsl", rhi::ShaderStage::Fragment);
    if (!m_demoVertexShader.IsValid() || !m_demoFragmentShader.IsValid()) {
        NGE_LOG_ERROR("Failed to create demo shaders for render pipeline");
        return;
    }

    rhi::GraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = m_demoVertexShader;
    pipelineDesc.fragmentShader = m_demoFragmentShader;
    pipelineDesc.vertexBindingCount = 2;
    // Binding 0: per-vertex data (position, normal, color)
    pipelineDesc.vertexBindings[0].binding = 0;
    pipelineDesc.vertexBindings[0].stride = sizeof(DemoVertex);
    pipelineDesc.vertexBindings[0].perInstance = false;
    // Binding 1: per-instance data (offset+scale, color tint)
    pipelineDesc.vertexBindings[1].binding = 1;
    pipelineDesc.vertexBindings[1].stride = sizeof(DemoInstanceData);
    pipelineDesc.vertexBindings[1].perInstance = true;

    pipelineDesc.vertexAttributeCount = 5;
    // location 0: position (float3)
    pipelineDesc.vertexAttributes[0].location = 0;
    pipelineDesc.vertexAttributes[0].binding = 0;
    pipelineDesc.vertexAttributes[0].format = rhi::Format::RGB32_FLOAT;
    pipelineDesc.vertexAttributes[0].offset = 0;
    // location 1: normal (float3)
    pipelineDesc.vertexAttributes[1].location = 1;
    pipelineDesc.vertexAttributes[1].binding = 0;
    pipelineDesc.vertexAttributes[1].format = rhi::Format::RGB32_FLOAT;
    pipelineDesc.vertexAttributes[1].offset = static_cast<u32>(offsetof(DemoVertex, normal));
    // location 2: color (float3)
    pipelineDesc.vertexAttributes[2].location = 2;
    pipelineDesc.vertexAttributes[2].binding = 0;
    pipelineDesc.vertexAttributes[2].format = rhi::Format::RGB32_FLOAT;
    pipelineDesc.vertexAttributes[2].offset = static_cast<u32>(offsetof(DemoVertex, color));
    // location 3: instance offset+scale (float4)
    pipelineDesc.vertexAttributes[3].location = 3;
    pipelineDesc.vertexAttributes[3].binding = 1;
    pipelineDesc.vertexAttributes[3].format = rhi::Format::RGBA32_FLOAT;
    pipelineDesc.vertexAttributes[3].offset = 0;
    // location 4: instance color tint (float4)
    pipelineDesc.vertexAttributes[4].location = 4;
    pipelineDesc.vertexAttributes[4].binding = 1;
    pipelineDesc.vertexAttributes[4].format = rhi::Format::RGBA32_FLOAT;
    pipelineDesc.vertexAttributes[4].offset = static_cast<u32>(offsetof(DemoInstanceData, colorTint));

    pipelineDesc.renderTargetCount = 1;
    pipelineDesc.renderTargets[0].format = m_device->GetSwapchainFormat();
    pipelineDesc.hasDepthStencil = true;
    pipelineDesc.depthStencil.format = rhi::Format::D32_FLOAT;
    pipelineDesc.depthStencil.depthTest = true;
    pipelineDesc.depthStencil.depthWrite = true;
    pipelineDesc.depthStencil.depthCompare = rhi::CompareOp::Greater;
    pipelineDesc.cullMode = rhi::CullMode::Back;
    pipelineDesc.frontFace = rhi::FrontFace::CounterClockwise;
    pipelineDesc.debugName = "RenderPipelineInstancedCubes";
    m_visBufferPipeline = m_device->CreateGraphicsPipeline(pipelineDesc);

    // ─── Cube geometry: 24 vertices (4 per face) with normals ─────────
    // Face order: -Z (front), +Z (back), -X (left), +X (right), -Y (bottom), +Y (top)
    const std::array<DemoVertex, 24> vertices{{
        // Front face (-Z), normal (0,0,-1)
        {{-1.0f, 0.0f, -1.0f}, {0, 0, -1}, {1.0f, 0.2f, 0.2f}},
        {{ 1.0f, 0.0f, -1.0f}, {0, 0, -1}, {1.0f, 0.2f, 0.2f}},
        {{ 1.0f, 2.0f, -1.0f}, {0, 0, -1}, {1.0f, 0.2f, 0.2f}},
        {{-1.0f, 2.0f, -1.0f}, {0, 0, -1}, {1.0f, 0.2f, 0.2f}},
        // Back face (+Z), normal (0,0,1)
        {{ 1.0f, 0.0f,  1.0f}, {0, 0, 1}, {0.2f, 1.0f, 0.2f}},
        {{-1.0f, 0.0f,  1.0f}, {0, 0, 1}, {0.2f, 1.0f, 0.2f}},
        {{-1.0f, 2.0f,  1.0f}, {0, 0, 1}, {0.2f, 1.0f, 0.2f}},
        {{ 1.0f, 2.0f,  1.0f}, {0, 0, 1}, {0.2f, 1.0f, 0.2f}},
        // Left face (-X), normal (-1,0,0)
        {{-1.0f, 0.0f,  1.0f}, {-1, 0, 0}, {0.2f, 0.2f, 1.0f}},
        {{-1.0f, 0.0f, -1.0f}, {-1, 0, 0}, {0.2f, 0.2f, 1.0f}},
        {{-1.0f, 2.0f, -1.0f}, {-1, 0, 0}, {0.2f, 0.2f, 1.0f}},
        {{-1.0f, 2.0f,  1.0f}, {-1, 0, 0}, {0.2f, 0.2f, 1.0f}},
        // Right face (+X), normal (1,0,0)
        {{ 1.0f, 0.0f, -1.0f}, {1, 0, 0}, {1.0f, 1.0f, 0.2f}},
        {{ 1.0f, 0.0f,  1.0f}, {1, 0, 0}, {1.0f, 1.0f, 0.2f}},
        {{ 1.0f, 2.0f,  1.0f}, {1, 0, 0}, {1.0f, 1.0f, 0.2f}},
        {{ 1.0f, 2.0f, -1.0f}, {1, 0, 0}, {1.0f, 1.0f, 0.2f}},
        // Bottom face (-Y), normal (0,-1,0)
        {{-1.0f, 0.0f,  1.0f}, {0, -1, 0}, {0.5f, 0.5f, 0.5f}},
        {{ 1.0f, 0.0f,  1.0f}, {0, -1, 0}, {0.5f, 0.5f, 0.5f}},
        {{ 1.0f, 0.0f, -1.0f}, {0, -1, 0}, {0.5f, 0.5f, 0.5f}},
        {{-1.0f, 0.0f, -1.0f}, {0, -1, 0}, {0.5f, 0.5f, 0.5f}},
        // Top face (+Y), normal (0,1,0)
        {{-1.0f, 2.0f, -1.0f}, {0, 1, 0}, {0.8f, 0.8f, 0.8f}},
        {{ 1.0f, 2.0f, -1.0f}, {0, 1, 0}, {0.8f, 0.8f, 0.8f}},
        {{ 1.0f, 2.0f,  1.0f}, {0, 1, 0}, {0.8f, 0.8f, 0.8f}},
        {{-1.0f, 2.0f,  1.0f}, {0, 1, 0}, {0.8f, 0.8f, 0.8f}},
    }};

    // 36 indices (6 faces × 2 triangles × 3 vertices)
    const std::array<u16, 36> indices{{
        0, 1, 2, 0, 2, 3,       // front
        4, 5, 6, 4, 6, 7,       // back
        8, 9, 10, 8, 10, 11,    // left
        12, 13, 14, 12, 14, 15, // right
        16, 17, 18, 16, 18, 19, // bottom
        20, 21, 22, 20, 22, 23, // top
    }};

    rhi::BufferDesc vertexBufferDesc{};
    vertexBufferDesc.size = sizeof(vertices);
    vertexBufferDesc.usage = rhi::BufferUsage::Vertex;
    vertexBufferDesc.memoryUsage = rhi::MemoryUsage::CPU_To_GPU;
    vertexBufferDesc.debugName = "DemoVertices";
    m_demoVertexBuffer = m_device->CreateBuffer(vertexBufferDesc);

    rhi::BufferDesc indexBufferDesc{};
    indexBufferDesc.size = sizeof(indices);
    indexBufferDesc.usage = rhi::BufferUsage::Index;
    indexBufferDesc.memoryUsage = rhi::MemoryUsage::CPU_To_GPU;
    indexBufferDesc.debugName = "DemoIndices";
    m_demoIndexBuffer = m_device->CreateBuffer(indexBufferDesc);

    if (m_demoVertexBuffer.IsValid() && m_demoIndexBuffer.IsValid()) {
        m_device->UpdateBuffer(m_demoVertexBuffer, vertices.data(), sizeof(vertices));
        m_device->UpdateBuffer(m_demoIndexBuffer, indices.data(), sizeof(indices));
        m_demoIndexCount = static_cast<u32>(indices.size());
    }

    // ─── Instance data: 5×5 grid of cubes with varying heights ────────
    constexpr u32 gridSize = 5;
    constexpr f32 spacing = 4.0f;
    std::vector<DemoInstanceData> instances;
    instances.reserve(gridSize * gridSize);

    for (u32 z = 0; z < gridSize; ++z) {
        for (u32 x = 0; x < gridSize; ++x) {
            DemoInstanceData inst{};
            f32 fx = static_cast<f32>(x) - (gridSize - 1) * 0.5f;
            f32 fz = static_cast<f32>(z) - (gridSize - 1) * 0.5f;
            inst.offsetScale = {fx * spacing, 0.0f, fz * spacing, 1.0f};

            // Color tint based on grid position (HSV-like)
            f32 hue = static_cast<f32>(x + z) / static_cast<f32>(gridSize * 2 - 2);
            inst.colorTint = {
                0.5f + 0.5f * std::sin(hue * 6.28f),
                0.5f + 0.5f * std::sin(hue * 6.28f + 2.09f),
                0.5f + 0.5f * std::sin(hue * 6.28f + 4.19f),
                1.0f
            };
            instances.push_back(inst);
        }
    }

    rhi::BufferDesc instanceBufferDesc{};
    instanceBufferDesc.size = instances.size() * sizeof(DemoInstanceData);
    instanceBufferDesc.usage = rhi::BufferUsage::Vertex;
    instanceBufferDesc.memoryUsage = rhi::MemoryUsage::CPU_To_GPU;
    instanceBufferDesc.debugName = "DemoInstances";
    m_demoInstanceBuffer = m_device->CreateBuffer(instanceBufferDesc);

    if (m_demoInstanceBuffer.IsValid()) {
        m_device->UpdateBuffer(m_demoInstanceBuffer, instances.data(),
                               instances.size() * sizeof(DemoInstanceData));
        m_demoInstanceCount = static_cast<u32>(instances.size());
    }

    NGE_LOG_INFO("Render pipelines created: {} instances", m_demoInstanceCount);
}

} // namespace nge::renderer
