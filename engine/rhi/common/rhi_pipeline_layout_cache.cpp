#include "engine/rhi/common/rhi_pipeline_layout_cache.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool PipelineLayoutKey::operator==(const PipelineLayoutKey& other) const {
    if (setLayouts.size() != other.setLayouts.size()) return false;
    if (pushConstants.size() != other.pushConstants.size()) return false;

    for (u32 i = 0; i < static_cast<u32>(setLayouts.size()); ++i) {
        if (setLayouts[i].set != other.setLayouts[i].set) return false;
        if (setLayouts[i].bindings.size() != other.setLayouts[i].bindings.size()) return false;
        for (u32 j = 0; j < static_cast<u32>(setLayouts[i].bindings.size()); ++j) {
            const auto& a = setLayouts[i].bindings[j];
            const auto& b = other.setLayouts[i].bindings[j];
            if (a.set != b.set || a.binding != b.binding ||
                a.type != b.type || a.count != b.count) return false;
        }
    }

    for (u32 i = 0; i < static_cast<u32>(pushConstants.size()); ++i) {
        if (pushConstants[i].offset != other.pushConstants[i].offset ||
            pushConstants[i].size != other.pushConstants[i].size) return false;
    }

    return true;
}

size_t PipelineLayoutKeyHash::operator()(const PipelineLayoutKey& key) const {
    size_t h = 0;
    auto combine = [&h](size_t val) {
        h ^= val + 0x9e3779b9 + (h << 6) + (h >> 2);
    };

    combine(key.setLayouts.size());
    for (const auto& set : key.setLayouts) {
        combine(set.set);
        for (const auto& b : set.bindings) {
            combine(b.set);
            combine(b.binding);
            combine(static_cast<size_t>(b.type));
            combine(b.count);
        }
    }
    for (const auto& pc : key.pushConstants) {
        combine(pc.offset);
        combine(pc.size);
    }
    return h;
}

bool PipelineLayoutCache::Init(IDevice* device) {
    m_device = device;
    m_hitCount = 0;
    m_missCount = 0;
    NGE_LOG_INFO("Pipeline layout cache initialized");
    return true;
}

void PipelineLayoutCache::Shutdown() {
    // TODO: vkDestroyPipelineLayout for each cached layout
    std::lock_guard lock(m_mutex);
    m_cache.clear();
}

CachedPipelineLayout PipelineLayoutCache::GetOrCreate(
    const std::vector<assets::ShaderReflectionData>& stages,
    u64 frameNumber) {

    // Merge bindings from all stages
    auto mergedBindings = assets::ShaderReflector::MergeBindings(stages);
    auto setLayouts = assets::ShaderReflector::BuildSetLayouts(mergedBindings);

    // Collect push constants from all stages
    std::vector<assets::ReflectedPushConstant> pushConstants;
    for (const auto& stage : stages) {
        for (const auto& pc : stage.pushConstants) {
            pushConstants.push_back(pc);
        }
    }

    PipelineLayoutKey key;
    key.setLayouts = std::move(setLayouts);
    key.pushConstants = std::move(pushConstants);

    return GetOrCreate(key, frameNumber);
}

CachedPipelineLayout PipelineLayoutCache::GetOrCreate(const PipelineLayoutKey& key, u64 frameNumber) {
    std::lock_guard lock(m_mutex);

    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        it->second.lastUsedFrame = frameNumber;
        m_hitCount++;
        return it->second;
    }

    m_missCount++;
    auto layout = CreateLayout(key);
    layout.lastUsedFrame = frameNumber;
    m_cache[key] = layout;
    return layout;
}

u32 PipelineLayoutCache::EvictUnused(u64 currentFrame, u64 maxAge) {
    std::lock_guard lock(m_mutex);
    u32 evicted = 0;

    for (auto it = m_cache.begin(); it != m_cache.end(); ) {
        if (currentFrame - it->second.lastUsedFrame > maxAge) {
            // TODO: vkDestroyPipelineLayout(device, it->second.handle, nullptr);
            it = m_cache.erase(it);
            evicted++;
        } else {
            ++it;
        }
    }
    return evicted;
}

u32 PipelineLayoutCache::GetCachedCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_cache.size());
}

CachedPipelineLayout PipelineLayoutCache::CreateLayout(const PipelineLayoutKey& key) {
    // TODO: Create VkPipelineLayout
    //
    // 1. For each set layout:
    //    VkDescriptorSetLayoutCreateInfo → vkCreateDescriptorSetLayout
    //    Each binding → VkDescriptorSetLayoutBinding
    //
    // 2. Collect push constant ranges:
    //    VkPushConstantRange for each push constant
    //
    // 3. VkPipelineLayoutCreateInfo with set layouts + push constant ranges
    //    vkCreatePipelineLayout(device, &ci, nullptr, &layout)

    CachedPipelineLayout layout;
    layout.handle = static_cast<u64>(m_cache.size() + 1); // Stub
    layout.setCount = static_cast<u32>(key.setLayouts.size());
    layout.pushConstantRanges = static_cast<u32>(key.pushConstants.size());
    return layout;
}

} // namespace nge::rhi
