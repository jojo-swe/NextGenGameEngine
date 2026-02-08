#include "engine/rhi/common/rhi_descriptor_pool_manager.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool DescriptorPoolManager::Init(IDevice* device, const DescriptorPoolConfig& config) {
    m_device = device;
    m_config = config;
    m_growthEvents = 0;
    m_totalAllocations = 0;
    m_typeAllocations.clear();

    // Create initial pool
    m_pools.push_back(CreatePool());

    NGE_LOG_INFO("Descriptor pool manager initialized: setsPerPool={}, maxPools={}, growth={}",
                 config.setsPerPool, config.maxPools, config.allowGrowth);
    return true;
}

void DescriptorPoolManager::Shutdown() {
    for (auto& pool : m_pools) {
        // TODO: vkDestroyDescriptorPool(device, pool.handle, nullptr);
        (void)pool;
    }
    m_pools.clear();
    m_typeAllocations.clear();
}

u64 DescriptorPoolManager::Allocate(const std::vector<DescriptorType>& types) {
    std::lock_guard lock(m_mutex);

    Pool* pool = FindAvailablePool();
    if (!pool) {
        if (!m_config.allowGrowth || m_pools.size() >= m_config.maxPools) {
            NGE_LOG_ERROR("Descriptor pool manager: all pools exhausted, maxPools={}",
                          m_config.maxPools);
            return 0;
        }

        m_pools.push_back(CreatePool());
        m_growthEvents++;
        pool = &m_pools.back();
        NGE_LOG_WARN("Descriptor pool growth event #{}: now {} pools", m_growthEvents, m_pools.size());
    }

    // TODO: VkDescriptorSetAllocateInfo allocInfo{};
    // allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    // allocInfo.descriptorPool = pool->handle;
    // allocInfo.descriptorSetCount = 1;
    // allocInfo.pSetLayouts = &layout;
    // VkDescriptorSet set;
    // VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &set);
    // if (result == VK_ERROR_OUT_OF_POOL_MEMORY) { pool->full = true; retry; }

    pool->setsAllocated++;
    if (pool->setsAllocated >= pool->maxSets) {
        pool->full = true;
    }

    m_totalAllocations++;
    for (auto type : types) {
        m_typeAllocations[static_cast<u8>(type)]++;
    }

    return 1; // Stub: would return VkDescriptorSet
}

void DescriptorPoolManager::Free(u64 descriptorSet) {
    std::lock_guard lock(m_mutex);
    // Individual free is expensive on Vulkan; prefer pool reset
    // TODO: vkFreeDescriptorSets if VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
    (void)descriptorSet;
}

void DescriptorPoolManager::ResetAll() {
    std::lock_guard lock(m_mutex);
    for (auto& pool : m_pools) {
        // TODO: vkResetDescriptorPool(device, pool.handle, 0);
        pool.setsAllocated = 0;
        pool.full = false;
    }
    m_totalAllocations = 0;
    m_typeAllocations.clear();
}

void DescriptorPoolManager::ResetTransient() {
    // Reset only the first pool (transient usage pattern)
    // Persistent pools keep their allocations
    std::lock_guard lock(m_mutex);
    if (!m_pools.empty()) {
        auto& pool = m_pools[0];
        // TODO: vkResetDescriptorPool(device, pool.handle, 0);
        pool.setsAllocated = 0;
        pool.full = false;
    }
}

u32 DescriptorPoolManager::GetPoolCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_pools.size());
}

DescriptorPoolStats DescriptorPoolManager::GetStats() const {
    std::lock_guard lock(m_mutex);
    DescriptorPoolStats stats{};
    stats.totalPools = static_cast<u32>(m_pools.size());
    stats.growthEvents = m_growthEvents;
    stats.allocationsPerType = m_typeAllocations;

    u32 totalAllocated = 0;
    u32 totalAvailable = 0;
    for (const auto& pool : m_pools) {
        totalAllocated += pool.setsAllocated;
        totalAvailable += pool.maxSets;
    }
    stats.totalSetsAllocated = totalAllocated;
    stats.totalSetsAvailable = totalAvailable;

    // Fragmentation: ratio of empty slots in non-full pools
    u32 wastedSlots = 0;
    for (const auto& pool : m_pools) {
        if (!pool.full && pool.setsAllocated > 0) {
            wastedSlots += pool.maxSets - pool.setsAllocated;
        }
    }
    stats.fragmentationPercent = totalAvailable > 0
        ? static_cast<f32>(wastedSlots) / static_cast<f32>(totalAvailable) * 100.0f
        : 0.0f;

    return stats;
}

DescriptorPoolManager::Pool DescriptorPoolManager::CreatePool() {
    Pool pool;
    pool.setsAllocated = 0;
    pool.maxSets = m_config.setsPerPool;
    pool.full = false;

    // TODO: Build VkDescriptorPoolSize array from ratios
    // std::vector<VkDescriptorPoolSize> poolSizes;
    // for (const auto& ratio : m_config.typeRatios) {
    //     VkDescriptorPoolSize size{};
    //     size.type = ToVkDescriptorType(ratio.type);
    //     size.descriptorCount = static_cast<u32>(m_config.setsPerPool * ratio.ratio);
    //     poolSizes.push_back(size);
    // }
    //
    // VkDescriptorPoolCreateInfo ci{};
    // ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    // ci.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    // ci.maxSets = m_config.setsPerPool;
    // ci.poolSizeCount = static_cast<u32>(poolSizes.size());
    // ci.pPoolSizes = poolSizes.data();
    // vkCreateDescriptorPool(device, &ci, nullptr, &pool.handle);

    pool.handle = 0; // Stub

    return pool;
}

DescriptorPoolManager::Pool* DescriptorPoolManager::FindAvailablePool() {
    for (auto& pool : m_pools) {
        if (!pool.full) return &pool;
    }
    return nullptr;
}

} // namespace nge::rhi
