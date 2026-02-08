#include "engine/renderer/pipeline/gpu_culling_pipeline.h"
#include "engine/core/logging/log.h"
#include <cstring>

namespace nge::renderer {

bool GPUCullingPipeline::Init(rhi::IDevice* device, const GPUCullingConfig& config) {
    m_device = device;
    m_config = config;

    // Visibility flags: one u32 per meshlet group
    {
        rhi::BufferDesc desc;
        desc.size = config.maxMeshletGroups * sizeof(u32);
        desc.usage = rhi::BufferUsage::Storage;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "CullingVisibility";
        m_visibilityBuffer = device->CreateBuffer(desc);
    }

    // Selected meshlet buffer
    {
        rhi::BufferDesc desc;
        desc.size = config.maxMeshletGroups * 16; // SelectedMeshlet = 16 bytes
        desc.usage = rhi::BufferUsage::Storage;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "SelectedMeshlets";
        m_selectedMeshletBuffer = device->CreateBuffer(desc);
    }

    // Selected count
    {
        rhi::BufferDesc desc;
        desc.size = sizeof(u32);
        desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "SelectedCount";
        m_selectedCountBuffer = device->CreateBuffer(desc);
    }

    // Early draw commands
    {
        rhi::BufferDesc desc;
        desc.size = config.maxDrawCommands * 20; // DrawIndexedCommand = 20 bytes
        desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "EarlyDrawCommands";
        m_drawCommandBuffer = device->CreateBuffer(desc);
    }

    // Early draw count
    {
        rhi::BufferDesc desc;
        desc.size = sizeof(u32);
        desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "EarlyDrawCount";
        m_drawCountBuffer = device->CreateBuffer(desc);
    }

    // Late draw commands
    {
        rhi::BufferDesc desc;
        desc.size = config.maxDrawCommands * 20;
        desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "LateDrawCommands";
        m_lateDrawCommandBuffer = device->CreateBuffer(desc);
    }

    // Late draw count
    {
        rhi::BufferDesc desc;
        desc.size = sizeof(u32);
        desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "LateDrawCount";
        m_lateDrawCountBuffer = device->CreateBuffer(desc);
    }

    // Constant buffer
    {
        rhi::BufferDesc desc;
        desc.size = 256; // Aligned push constant block
        desc.usage = rhi::BufferUsage::Constant | rhi::BufferUsage::TransferDst;
        desc.memoryUsage = rhi::MemoryUsage::CPU_To_GPU;
        desc.debugName = "CullingConstants";
        m_constantBuffer = device->CreateBuffer(desc);
    }

    // TODO: Create compute pipelines from compiled shaders
    // m_earlyOcclusionPipeline = CreateComputePipeline("occlusion_cull.hlsl", "CSEarlyPass");
    // m_lodSelectionPipeline = CreateComputePipeline("meshlet_lod.hlsl", "CSMain");
    // m_drawBuildPipeline = CreateComputePipeline("indirect_draw_build.hlsl", "CSBuildMDI");
    // m_lateOcclusionPipeline = CreateComputePipeline("occlusion_cull.hlsl", "CSLatePass");
    // m_counterResetPipeline = CreateComputePipeline("indirect_draw_build.hlsl", "CSResetCounters");

    NGE_LOG_INFO("GPU culling pipeline initialized: max {} groups, {} draws",
                 config.maxMeshletGroups, config.maxDrawCommands);
    return true;
}

void GPUCullingPipeline::Shutdown() {
    if (!m_device) return;

    auto destroy = [&](rhi::BufferHandle& h) {
        if (h.IsValid()) { m_device->DestroyBuffer(h); h = {}; }
    };

    destroy(m_visibilityBuffer);
    destroy(m_selectedMeshletBuffer);
    destroy(m_selectedCountBuffer);
    destroy(m_drawCommandBuffer);
    destroy(m_drawCountBuffer);
    destroy(m_lateDrawCommandBuffer);
    destroy(m_lateDrawCountBuffer);
    destroy(m_constantBuffer);
}

void GPUCullingPipeline::BeginFrame(const GPUCullingFrameData& frameData) {
    m_frameData = frameData;

    // Upload constants
    void* mapped = m_device->MapBuffer(m_constantBuffer);
    if (mapped) {
        std::memcpy(mapped, &m_frameData, sizeof(GPUCullingFrameData));
        m_device->UnmapBuffer(m_constantBuffer);
    }
}

void GPUCullingPipeline::ResetCounters(rhi::ICommandList* cmd) {
    // Clear draw count buffers to zero
    cmd->FillBuffer(m_drawCountBuffer, 0, sizeof(u32), 0);
    cmd->FillBuffer(m_lateDrawCountBuffer, 0, sizeof(u32), 0);
    cmd->FillBuffer(m_selectedCountBuffer, 0, sizeof(u32), 0);

    cmd->BufferBarrier(m_drawCountBuffer, rhi::ResourceState::TransferDst, rhi::ResourceState::ShaderWrite);
    cmd->BufferBarrier(m_lateDrawCountBuffer, rhi::ResourceState::TransferDst, rhi::ResourceState::ShaderWrite);
    cmd->BufferBarrier(m_selectedCountBuffer, rhi::ResourceState::TransferDst, rhi::ResourceState::ShaderWrite);
}

void GPUCullingPipeline::EarlyCull(rhi::ICommandList* cmd) {
    if (!m_config.enableFrustumCull && !m_config.enableOcclusionCull) return;

    cmd->BeginDebugLabel("EarlyCull", 0.8f, 0.2f, 0.2f);

    // Bind occlusion cull pipeline
    // cmd->BindComputePipeline(m_earlyOcclusionPipeline);
    // cmd->BindBuffer(0, m_meshletGroupBuffer);    // Meshlet groups input
    // cmd->BindBuffer(1, m_visibilityBuffer);       // Visibility output
    // cmd->BindBuffer(2, m_constantBuffer);          // Constants
    // cmd->BindTexture(3, m_prevHzbTexture);         // Previous frame HZB

    u32 groupCount = m_frameData.meshletGroupCount;
    u32 dispatchX = (groupCount + 63) / 64;
    cmd->Dispatch(dispatchX, 1, 1);

    // Barrier: visibility buffer write → read
    cmd->BufferBarrier(m_visibilityBuffer, rhi::ResourceState::ShaderWrite, rhi::ResourceState::ShaderRead);

    cmd->EndDebugLabel();
}

void GPUCullingPipeline::SelectLODs(rhi::ICommandList* cmd) {
    if (!m_config.enableLODSelection) return;

    cmd->BeginDebugLabel("LODSelection", 0.2f, 0.8f, 0.2f);

    // Bind LOD selection pipeline
    // cmd->BindComputePipeline(m_lodSelectionPipeline);
    // cmd->BindBuffer(0, m_meshletGroupBuffer);     // Groups
    // cmd->BindBuffer(1, m_visibilityBuffer);        // Visibility (early cull result)
    // cmd->BindBuffer(2, m_selectedMeshletBuffer);   // Selected output
    // cmd->BindBuffer(3, m_selectedCountBuffer);     // Count output
    // cmd->BindBuffer(4, m_constantBuffer);

    u32 groupCount = m_frameData.meshletGroupCount;
    u32 dispatchX = (groupCount + 63) / 64;
    cmd->Dispatch(dispatchX, 1, 1);

    cmd->BufferBarrier(m_selectedMeshletBuffer, rhi::ResourceState::ShaderWrite, rhi::ResourceState::ShaderRead);
    cmd->BufferBarrier(m_selectedCountBuffer, rhi::ResourceState::ShaderWrite, rhi::ResourceState::ShaderRead);

    cmd->EndDebugLabel();
}

void GPUCullingPipeline::BuildDrawArgs(rhi::ICommandList* cmd) {
    cmd->BeginDebugLabel("BuildDrawArgs", 0.2f, 0.2f, 0.8f);

    // Bind indirect draw build pipeline
    // cmd->BindComputePipeline(m_drawBuildPipeline);
    // cmd->BindBuffer(0, m_selectedMeshletBuffer);  // Selected meshlets
    // cmd->BindBuffer(1, m_drawCommandBuffer);       // Draw commands output
    // cmd->BindBuffer(2, m_drawCountBuffer);          // Draw count output

    // Dispatch based on selected count (use indirect dispatch from selectedCountBuffer)
    // For now, dispatch based on max possible
    u32 dispatchX = (m_config.maxDrawCommands + 63) / 64;
    cmd->Dispatch(dispatchX, 1, 1);

    cmd->BufferBarrier(m_drawCommandBuffer, rhi::ResourceState::ShaderWrite, rhi::ResourceState::IndirectArgument);
    cmd->BufferBarrier(m_drawCountBuffer, rhi::ResourceState::ShaderWrite, rhi::ResourceState::IndirectArgument);

    cmd->EndDebugLabel();
}

void GPUCullingPipeline::LateCull(rhi::ICommandList* cmd) {
    if (!m_config.enableOcclusionCull) return;

    cmd->BeginDebugLabel("LateCull", 0.8f, 0.6f, 0.2f);

    // Re-test previously occluded meshlet groups against the current frame's HZB
    // cmd->BindComputePipeline(m_lateOcclusionPipeline);
    // cmd->BindBuffer(0, m_meshletGroupBuffer);
    // cmd->BindBuffer(1, m_visibilityBuffer);       // Update visibility
    // cmd->BindBuffer(2, m_constantBuffer);
    // cmd->BindTexture(3, m_hzbTexture);            // Current frame HZB

    u32 groupCount = m_frameData.meshletGroupCount;
    u32 dispatchX = (groupCount + 63) / 64;
    cmd->Dispatch(dispatchX, 1, 1);

    cmd->BufferBarrier(m_visibilityBuffer, rhi::ResourceState::ShaderWrite, rhi::ResourceState::ShaderRead);

    cmd->EndDebugLabel();
}

void GPUCullingPipeline::BuildLateDrawArgs(rhi::ICommandList* cmd) {
    cmd->BeginDebugLabel("BuildLateDrawArgs", 0.6f, 0.2f, 0.8f);

    // Build draw args for newly-visible geometry from late cull
    // Same pipeline as BuildDrawArgs but outputs to late buffers

    u32 dispatchX = (m_config.maxDrawCommands + 63) / 64;
    cmd->Dispatch(dispatchX, 1, 1);

    cmd->BufferBarrier(m_lateDrawCommandBuffer, rhi::ResourceState::ShaderWrite, rhi::ResourceState::IndirectArgument);
    cmd->BufferBarrier(m_lateDrawCountBuffer, rhi::ResourceState::ShaderWrite, rhi::ResourceState::IndirectArgument);

    cmd->EndDebugLabel();
}

} // namespace nge::renderer
