#include "engine/rhi/common/rhi_indirect_count.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool IndirectCountBuilder::Init(IDevice* device) {
    m_device = device;
    NGE_LOG_INFO("Indirect count builder initialized");
    return true;
}

void IndirectCountBuilder::Shutdown() {
    for (auto& buf : m_buffers) {
        if (buf.alive) {
            // TODO: device->DestroyBuffer(buf.argBuffer);
            // TODO: device->DestroyBuffer(buf.countBuffer);
            buf.alive = false;
        }
    }
    m_buffers.clear();
}

u32 IndirectCountBuilder::CreateBuffer(const IndirectCountBufferDesc& desc) {
    std::lock_guard lock(m_mutex);

    IndirectCountBuffer buf;
    buf.desc = desc;
    buf.alive = true;

    // Determine stride based on type
    switch (desc.type) {
        case IndirectCountType::Draw:
            buf.stride = sizeof(IndirectDrawArgs);
            break;
        case IndirectCountType::DrawIndexed:
            buf.stride = sizeof(IndirectDrawIndexedArgs);
            break;
        case IndirectCountType::MeshTasks:
            buf.stride = sizeof(IndirectMeshTasksArgs);
            break;
        case IndirectCountType::Dispatch:
            buf.stride = sizeof(IndirectDispatchArgs);
            break;
    }

    // TODO: Create argument buffer
    // BufferDesc argDesc{};
    // argDesc.size = buf.stride * desc.maxDrawCount;
    // argDesc.usage = BufferUsage::IndirectBuffer | BufferUsage::StorageBuffer | BufferUsage::TransferDst;
    // argDesc.memoryType = MemoryType::DeviceLocal;
    // buf.argBuffer = device->CreateBuffer(argDesc);

    // TODO: Create count buffer (single u32)
    // BufferDesc countDesc{};
    // countDesc.size = sizeof(u32);
    // countDesc.usage = BufferUsage::IndirectBuffer | BufferUsage::StorageBuffer | BufferUsage::TransferDst;
    // countDesc.memoryType = MemoryType::DeviceLocal;
    // buf.countBuffer = device->CreateBuffer(countDesc);

    u32 id = static_cast<u32>(m_buffers.size());
    m_buffers.push_back(std::move(buf));

    NGE_LOG_INFO("Indirect count buffer '{}' created: type={}, maxDraws={}, stride={}",
                 desc.debugName, static_cast<u32>(desc.type), desc.maxDrawCount, m_buffers[id].stride);
    return id;
}

void IndirectCountBuilder::DestroyBuffer(u32 bufferId) {
    std::lock_guard lock(m_mutex);
    if (bufferId >= m_buffers.size()) return;
    auto& buf = m_buffers[bufferId];
    if (!buf.alive) return;

    // TODO: device->DestroyBuffer(buf.argBuffer);
    // TODO: device->DestroyBuffer(buf.countBuffer);
    buf.alive = false;
}

BufferHandle IndirectCountBuilder::GetArgBuffer(u32 bufferId) const {
    std::lock_guard lock(m_mutex);
    if (bufferId >= m_buffers.size()) return {};
    return m_buffers[bufferId].argBuffer;
}

BufferHandle IndirectCountBuilder::GetCountBuffer(u32 bufferId) const {
    std::lock_guard lock(m_mutex);
    if (bufferId >= m_buffers.size()) return {};
    return m_buffers[bufferId].countBuffer;
}

void IndirectCountBuilder::ClearCount(ICommandList* cmd, u32 bufferId) {
    std::lock_guard lock(m_mutex);
    if (bufferId >= m_buffers.size()) return;
    if (!m_buffers[bufferId].alive) return;

    // TODO: vkCmdFillBuffer(cmd, m_buffers[bufferId].countBuffer, 0, sizeof(u32), 0);
    (void)cmd;
}

void IndirectCountBuilder::DrawIndirectCount(ICommandList* cmd, u32 bufferId) {
    std::lock_guard lock(m_mutex);
    if (bufferId >= m_buffers.size()) return;
    const auto& buf = m_buffers[bufferId];
    if (!buf.alive) return;

    // TODO: vkCmdDrawIndirectCount(
    //     cmd->GetVkCommandBuffer(),
    //     buf.argBuffer,   // buffer with draw args
    //     0,               // offset
    //     buf.countBuffer, // buffer with draw count
    //     0,               // countOffset
    //     buf.desc.maxDrawCount,
    //     buf.stride);
    (void)cmd;
}

void IndirectCountBuilder::DrawIndexedIndirectCount(ICommandList* cmd, u32 bufferId) {
    std::lock_guard lock(m_mutex);
    if (bufferId >= m_buffers.size()) return;
    const auto& buf = m_buffers[bufferId];
    if (!buf.alive) return;

    // TODO: vkCmdDrawIndexedIndirectCount(
    //     cmd->GetVkCommandBuffer(),
    //     buf.argBuffer, 0,
    //     buf.countBuffer, 0,
    //     buf.desc.maxDrawCount,
    //     buf.stride);
    (void)cmd;
}

void IndirectCountBuilder::DrawMeshTasksIndirectCount(ICommandList* cmd, u32 bufferId) {
    std::lock_guard lock(m_mutex);
    if (bufferId >= m_buffers.size()) return;
    const auto& buf = m_buffers[bufferId];
    if (!buf.alive) return;

    // TODO: vkCmdDrawMeshTasksIndirectCountEXT(
    //     cmd->GetVkCommandBuffer(),
    //     buf.argBuffer, 0,
    //     buf.countBuffer, 0,
    //     buf.desc.maxDrawCount,
    //     buf.stride);
    (void)cmd;
}

void IndirectCountBuilder::DispatchIndirect(ICommandList* cmd, u32 bufferId) {
    std::lock_guard lock(m_mutex);
    if (bufferId >= m_buffers.size()) return;
    const auto& buf = m_buffers[bufferId];
    if (!buf.alive) return;

    // TODO: vkCmdDispatchIndirect(cmd->GetVkCommandBuffer(), buf.argBuffer, 0);
    (void)cmd;
}

u32 IndirectCountBuilder::GetMaxDrawCount(u32 bufferId) const {
    std::lock_guard lock(m_mutex);
    if (bufferId >= m_buffers.size()) return 0;
    return m_buffers[bufferId].desc.maxDrawCount;
}

IndirectCountBufferStats IndirectCountBuilder::GetStats() const {
    std::lock_guard lock(m_mutex);
    IndirectCountBufferStats stats{};
    for (const auto& buf : m_buffers) {
        if (buf.alive) {
            stats.activeBuffers++;
            stats.totalMaxDraws += buf.desc.maxDrawCount;
            stats.totalMemoryBytes += static_cast<u64>(buf.stride) * buf.desc.maxDrawCount + sizeof(u32);
        }
    }
    return stats;
}

} // namespace nge::rhi
