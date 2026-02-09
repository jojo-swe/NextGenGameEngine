#include "engine/rhi/vulkan/vk_descriptor_layout_cache.h"
#include "engine/core/logging/log.h"

namespace nge::rhi::vulkan {

bool DescriptorLayoutCache::Init(const DescriptorLayoutCacheConfig& config) {
    m_config = config;
    m_hashToHandle.reserve(config.initialCapacity);
    m_layouts.reserve(config.initialCapacity);
    m_nextHandle = 1;
    m_cacheHits = 0;
    m_cacheMisses = 0;

    NGE_LOG_INFO("Descriptor layout cache initialized: capacity={}", config.initialCapacity);
    return true;
}

void DescriptorLayoutCache::Shutdown() {
    for (auto& [handle, layout] : m_layouts) {
        DestroyLayout(handle);
    }
    m_layouts.clear();
    m_hashToHandle.clear();
    m_nextHandle = 1;
}

DescriptorSetLayoutHandle DescriptorLayoutCache::GetOrCreate(const LayoutCreateInfo& info) {
    std::lock_guard lock(m_mutex);

    u64 hash = HashLayout(info);

    auto it = m_hashToHandle.find(hash);
    if (it != m_hashToHandle.end()) {
        // Cache hit — increment ref count
        m_cacheHits++;
        auto& layout = m_layouts[it->second];
        layout.refCount++;
        return it->second;
    }

    // Cache miss — create new layout
    m_cacheMisses++;
    DescriptorSetLayoutHandle handle = CreateLayout(info);

    CachedLayout cached;
    cached.handle = handle;
    cached.hash = hash;
    cached.refCount = 1;
    cached.bindingCount = static_cast<u32>(info.bindings.size());
    cached.pushDescriptor = info.pushDescriptor;
    cached.debugName = info.debugName;

    m_layouts[handle] = std::move(cached);
    m_hashToHandle[hash] = handle;

    return handle;
}

void DescriptorLayoutCache::Release(DescriptorSetLayoutHandle handle) {
    std::lock_guard lock(m_mutex);

    auto it = m_layouts.find(handle);
    if (it == m_layouts.end()) return;

    it->second.refCount--;
    if (it->second.refCount == 0) {
        // Remove from hash map
        m_hashToHandle.erase(it->second.hash);

        // Destroy Vulkan object
        DestroyLayout(handle);

        m_layouts.erase(it);
    }
}

void DescriptorLayoutCache::AddRef(DescriptorSetLayoutHandle handle) {
    std::lock_guard lock(m_mutex);

    auto it = m_layouts.find(handle);
    if (it != m_layouts.end()) {
        it->second.refCount++;
    }
}

const CachedLayout* DescriptorLayoutCache::GetLayout(DescriptorSetLayoutHandle handle) const {
    std::lock_guard lock(m_mutex);

    auto it = m_layouts.find(handle);
    if (it != m_layouts.end()) return &it->second;
    return nullptr;
}

bool DescriptorLayoutCache::HasLayout(u64 hash) const {
    std::lock_guard lock(m_mutex);
    return m_hashToHandle.find(hash) != m_hashToHandle.end();
}

u32 DescriptorLayoutCache::GetLayoutCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_layouts.size());
}

DescriptorLayoutCacheStats DescriptorLayoutCache::GetStats() const {
    std::lock_guard lock(m_mutex);
    DescriptorLayoutCacheStats stats{};
    stats.totalLayouts = static_cast<u32>(m_layouts.size());
    stats.cacheHits = m_cacheHits;
    stats.cacheMisses = m_cacheMisses;
    u32 total = m_cacheHits + m_cacheMisses;
    stats.hitRate = total > 0 ? static_cast<f32>(m_cacheHits) / static_cast<f32>(total) * 100.0f : 0.0f;

    u32 totalRef = 0;
    for (const auto& [h, layout] : m_layouts) {
        totalRef += layout.refCount;
    }
    stats.totalRefCount = totalRef;

    return stats;
}

u64 DescriptorLayoutCache::HashLayout(const LayoutCreateInfo& info) const {
    // FNV-1a hash over binding descriptions
    u64 hash = 14695981039346656037ULL; // FNV offset basis
    constexpr u64 FNV_PRIME = 1099511628211ULL;

    auto hashByte = [&](u8 byte) {
        hash ^= byte;
        hash *= FNV_PRIME;
    };

    auto hashU32 = [&](u32 val) {
        hashByte(static_cast<u8>(val));
        hashByte(static_cast<u8>(val >> 8));
        hashByte(static_cast<u8>(val >> 16));
        hashByte(static_cast<u8>(val >> 24));
    };

    // Hash push descriptor flag
    hashByte(info.pushDescriptor ? 1 : 0);

    // Hash each binding (sorted by binding number for order independence)
    std::vector<const LayoutBinding*> sorted;
    sorted.reserve(info.bindings.size());
    for (const auto& b : info.bindings) sorted.push_back(&b);
    std::sort(sorted.begin(), sorted.end(),
              [](const LayoutBinding* a, const LayoutBinding* b) { return a->binding < b->binding; });

    for (const auto* binding : sorted) {
        hashU32(binding->binding);
        hashByte(static_cast<u8>(binding->type));
        hashU32(binding->count);
        hashU32(static_cast<u32>(binding->stageFlags));
        hashByte(binding->partiallyBound ? 1 : 0);
    }

    return hash;
}

DescriptorSetLayoutHandle DescriptorLayoutCache::CreateLayout(const LayoutCreateInfo& info) {
    // TODO: Create actual VkDescriptorSetLayout via vkCreateDescriptorSetLayout
    // VkDescriptorSetLayoutCreateInfo createInfo{};
    // createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    // createInfo.bindingCount = info.bindings.size();
    // std::vector<VkDescriptorSetLayoutBinding> vkBindings(info.bindings.size());
    // for (size_t i = 0; i < info.bindings.size(); ++i) {
    //     vkBindings[i].binding = info.bindings[i].binding;
    //     vkBindings[i].descriptorType = (VkDescriptorType)info.bindings[i].type;
    //     vkBindings[i].descriptorCount = info.bindings[i].count;
    //     vkBindings[i].stageFlags = (VkShaderStageFlags)info.bindings[i].stageFlags;
    // }
    // if (info.pushDescriptor) {
    //     createInfo.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    // }
    // VkDescriptorSetLayout layout;
    // vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &layout);

    DescriptorSetLayoutHandle handle = m_nextHandle++;
    NGE_LOG_DEBUG("Created descriptor set layout: handle={}, bindings={}, name='{}'",
                  handle, info.bindings.size(), info.debugName);
    return handle;
}

void DescriptorLayoutCache::DestroyLayout(DescriptorSetLayoutHandle handle) {
    // TODO: vkDestroyDescriptorSetLayout(device, (VkDescriptorSetLayout)handle, nullptr);
    NGE_LOG_DEBUG("Destroyed descriptor set layout: handle={}", handle);
}

} // namespace nge::rhi::vulkan
