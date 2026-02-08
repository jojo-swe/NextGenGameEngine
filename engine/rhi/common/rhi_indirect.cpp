#include "engine/rhi/common/rhi_indirect.h"
#include "engine/core/logging/log.h"
#include <cstring>

namespace nge::rhi {

bool IndirectBufferBuilder::Init(IDevice* device, u32 maxDrawCommands, u32 maxDispatchCommands) {
    m_device = device;
    m_maxDrawCommands = maxDrawCommands;
    m_maxDispatchCommands = maxDispatchCommands;

    m_cpuDrawCommands.reserve(maxDrawCommands);
    m_cpuMeshTasksCommands.reserve(maxDrawCommands);
    m_cpuDispatchCommands.reserve(maxDispatchCommands);

    // Draw indexed indirect buffer
    {
        BufferDesc desc;
        desc.size = maxDrawCommands * sizeof(DrawIndexedIndirectCommand);
        desc.usage = BufferUsage::Indirect | BufferUsage::Storage | BufferUsage::TransferDst;
        desc.memoryUsage = MemoryUsage::GPU_Only;
        desc.debugName = "IndirectDrawBuffer";
        m_drawBuffer = device->CreateBuffer(desc);
    }

    // Draw count buffer (single u32)
    {
        BufferDesc desc;
        desc.size = sizeof(u32);
        desc.usage = BufferUsage::Indirect | BufferUsage::Storage | BufferUsage::TransferDst;
        desc.memoryUsage = MemoryUsage::GPU_Only;
        desc.debugName = "IndirectDrawCount";
        m_drawCountBuffer = device->CreateBuffer(desc);
    }

    // Staging buffer for CPU → GPU upload
    {
        BufferDesc desc;
        desc.size = maxDrawCommands * sizeof(DrawIndexedIndirectCommand);
        desc.usage = BufferUsage::TransferSrc;
        desc.memoryUsage = MemoryUsage::CPU_To_GPU;
        desc.debugName = "IndirectDrawStaging";
        m_drawStagingBuffer = device->CreateBuffer(desc);
    }

    // Mesh tasks buffer
    {
        BufferDesc desc;
        desc.size = maxDrawCommands * sizeof(DrawMeshTasksIndirectCommand);
        desc.usage = BufferUsage::Indirect | BufferUsage::Storage | BufferUsage::TransferDst;
        desc.memoryUsage = MemoryUsage::GPU_Only;
        desc.debugName = "IndirectMeshTasksBuffer";
        m_meshTasksBuffer = device->CreateBuffer(desc);
    }

    // Dispatch indirect buffer
    {
        BufferDesc desc;
        desc.size = maxDispatchCommands * sizeof(DispatchIndirectCommand);
        desc.usage = BufferUsage::Indirect | BufferUsage::Storage | BufferUsage::TransferDst;
        desc.memoryUsage = MemoryUsage::GPU_Only;
        desc.debugName = "IndirectDispatchBuffer";
        m_dispatchBuffer = device->CreateBuffer(desc);
    }

    NGE_LOG_INFO("Indirect buffer builder initialized: max {} draws, {} dispatches",
                 maxDrawCommands, maxDispatchCommands);
    return true;
}

void IndirectBufferBuilder::Shutdown() {
    if (!m_device) return;

    auto destroy = [&](BufferHandle& h) {
        if (h.IsValid()) { m_device->DestroyBuffer(h); h = {}; }
    };

    destroy(m_drawBuffer);
    destroy(m_drawCountBuffer);
    destroy(m_drawStagingBuffer);
    destroy(m_meshTasksBuffer);
    destroy(m_dispatchBuffer);

    m_cpuDrawCommands.clear();
    m_cpuMeshTasksCommands.clear();
    m_cpuDispatchCommands.clear();
}

void IndirectBufferBuilder::BeginFrame() {
    m_cpuDrawCommands.clear();
    m_cpuMeshTasksCommands.clear();
    m_cpuDispatchCommands.clear();
    m_drawCount = 0;
    m_meshTasksCount = 0;
    m_dispatchCount = 0;
}

u32 IndirectBufferBuilder::AddDrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex,
                                            i32 vertexOffset, u32 firstInstance) {
    if (m_drawCount >= m_maxDrawCommands) {
        NGE_LOG_WARN("Indirect draw command limit reached");
        return UINT32_MAX;
    }

    DrawIndexedIndirectCommand cmd;
    cmd.indexCount = indexCount;
    cmd.instanceCount = instanceCount;
    cmd.firstIndex = firstIndex;
    cmd.vertexOffset = vertexOffset;
    cmd.firstInstance = firstInstance;
    m_cpuDrawCommands.push_back(cmd);
    return m_drawCount++;
}

u32 IndirectBufferBuilder::AddMeshTasksDispatch(u32 groupX, u32 groupY, u32 groupZ) {
    if (m_meshTasksCount >= m_maxDrawCommands) return UINT32_MAX;

    DrawMeshTasksIndirectCommand cmd;
    cmd.groupCountX = groupX;
    cmd.groupCountY = groupY;
    cmd.groupCountZ = groupZ;
    m_cpuMeshTasksCommands.push_back(cmd);
    return m_meshTasksCount++;
}

u32 IndirectBufferBuilder::AddComputeDispatch(u32 groupX, u32 groupY, u32 groupZ) {
    if (m_dispatchCount >= m_maxDispatchCommands) return UINT32_MAX;

    DispatchIndirectCommand cmd;
    cmd.groupCountX = groupX;
    cmd.groupCountY = groupY;
    cmd.groupCountZ = groupZ;
    m_cpuDispatchCommands.push_back(cmd);
    return m_dispatchCount++;
}

void IndirectBufferBuilder::Upload(ICommandList* cmd) {
    // Upload draw commands
    if (!m_cpuDrawCommands.empty()) {
        u32 size = static_cast<u32>(m_cpuDrawCommands.size() * sizeof(DrawIndexedIndirectCommand));
        void* mapped = m_device->MapBuffer(m_drawStagingBuffer);
        if (mapped) {
            std::memcpy(mapped, m_cpuDrawCommands.data(), size);
            m_device->UnmapBuffer(m_drawStagingBuffer);
        }
        cmd->CopyBuffer(m_drawStagingBuffer, 0, m_drawBuffer, 0, size);
        cmd->BufferBarrier(m_drawBuffer, ResourceState::TransferDst, ResourceState::IndirectArgument);

        // Write draw count
        cmd->FillBuffer(m_drawCountBuffer, 0, sizeof(u32), m_drawCount);
        cmd->BufferBarrier(m_drawCountBuffer, ResourceState::TransferDst, ResourceState::IndirectArgument);
    }

    // TODO: Upload mesh tasks and dispatch commands similarly
}

void IndirectBufferBuilder::ResetDrawCount(ICommandList* cmd) {
    cmd->FillBuffer(m_drawCountBuffer, 0, sizeof(u32), 0);
    cmd->BufferBarrier(m_drawCountBuffer, ResourceState::TransferDst, ResourceState::ShaderWrite);
}

} // namespace nge::rhi
