#include "engine/rhi/vulkan/vk_descriptor_template.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <functional>

namespace nge::rhi {

size_t DescriptorTemplateDescHash::operator()(const DescriptorTemplateDesc& desc) const {
    size_t h = std::hash<u64>{}(desc.setLayoutHandle);
    h ^= std::hash<u32>{}(desc.pipelineBindPoint) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<u64>{}(desc.pipelineLayoutHandle) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<u32>{}(desc.setIndex) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<size_t>{}(desc.entries.size()) + 0x9e3779b9 + (h << 6) + (h >> 2);
    for (const auto& e : desc.entries) {
        h ^= std::hash<u32>{}(e.dstBinding) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<u32>{}(e.descriptorType) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<u64>{}(e.offset) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    return h;
}

bool operator==(const DescriptorTemplateDesc& a, const DescriptorTemplateDesc& b) {
    if (a.setLayoutHandle != b.setLayoutHandle) return false;
    if (a.pipelineBindPoint != b.pipelineBindPoint) return false;
    if (a.pipelineLayoutHandle != b.pipelineLayoutHandle) return false;
    if (a.setIndex != b.setIndex) return false;
    if (a.entries.size() != b.entries.size()) return false;
    for (size_t i = 0; i < a.entries.size(); ++i) {
        if (a.entries[i].dstBinding != b.entries[i].dstBinding) return false;
        if (a.entries[i].dstArrayElement != b.entries[i].dstArrayElement) return false;
        if (a.entries[i].descriptorCount != b.entries[i].descriptorCount) return false;
        if (a.entries[i].descriptorType != b.entries[i].descriptorType) return false;
        if (a.entries[i].offset != b.entries[i].offset) return false;
        if (a.entries[i].stride != b.entries[i].stride) return false;
    }
    return true;
}

bool DescriptorTemplateCache::Init(IDevice* device) {
    m_device = device;
    m_hitCount = 0;
    m_missCount = 0;
    NGE_LOG_INFO("Descriptor update template cache initialized");
    return true;
}

void DescriptorTemplateCache::Shutdown() {
    std::lock_guard lock(m_mutex);
    // TODO: vkDestroyDescriptorUpdateTemplate for each cached template
    m_cache.clear();
}

u64 DescriptorTemplateCache::GetOrCreate(const DescriptorTemplateDesc& desc, u64 frameNumber) {
    std::lock_guard lock(m_mutex);

    auto it = m_cache.find(desc);
    if (it != m_cache.end()) {
        it->second.lastUsedFrame = frameNumber;
        it->second.updateCount++;
        m_hitCount++;
        return it->second.handle;
    }

    m_missCount++;

    // TODO: Create VkDescriptorUpdateTemplate
    // std::vector<VkDescriptorUpdateTemplateEntry> vkEntries(desc.entries.size());
    // for (size_t i = 0; i < desc.entries.size(); ++i) {
    //     vkEntries[i].dstBinding = desc.entries[i].dstBinding;
    //     vkEntries[i].dstArrayElement = desc.entries[i].dstArrayElement;
    //     vkEntries[i].descriptorCount = desc.entries[i].descriptorCount;
    //     vkEntries[i].descriptorType = (VkDescriptorType)desc.entries[i].descriptorType;
    //     vkEntries[i].offset = desc.entries[i].offset;
    //     vkEntries[i].stride = desc.entries[i].stride;
    // }
    // VkDescriptorUpdateTemplateCreateInfo ci{};
    // ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO;
    // ci.descriptorUpdateEntryCount = (u32)vkEntries.size();
    // ci.pDescriptorUpdateEntries = vkEntries.data();
    // ci.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
    // ci.descriptorSetLayout = (VkDescriptorSetLayout)desc.setLayoutHandle;
    // ci.pipelineBindPoint = (VkPipelineBindPoint)desc.pipelineBindPoint;
    // ci.pipelineLayout = (VkPipelineLayout)desc.pipelineLayoutHandle;
    // ci.set = desc.setIndex;
    // VkDescriptorUpdateTemplate tmpl;
    // vkCreateDescriptorUpdateTemplate(device, &ci, nullptr, &tmpl);

    CachedTemplate cached;
    cached.handle = static_cast<u64>(m_cache.size() + 1); // Stub
    cached.lastUsedFrame = frameNumber;
    cached.updateCount = 1;
    m_cache[desc] = cached;

    return cached.handle;
}

void DescriptorTemplateCache::UpdateDescriptorSet(u64 descriptorSet, u64 templateHandle, const void* data) {
    // TODO: vkUpdateDescriptorSetWithTemplate(device, (VkDescriptorSet)descriptorSet,
    //                                         (VkDescriptorUpdateTemplate)templateHandle, data);
    (void)descriptorSet;
    (void)templateHandle;
    (void)data;
}

u32 DescriptorTemplateCache::EvictUnused(u64 currentFrame, u64 maxAge) {
    std::lock_guard lock(m_mutex);
    u32 evicted = 0;
    for (auto it = m_cache.begin(); it != m_cache.end(); ) {
        if (currentFrame - it->second.lastUsedFrame > maxAge) {
            // TODO: vkDestroyDescriptorUpdateTemplate
            it = m_cache.erase(it);
            evicted++;
        } else {
            ++it;
        }
    }
    return evicted;
}

u32 DescriptorTemplateCache::GetCachedCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_cache.size());
}

} // namespace nge::rhi
