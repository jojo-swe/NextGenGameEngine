#include "engine/rhi/common/rhi_indirect_dispatch_builder.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <cmath>

namespace nge::rhi {

bool IndirectDispatchBuilder::Init(const IndirectDispatchConfig& config) {
    m_config = config;
    m_nextSlotId = 1;
    m_nextOutputOffset = 0;
    m_totalDispatches = 0;
    m_clampedDispatches = 0;
    m_totalWorkgroups = 0;

    // TODO: Create VkBuffer for indirect commands
    // VkBufferCreateInfo bufferInfo{};
    // bufferInfo.size = config.commandBufferSize;
    // bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    NGE_LOG_INFO("Indirect dispatch builder initialized: maxSlots={}, cmdBufSize={}",
                 config.maxSlots, config.commandBufferSize);
    return true;
}

void IndirectDispatchBuilder::Shutdown() {
    m_slots.clear();
}

u32 IndirectDispatchBuilder::CreateSlot(const IndirectDispatchRequest& request) {
    std::lock_guard lock(m_mutex);

    if (m_slots.size() >= m_config.maxSlots) {
        NGE_LOG_WARN("Indirect dispatch builder: max slots reached ({})", m_config.maxSlots);
        return 0;
    }

    u32 slotId = m_nextSlotId++;

    IndirectDispatchSlot slot;
    slot.slotId = slotId;
    slot.outputBufferHandle = 0; // TODO: actual VkBuffer handle
    slot.outputOffset = m_nextOutputOffset;
    slot.request = request;
    slot.ready = false;

    m_nextOutputOffset += sizeof(DispatchIndirectCommand);

    if (m_nextOutputOffset > m_config.commandBufferSize) {
        NGE_LOG_WARN("Indirect dispatch: command buffer overflow at offset {}", m_nextOutputOffset);
    }

    m_slots[slotId] = std::move(slot);

    NGE_LOG_DEBUG("Created indirect dispatch slot {}: workgroupSize={}, max={}, name='{}'",
                  slotId, request.workgroupSize, request.maxGroupCount, request.debugName);

    return slotId;
}

void IndirectDispatchBuilder::DestroySlot(u32 slotId) {
    std::lock_guard lock(m_mutex);
    m_slots.erase(slotId);
}

DispatchIndirectCommand IndirectDispatchBuilder::BuildCommand(u32 elementCount,
                                                                const IndirectDispatchRequest& request) const {
    DispatchIndirectCommand cmd;

    if (request.is2D && request.dispatchWidth > 0) {
        // 2D dispatch: X = width, Y = ceil(count / width), Z = 1
        u32 groupsX = (request.dispatchWidth + request.workgroupSize - 1) / request.workgroupSize;
        u32 totalElements = elementCount;
        u32 rows = (totalElements + request.dispatchWidth - 1) / request.dispatchWidth;

        cmd.groupCountX = groupsX;
        cmd.groupCountY = rows;
        cmd.groupCountZ = 1;
    } else {
        // 1D dispatch: X = ceil(count / workgroupSize), Y = 1, Z = 1
        u32 groups = (elementCount + request.workgroupSize - 1) / request.workgroupSize;
        cmd.groupCountX = groups;
        cmd.groupCountY = 1;
        cmd.groupCountZ = 1;
    }

    // Safety clamp
    if (m_config.clampToMaxGroups) {
        u32 maxG = request.maxGroupCount;
        if (maxG == 0) maxG = 65535;

        bool clamped = false;
        if (cmd.groupCountX > maxG) { cmd.groupCountX = maxG; clamped = true; }
        if (cmd.groupCountY > maxG) { cmd.groupCountY = maxG; clamped = true; }
        if (cmd.groupCountZ > maxG) { cmd.groupCountZ = maxG; clamped = true; }

        if (clamped) {
            NGE_LOG_WARN("Indirect dispatch clamped to max {} for '{}'", maxG, request.debugName);
        }
    }

    return cmd;
}

u64 IndirectDispatchBuilder::GetCommandOffset(u32 slotId) const {
    std::lock_guard lock(m_mutex);
    auto it = m_slots.find(slotId);
    if (it == m_slots.end()) return UINT64_MAX;
    return it->second.outputOffset;
}

const IndirectDispatchSlot* IndirectDispatchBuilder::GetSlot(u32 slotId) const {
    std::lock_guard lock(m_mutex);
    auto it = m_slots.find(slotId);
    if (it != m_slots.end()) return &it->second;
    return nullptr;
}

std::vector<u32> IndirectDispatchBuilder::GetActiveSlotIds() const {
    std::lock_guard lock(m_mutex);
    std::vector<u32> ids;
    ids.reserve(m_slots.size());
    for (const auto& [id, slot] : m_slots) {
        ids.push_back(id);
    }
    return ids;
}

void IndirectDispatchBuilder::Clear() {
    std::lock_guard lock(m_mutex);
    m_slots.clear();
    m_nextOutputOffset = 0;
}

void IndirectDispatchBuilder::RecordDispatch(u32 slotId, u32 actualGroupCount) {
    std::lock_guard lock(m_mutex);
    m_totalDispatches++;
    m_totalWorkgroups += actualGroupCount;

    auto it = m_slots.find(slotId);
    if (it != m_slots.end()) {
        if (actualGroupCount >= it->second.request.maxGroupCount) {
            m_clampedDispatches++;
        }
    }
}

IndirectDispatchStats IndirectDispatchBuilder::GetStats() const {
    std::lock_guard lock(m_mutex);
    IndirectDispatchStats stats{};
    stats.activeSlots = static_cast<u32>(m_slots.size());
    stats.totalDispatches = m_totalDispatches;
    stats.clampedDispatches = m_clampedDispatches;
    stats.totalWorkgroups = m_totalWorkgroups;
    return stats;
}

} // namespace nge::rhi
