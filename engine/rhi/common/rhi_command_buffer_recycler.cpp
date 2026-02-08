#include "engine/rhi/common/rhi_command_buffer_recycler.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool CommandBufferRecycler::Init(IDevice* device, const CommandBufferRecyclerConfig& config) {
    m_device = device;
    m_config = config;
    m_currentFrame = 0;
    m_growthEvents = 0;
    m_recycleEvents = 0;

    GrowPool(config.initialSecondaryCount, 0);

    NGE_LOG_INFO("Command buffer recycler initialized: initial={}, max={}, recycleAfter={} frames",
                 config.initialSecondaryCount, config.maxSecondaryCount, config.framesBeforeRecycle);
    return true;
}

void CommandBufferRecycler::Shutdown() {
    for (auto& buf : m_buffers) {
        // TODO: vkFreeCommandBuffers(device, buf.poolHandle, 1, &buf.handle);
        buf.handle = 0;
    }
    m_buffers.clear();
    while (!m_available.empty()) m_available.pop();
}

u32 CommandBufferRecycler::AcquireSecondary(u32 queueFamily, const std::string& debugName) {
    std::lock_guard lock(m_mutex);

    if (m_available.empty()) {
        if (!m_config.allowGrowth || m_buffers.size() >= m_config.maxSecondaryCount) {
            NGE_LOG_ERROR("Command buffer recycler exhausted: max={}", m_config.maxSecondaryCount);
            return UINT32_MAX;
        }
        u32 growCount = std::min(16u, m_config.maxSecondaryCount - static_cast<u32>(m_buffers.size()));
        GrowPool(growCount, queueFamily);
        m_growthEvents++;
    }

    u32 id = m_available.front();
    m_available.pop();

    m_buffers[id].inUse = true;
    m_buffers[id].debugName = debugName;
    m_buffers[id].lastUsedFrame = m_currentFrame;
    m_buffers[id].queueFamily = queueFamily;

    return id;
}

void CommandBufferRecycler::MarkRecorded(u32 bufferId) {
    std::lock_guard lock(m_mutex);
    if (bufferId >= m_buffers.size()) return;
    m_buffers[bufferId].recorded = true;
}

void CommandBufferRecycler::MarkDirty(u32 bufferId) {
    std::lock_guard lock(m_mutex);
    if (bufferId >= m_buffers.size()) return;
    m_buffers[bufferId].recorded = false;
}

bool CommandBufferRecycler::IsRecorded(u32 bufferId) const {
    std::lock_guard lock(m_mutex);
    if (bufferId >= m_buffers.size()) return false;
    return m_buffers[bufferId].recorded;
}

void CommandBufferRecycler::Release(u32 bufferId) {
    std::lock_guard lock(m_mutex);
    if (bufferId >= m_buffers.size()) return;
    if (!m_buffers[bufferId].inUse) return;

    m_buffers[bufferId].inUse = false;
    m_buffers[bufferId].recorded = false;
    m_buffers[bufferId].debugName.clear();
    m_available.push(bufferId);
}

u64 CommandBufferRecycler::GetHandle(u32 bufferId) const {
    std::lock_guard lock(m_mutex);
    if (bufferId >= m_buffers.size()) return 0;
    return m_buffers[bufferId].handle;
}

void CommandBufferRecycler::BeginFrame(u64 frameNumber) {
    std::lock_guard lock(m_mutex);
    m_currentFrame = frameNumber;

    // Recycle buffers that haven't been used for N frames
    for (u32 i = 0; i < m_buffers.size(); ++i) {
        auto& buf = m_buffers[i];
        if (buf.inUse && !buf.recorded &&
            frameNumber - buf.lastUsedFrame >= m_config.framesBeforeRecycle) {
            buf.inUse = false;
            buf.debugName.clear();
            m_available.push(i);
            m_recycleEvents++;
        }
    }
}

void CommandBufferRecycler::ResetAll() {
    std::lock_guard lock(m_mutex);
    while (!m_available.empty()) m_available.pop();

    for (u32 i = 0; i < m_buffers.size(); ++i) {
        m_buffers[i].inUse = false;
        m_buffers[i].recorded = false;
        m_buffers[i].debugName.clear();
        m_available.push(i);
    }
}

CommandBufferRecyclerStats CommandBufferRecycler::GetStats() const {
    std::lock_guard lock(m_mutex);
    CommandBufferRecyclerStats stats{};
    stats.totalBuffers = static_cast<u32>(m_buffers.size());
    stats.availableBuffers = static_cast<u32>(m_available.size());
    stats.inUseBuffers = stats.totalBuffers - stats.availableBuffers;
    stats.growthEvents = m_growthEvents;
    stats.recycleEvents = m_recycleEvents;

    u32 recorded = 0;
    for (const auto& buf : m_buffers) {
        if (buf.recorded) recorded++;
    }
    stats.recordedBuffers = recorded;

    return stats;
}

void CommandBufferRecycler::GrowPool(u32 count, u32 queueFamily) {
    u32 startIdx = static_cast<u32>(m_buffers.size());
    m_buffers.resize(startIdx + count);

    for (u32 i = startIdx; i < startIdx + count; ++i) {
        auto& buf = m_buffers[i];
        buf.level = CommandBufferLevel::Secondary;
        buf.queueFamily = queueFamily;
        buf.inUse = false;
        buf.recorded = false;
        buf.lastUsedFrame = 0;

        // TODO: Allocate from command pool
        // VkCommandBufferAllocateInfo allocInfo{};
        // allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        // allocInfo.commandPool = getPoolForFamily(queueFamily);
        // allocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        // allocInfo.commandBufferCount = 1;
        // vkAllocateCommandBuffers(device, &allocInfo, &buf.handle);

        buf.handle = static_cast<u64>(i + 1); // Stub
        buf.poolHandle = 1; // Stub
        m_available.push(i);
    }
}

} // namespace nge::rhi
