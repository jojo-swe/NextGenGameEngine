#include "engine/rhi/common/rhi_bindless_texture_manager.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <stack>

namespace nge::rhi {

bool BindlessTextureManager::Init(IDevice* device, const BindlessTextureConfig& config) {
    m_device = device;
    m_config = config;
    m_currentFrame = 0;

    m_slots.resize(config.maxTextures);
    for (u32 i = 0; i < config.maxTextures; ++i) {
        m_slots[i].slotIndex = i;
        m_slots[i].resident = false;
        m_slots[i].lastUsedFrame = 0;
    }

    // Reserve default texture slots
    for (u32 i = 0; i < config.reservedSlots; ++i) {
        m_slots[i].resident = true;
        m_slots[i].debugName = (i == 0) ? "white_default" :
                                (i == 1) ? "black_default" :
                                (i == 2) ? "normal_default" : "error_default";
    }

    // Initialize free list (skip reserved slots)
    for (u32 i = config.reservedSlots; i < config.maxTextures; ++i) {
        m_freeSlots.push(i);
    }

    // TODO: Create descriptor set layout for unbounded texture array
    // VkDescriptorSetLayoutBinding binding{};
    // binding.binding = 0;
    // binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    // binding.descriptorCount = config.maxTextures;
    // binding.stageFlags = VK_SHADER_STAGE_ALL;
    // VkDescriptorBindingFlags bindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
    //                                         VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
    //                                         VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

    // TODO: Allocate descriptor set
    m_descriptorSet = 1; // Stub

    NGE_LOG_INFO("Bindless texture manager initialized: {} max textures, {} reserved",
                 config.maxTextures, config.reservedSlots);
    return true;
}

void BindlessTextureManager::Shutdown() {
    // TODO: Free descriptor set
    m_slots.clear();
    while (!m_freeSlots.empty()) m_freeSlots.pop();
    m_pendingUpdates.clear();
}

u32 BindlessTextureManager::Register(TextureHandle texture, const std::string& debugName) {
    std::lock_guard lock(m_mutex);

    if (m_freeSlots.empty()) {
        NGE_LOG_ERROR("Bindless texture manager: no free slots (max={})", m_config.maxTextures);
        return GetErrorTextureIndex();
    }

    u32 slot = m_freeSlots.top();
    m_freeSlots.pop();

    m_slots[slot].texture = texture;
    m_slots[slot].debugName = debugName;
    m_slots[slot].resident = true;
    m_slots[slot].lastUsedFrame = m_currentFrame;

    m_pendingUpdates.push_back(slot);

    return slot;
}

void BindlessTextureManager::Unregister(u32 slotIndex) {
    std::lock_guard lock(m_mutex);

    if (slotIndex < m_config.reservedSlots || slotIndex >= m_config.maxTextures) return;

    m_slots[slotIndex].texture = {};
    m_slots[slotIndex].debugName.clear();
    m_slots[slotIndex].resident = false;

    // Point to error texture in descriptor to avoid undefined reads
    m_pendingUpdates.push_back(slotIndex);
    m_freeSlots.push(slotIndex);
}

void BindlessTextureManager::Update(u32 slotIndex, TextureHandle newTexture) {
    std::lock_guard lock(m_mutex);

    if (slotIndex >= m_config.maxTextures) return;

    m_slots[slotIndex].texture = newTexture;
    m_slots[slotIndex].lastUsedFrame = m_currentFrame;
    m_pendingUpdates.push_back(slotIndex);
}

void BindlessTextureManager::SetResident(u32 slotIndex, bool resident) {
    std::lock_guard lock(m_mutex);
    if (slotIndex >= m_config.maxTextures) return;
    m_slots[slotIndex].resident = resident;
}

bool BindlessTextureManager::IsResident(u32 slotIndex) const {
    std::lock_guard lock(m_mutex);
    if (slotIndex >= m_config.maxTextures) return false;
    return m_slots[slotIndex].resident;
}

TextureHandle BindlessTextureManager::GetTexture(u32 slotIndex) const {
    std::lock_guard lock(m_mutex);
    if (slotIndex >= m_config.maxTextures) return {};
    return m_slots[slotIndex].texture;
}

void BindlessTextureManager::FlushUpdates() {
    std::lock_guard lock(m_mutex);

    if (m_pendingUpdates.empty()) return;

    // Deduplicate
    std::sort(m_pendingUpdates.begin(), m_pendingUpdates.end());
    m_pendingUpdates.erase(
        std::unique(m_pendingUpdates.begin(), m_pendingUpdates.end()),
        m_pendingUpdates.end());

    // TODO: Batch VkWriteDescriptorSet for all pending slots
    // for (u32 slot : m_pendingUpdates) {
    //     VkDescriptorImageInfo imageInfo{};
    //     imageInfo.imageView = m_slots[slot].texture.IsValid()
    //         ? getImageView(m_slots[slot].texture)
    //         : getImageView(m_slots[GetErrorTextureIndex()].texture);
    //     imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    //
    //     VkWriteDescriptorSet write{};
    //     write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    //     write.dstSet = m_descriptorSet;
    //     write.dstBinding = 0;
    //     write.dstArrayElement = slot;
    //     write.descriptorCount = 1;
    //     write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    //     write.pImageInfo = &imageInfo;
    //     writes.push_back(write);
    // }
    // vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);

    m_pendingUpdates.clear();
}

void BindlessTextureManager::BeginFrame(u64 frameNumber) {
    m_currentFrame = frameNumber;
    FlushUpdates();
}

BindlessTextureStats BindlessTextureManager::GetStats() const {
    std::lock_guard lock(m_mutex);
    BindlessTextureStats stats{};
    stats.totalSlots = m_config.maxTextures;
    stats.freeSlots = static_cast<u32>(m_freeSlots.size());
    stats.usedSlots = m_config.maxTextures - stats.freeSlots;
    stats.pendingUploads = static_cast<u32>(m_pendingUpdates.size());

    u32 resident = 0;
    for (const auto& slot : m_slots) {
        if (slot.resident) resident++;
    }
    stats.residentTextures = resident;

    return stats;
}

} // namespace nge::rhi
