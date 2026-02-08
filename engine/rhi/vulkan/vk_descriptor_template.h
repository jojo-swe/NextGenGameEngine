#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── Vulkan Descriptor Update Template Cache ─────────────────────────────
// Pre-bakes VkDescriptorUpdateTemplate objects for efficient descriptor
// set updates. Instead of building VkWriteDescriptorSet arrays each frame,
// templates allow a single memcpy-style update from a contiguous struct.
//
// VK_KHR_descriptor_update_template (core in Vulkan 1.1)

struct DescriptorTemplateEntry {
    u32  dstBinding;
    u32  dstArrayElement;
    u32  descriptorCount;
    u32  descriptorType;     // VkDescriptorType
    u64  offset;             // Byte offset in the update data struct
    u64  stride;             // Byte stride between array elements
};

struct DescriptorTemplateDesc {
    std::vector<DescriptorTemplateEntry> entries;
    u64  setLayoutHandle;    // VkDescriptorSetLayout
    u32  pipelineBindPoint;  // VK_PIPELINE_BIND_POINT_GRAPHICS or COMPUTE
    u64  pipelineLayoutHandle;
    u32  setIndex;
};

struct DescriptorTemplateDescHash {
    size_t operator()(const DescriptorTemplateDesc& desc) const;
};

bool operator==(const DescriptorTemplateDesc& a, const DescriptorTemplateDesc& b);

struct CachedTemplate {
    u64 handle;              // VkDescriptorUpdateTemplate
    u64 lastUsedFrame;
    u32 updateCount;         // Times used
};

class DescriptorTemplateCache {
public:
    bool Init(IDevice* device);
    void Shutdown();

    // Get or create a template for the given descriptor layout
    u64 GetOrCreate(const DescriptorTemplateDesc& desc, u64 frameNumber);

    // Update a descriptor set using a template + data pointer
    void UpdateDescriptorSet(u64 descriptorSet, u64 templateHandle, const void* data);

    // Evict unused templates
    u32 EvictUnused(u64 currentFrame, u64 maxAge = 600);

    u32 GetCachedCount() const;
    u32 GetHitCount() const { return m_hitCount; }
    u32 GetMissCount() const { return m_missCount; }

private:
    IDevice* m_device = nullptr;
    std::unordered_map<DescriptorTemplateDesc, CachedTemplate, DescriptorTemplateDescHash> m_cache;
    mutable std::mutex m_mutex;
    u32 m_hitCount = 0;
    u32 m_missCount = 0;
};

} // namespace nge::rhi
