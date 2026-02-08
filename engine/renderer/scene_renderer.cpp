#include "engine/renderer/scene_renderer.h"
#include "engine/core/logging/log.h"
#include <cmath>
#include <chrono>

namespace nge::renderer {

bool SceneRenderer::Init(rhi::IDevice* device, const SceneRendererConfig& config) {
    m_device = device;
    m_config = config;

    u32 renderWidth = static_cast<u32>(config.windowWidth * config.renderScale);
    u32 renderHeight = static_cast<u32>(config.windowHeight * config.renderScale);

    // Frame fence for CPU/GPU sync
    m_frameFence.Init(device, config.framesInFlight);
    m_deletionQueue.Init(&m_frameFence.GetFence());

    // Bindless descriptor table
    rhi::BindlessTableConfig bindlessConfig;
    m_bindless.Init(device, bindlessConfig);

    // Material system
    m_materials.Init(device, 4096);

    // Transient buffer pool
    rhi::BufferPool::Config poolConfig;
    poolConfig.maxFramesInFlight = config.framesInFlight;
    m_bufferPool.Init(device, poolConfig);

    // Staging manager
    m_staging.Init(device);

    // Render compositor
    CompositorConfig compConfig;
    compConfig.renderWidth = renderWidth;
    compConfig.renderHeight = renderHeight;
    compConfig.outputWidth = config.windowWidth;
    compConfig.outputHeight = config.windowHeight;
    compConfig.enablePathTracing = config.enablePathTracing;
    compConfig.enableAsyncCompute = config.enableAsyncCompute;
    m_compositor.Init(device, compConfig);

    // Per-frame constant buffer
    {
        rhi::BufferDesc desc;
        desc.size = 256; // Aligned constant block
        desc.usage = rhi::BufferUsage::Constant | rhi::BufferUsage::TransferDst;
        desc.memoryUsage = rhi::MemoryUsage::CPU_To_GPU;
        desc.debugName = "PerFrameConstants";
        m_perFrameConstantBuffer = device->CreateBuffer(desc);
    }

    // Build TAA jitter sequence
    BuildJitterSequence();

    NGE_LOG_INFO("Scene renderer initialized: {}x{} render, {}x{} output, {} frames in flight",
                 renderWidth, renderHeight, config.windowWidth, config.windowHeight,
                 config.framesInFlight);
    return true;
}

void SceneRenderer::Shutdown() {
    // Wait for all GPU work to complete
    m_frameFence.WaitAll();

    // Flush deferred deletions
    m_deletionQueue.FlushAll();

    m_compositor.Shutdown();
    m_staging.Shutdown();
    m_bufferPool.Shutdown();
    m_materials.Shutdown();
    m_bindless.Shutdown();
    m_frameFence.Shutdown();

    if (m_perFrameConstantBuffer.IsValid()) {
        m_device->DestroyBuffer(m_perFrameConstantBuffer);
        m_perFrameConstantBuffer = {};
    }
}

void SceneRenderer::RenderFrame(ecs::World& world, f32 deltaTime) {
    auto frameStart = std::chrono::high_resolution_clock::now();

    // Wait if GPU is too far behind
    m_frameFence.BeginFrame();

    // Process deferred deletions
    m_deletionQueue.Flush();

    // Reset per-frame resources
    m_bindless.BeginFrame(m_frameNumber);
    m_bufferPool.BeginFrame(m_frameNumber);
    m_staging.BeginFrame();

    // Update time
    m_totalTime += deltaTime;

    // Extract render data from ECS
    ExtractRenderData(world);

    // Upload dirty materials
    m_materials.UploadDirtyMaterials();

    // Build frame data
    m_frameData.deltaTime = deltaTime;
    m_frameData.totalTime = m_totalTime;
    m_frameData.frameIndex = m_frameNumber;
    m_frameData.jitter = GetTAAJitter(m_frameNumber);

    // Upload per-frame constants
    UploadPerFrameData();

    // Flush staging uploads
    auto* graphicsCmd = m_device->GetCommandList();
    m_staging.Flush(graphicsCmd);

    // Execute render compositor
    rhi::ICommandList* asyncCmd = m_config.enableAsyncCompute
        ? m_device->GetAsyncComputeCommandList() : nullptr;

    m_compositor.RenderFrame(m_frameData, graphicsCmd, asyncCmd);

    // End frame — signal GPU completion
    m_bufferPool.EndFrame();
    m_frameFence.EndFrame(graphicsCmd);

    m_frameNumber++;

    auto frameEnd = std::chrono::high_resolution_clock::now();
    m_lastFrameTimeMs = std::chrono::duration<f32, std::milli>(frameEnd - frameStart).count();
}

void SceneRenderer::OnResize(u32 width, u32 height) {
    m_config.windowWidth = width;
    m_config.windowHeight = height;

    u32 renderWidth = static_cast<u32>(width * m_config.renderScale);
    u32 renderHeight = static_cast<u32>(height * m_config.renderScale);

    // Wait for GPU before resizing
    m_frameFence.WaitAll();

    CompositorConfig compConfig = m_compositor.GetConfig();
    compConfig.renderWidth = renderWidth;
    compConfig.renderHeight = renderHeight;
    compConfig.outputWidth = width;
    compConfig.outputHeight = height;

    m_compositor.Shutdown();
    m_compositor.Init(m_device, compConfig);

    NGE_LOG_INFO("Scene renderer resized: {}x{}", width, height);
}

void SceneRenderer::ExtractRenderData(ecs::World& world) {
    // TODO: Query ECS for renderable entities
    // For each entity with MeshRenderer + Transform:
    //   - Collect mesh handle, material ID, world transform
    //   - Build per-object instance data
    //   - Upload to GPU structured buffer for indirect rendering
    //
    // For each entity with Light component:
    //   - Collect light data for clustered culling
    //
    // For each entity with Camera component:
    //   - Extract view/projection matrices

    (void)world;
}

void SceneRenderer::UploadPerFrameData() {
    void* mapped = m_device->MapBuffer(m_perFrameConstantBuffer);
    if (mapped) {
        std::memcpy(mapped, &m_frameData, sizeof(CompositorFrameData));
        m_device->UnmapBuffer(m_perFrameConstantBuffer);
    }
}

void SceneRenderer::BuildJitterSequence() {
    // Halton sequence (base 2, 3) for TAA jitter
    auto halton = [](u32 index, u32 base) -> f32 {
        f32 result = 0.0f;
        f32 f = 1.0f / static_cast<f32>(base);
        u32 i = index;
        while (i > 0) {
            result += f * static_cast<f32>(i % base);
            i /= base;
            f /= static_cast<f32>(base);
        }
        return result;
    };

    for (u32 i = 0; i < JITTER_SAMPLES; ++i) {
        m_jitterSequence[i].x = halton(i + 1, 2) - 0.5f;
        m_jitterSequence[i].y = halton(i + 1, 3) - 0.5f;
    }
}

math::Vec2 SceneRenderer::GetTAAJitter(u32 frameIndex) const {
    u32 idx = frameIndex % JITTER_SAMPLES;
    return m_jitterSequence[idx];
}

} // namespace nge::renderer
