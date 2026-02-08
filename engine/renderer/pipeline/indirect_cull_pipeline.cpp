#include "engine/renderer/pipeline/indirect_cull_pipeline.h"
#include "engine/core/logging/log.h"

namespace nge::renderer {

bool IndirectCullPipeline::Init(rhi::IDevice* device, const IndirectCullConfig& config) {
    m_device = device;
    m_config = config;

    // Visible instance list buffer
    {
        rhi::BufferDesc desc;
        desc.size = config.maxInstances * sizeof(u32);
        desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::IndirectArgs;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "CullVisibleList";
        m_visibleBuffer = device->CreateBuffer(desc);
    }

    // Occluded list (early → late pass)
    {
        rhi::BufferDesc desc;
        desc.size = config.maxInstances * sizeof(u32);
        desc.usage = rhi::BufferUsage::Storage;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "CullOccludedList";
        m_occludedBuffer = device->CreateBuffer(desc);
    }

    // Indirect draw arguments
    {
        rhi::BufferDesc desc;
        desc.size = config.maxDrawCommands * 20; // 5 × u32 per DrawIndexedIndirect
        desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::IndirectArgs;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "CullDrawArgs";
        m_drawArgsBuffer = device->CreateBuffer(desc);
    }

    // Atomic counters (visible count, occluded count, draw count)
    {
        rhi::BufferDesc desc;
        desc.size = 16; // 4 × u32
        desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "CullCounters";
        m_counterBuffer = device->CreateBuffer(desc);
    }

    // Initialize occlusion feedback
    if (config.enableFeedbackReadback) {
        OcclusionFeedbackConfig fbConfig;
        fbConfig.maxInstances = config.maxInstances;
        m_occlusionFeedback.Init(device, fbConfig);
    }

    // TODO: Create compute pipelines from compiled shaders
    // m_frustumCullPipeline = CreateComputePipeline("frustum_cull.hlsl", "CSMain");
    // m_earlyCullPipeline = CreateComputePipeline("occlusion_cull.hlsl", "CSEarlyCull");
    // m_lateCullPipeline = CreateComputePipeline("occlusion_cull.hlsl", "CSLateCull");
    // m_buildArgsPipeline = CreateComputePipeline("occlusion_cull.hlsl", "CSBuildDrawArgs");
    // m_hizBuildPipeline = CreateComputePipeline("hiz_build.hlsl", "CSSinglePass");

    NGE_LOG_INFO("Indirect cull pipeline initialized: {} max instances, early/late={}, feedback={}",
                 config.maxInstances, config.enableEarlyLateCull, config.enableFeedbackReadback);
    return true;
}

void IndirectCullPipeline::Shutdown() {
    m_occlusionFeedback.Shutdown();

    auto destroy = [&](rhi::BufferHandle& h) {
        if (h.IsValid()) { m_device->DestroyBuffer(h); h = {}; }
    };

    destroy(m_visibleBuffer);
    destroy(m_occludedBuffer);
    destroy(m_drawArgsBuffer);
    destroy(m_counterBuffer);
}

void IndirectCullPipeline::BeginFrame(rhi::ICommandList* cmd, u32 frameIndex) {
    // Reset counters to zero
    u32 zeros[4] = {0, 0, 0, 0};
    cmd->UpdateBuffer(m_counterBuffer, 0, zeros, sizeof(zeros));
    cmd->BufferBarrier(m_counterBuffer, rhi::ResourceState::TransferDst, rhi::ResourceState::UnorderedAccess);

    if (m_config.enableFeedbackReadback) {
        m_occlusionFeedback.BeginFrame(frameIndex);
        m_occlusionFeedback.ReadBack();
    }

    m_stats = {};
}

void IndirectCullPipeline::SetSceneBuffer(const GPUSceneBuffer* sceneBuffer) {
    m_sceneBuffer = sceneBuffer;
    m_stats.totalInstances = sceneBuffer ? sceneBuffer->GetInstanceCount() : 0;
}

void IndirectCullPipeline::ExecuteEarlyCull(rhi::ICommandList* cmd) {
    if (!m_sceneBuffer || m_sceneBuffer->GetInstanceCount() == 0) return;

    u32 instanceCount = m_sceneBuffer->GetInstanceCount();
    u32 groupCount = (instanceCount + 63) / 64;

    // TODO: Bind resources and dispatch
    // cmd->BindComputePipeline(m_earlyCullPipeline);
    // cmd->BindBuffer(0, m_sceneBuffer->GetInstanceBuffer());
    // cmd->BindBuffer(1, m_visibleBuffer);
    // cmd->BindBuffer(2, m_counterBuffer);
    // cmd->BindBuffer(3, m_occludedBuffer);
    // cmd->SetPushConstants(&cullConstants, sizeof(CullConstants));
    // cmd->Dispatch(groupCount, 1, 1);

    // Barrier: UAV → UAV for late pass
    cmd->BufferBarrier(m_visibleBuffer, rhi::ResourceState::UnorderedAccess, rhi::ResourceState::UnorderedAccess);
    cmd->BufferBarrier(m_counterBuffer, rhi::ResourceState::UnorderedAccess, rhi::ResourceState::UnorderedAccess);
    cmd->BufferBarrier(m_occludedBuffer, rhi::ResourceState::UnorderedAccess, rhi::ResourceState::ShaderRead);

    (void)groupCount;
}

void IndirectCullPipeline::BuildHiZ(rhi::ICommandList* cmd) {
    // TODO: Dispatch HiZ pyramid builder from current frame's depth
    // cmd->BindComputePipeline(m_hizBuildPipeline);
    // cmd->BindTexture(0, depthBuffer);
    // cmd->BindUAV(0, hizMip1); ... cmd->BindUAV(4, hizMip5);
    // cmd->Dispatch(dispatchX, dispatchY, 1);
    (void)cmd;
}

void IndirectCullPipeline::ExecuteLateCull(rhi::ICommandList* cmd) {
    if (!m_config.enableEarlyLateCull) return;

    // TODO: Dispatch late cull pass using current frame HZB
    // cmd->BindComputePipeline(m_lateCullPipeline);
    // cmd->Dispatch(lateGroupCount, 1, 1);
    (void)cmd;
}

void IndirectCullPipeline::BuildDrawArgs(rhi::ICommandList* cmd) {
    // TODO: Dispatch draw argument builder
    // cmd->BindComputePipeline(m_buildArgsPipeline);
    // cmd->Dispatch(1, 1, 1);

    // Barrier: UAV → indirect args
    cmd->BufferBarrier(m_drawArgsBuffer, rhi::ResourceState::UnorderedAccess, rhi::ResourceState::IndirectArgs);
    cmd->BufferBarrier(m_visibleBuffer, rhi::ResourceState::UnorderedAccess, rhi::ResourceState::ShaderRead);
}

void IndirectCullPipeline::ReadbackVisibility(rhi::ICommandList* cmd) {
    if (!m_config.enableFeedbackReadback) return;
    m_occlusionFeedback.RecordResults(cmd, m_visibleBuffer, m_stats.totalInstances);
}

void IndirectCullPipeline::EndFrame(rhi::ICommandList* cmd) {
    (void)cmd;
    // Stats will be populated from counter readback in future frames
}

IndirectCullStats IndirectCullPipeline::GetStats() const {
    std::lock_guard lock(m_mutex);
    auto stats = m_stats;
    stats.cullEfficiency = stats.totalInstances > 0
        ? 1.0f - static_cast<f32>(stats.finalVisible) / static_cast<f32>(stats.totalInstances)
        : 0.0f;
    return stats;
}

} // namespace nge::renderer
