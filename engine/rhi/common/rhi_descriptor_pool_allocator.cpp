#include "engine/rhi/common/rhi_descriptor_pool_allocator.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool DescriptorPoolAllocator::Init(const DescriptorPoolAllocConfig& config) {
    m_config = config;
    m_nextAllocationId = 0;
    m_poolGrowthCount = 0;
    m_allocationFailures = 0;

    // Provide default pool sizes if none specified
    if (m_config.defaultPoolSizes.empty()) {
        m_config.defaultPoolSizes = {
            {DescriptorType::CombinedImageSampler, 512},
            {DescriptorType::UniformBuffer, 512},
            {DescriptorType::StorageBuffer, 256},
            {DescriptorType::SampledImage, 256},
            {DescriptorType::StorageImage, 128},
            {DescriptorType::Sampler, 128},
            {DescriptorType::InputAttachment, 64},
            {DescriptorType::UniformBufferDynamic, 128},
            {DescriptorType::StorageBufferDynamic, 64},
        };
    }

    NGE_LOG_INFO("Descriptor pool allocator initialized: maxPools={}, setsPerPool={}, autoGrow={}",
                 config.maxPools, config.setsPerPool, config.autoGrow);
    return true;
}

void DescriptorPoolAllocator::Shutdown() {
    m_pools.clear();
    m_allocationToPool.clear();
}

u32 DescriptorPoolAllocator::Allocate(const std::vector<PoolSizeEntry>& requirements) {
    std::lock_guard lock(m_mutex);

    u32 poolId = FindOrCreatePool(requirements);
    if (poolId == UINT32_MAX) {
        m_allocationFailures++;
        return UINT32_MAX;
    }

    auto& pool = m_pools[poolId];
    pool.allocatedSets++;

    // Track per-type usage
    for (const auto& req : requirements) {
        for (auto& used : pool.usedCounts) {
            if (used.type == req.type) {
                used.count += req.count;
                break;
            }
        }
    }

    // Check if pool is now exhausted
    if (pool.allocatedSets >= pool.maxSets) {
        pool.exhausted = true;
    }

    u32 allocId = m_nextAllocationId++;
    m_allocationToPool[allocId] = poolId;

    return allocId;
}

void DescriptorPoolAllocator::Free(u32 allocationId) {
    std::lock_guard lock(m_mutex);

    auto it = m_allocationToPool.find(allocationId);
    if (it == m_allocationToPool.end()) return;

    u32 poolId = it->second;
    if (poolId < m_pools.size()) {
        auto& pool = m_pools[poolId];
        if (pool.allocatedSets > 0) {
            pool.allocatedSets--;
        }
        pool.exhausted = (pool.allocatedSets >= pool.maxSets);
    }

    m_allocationToPool.erase(it);
}

void DescriptorPoolAllocator::ResetAllPools() {
    std::lock_guard lock(m_mutex);

    for (auto& pool : m_pools) {
        pool.allocatedSets = 0;
        pool.exhausted = false;
        for (auto& used : pool.usedCounts) {
            used.count = 0;
        }
    }

    m_allocationToPool.clear();
}

void DescriptorPoolAllocator::ResetPool(u32 poolId) {
    std::lock_guard lock(m_mutex);

    if (poolId >= m_pools.size()) return;

    auto& pool = m_pools[poolId];
    pool.allocatedSets = 0;
    pool.exhausted = false;
    for (auto& used : pool.usedCounts) {
        used.count = 0;
    }

    // Remove allocations belonging to this pool
    auto it = m_allocationToPool.begin();
    while (it != m_allocationToPool.end()) {
        if (it->second == poolId) {
            it = m_allocationToPool.erase(it);
        } else {
            ++it;
        }
    }
}

const DescriptorPoolInfo* DescriptorPoolAllocator::GetPoolInfo(u32 poolId) const {
    std::lock_guard lock(m_mutex);

    if (poolId >= m_pools.size()) return nullptr;
    return &m_pools[poolId];
}

u32 DescriptorPoolAllocator::GetPoolCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_pools.size());
}

u32 DescriptorPoolAllocator::GetTotalAllocated() const {
    std::lock_guard lock(m_mutex);

    u32 total = 0;
    for (const auto& pool : m_pools) {
        total += pool.allocatedSets;
    }
    return total;
}

bool DescriptorPoolAllocator::NeedsCompaction(float threshold) const {
    std::lock_guard lock(m_mutex);

    if (m_pools.size() <= 1) return false;

    u32 underutilized = 0;
    for (const auto& pool : m_pools) {
        float util = pool.maxSets > 0
            ? static_cast<float>(pool.allocatedSets) / static_cast<float>(pool.maxSets)
            : 0.0f;
        if (util < threshold) underutilized++;
    }

    return underutilized > m_pools.size() / 2;
}

std::vector<u32> DescriptorPoolAllocator::GetUnderutilizedPools(float threshold) const {
    std::lock_guard lock(m_mutex);

    std::vector<u32> result;
    for (u32 i = 0; i < m_pools.size(); ++i) {
        float util = m_pools[i].maxSets > 0
            ? static_cast<float>(m_pools[i].allocatedSets) / static_cast<float>(m_pools[i].maxSets)
            : 0.0f;
        if (util < threshold) {
            result.push_back(i);
        }
    }
    return result;
}

void DescriptorPoolAllocator::Reset() {
    std::lock_guard lock(m_mutex);

    m_pools.clear();
    m_allocationToPool.clear();
    m_nextAllocationId = 0;
    m_poolGrowthCount = 0;
    m_allocationFailures = 0;
}

DescriptorPoolAllocStats DescriptorPoolAllocator::GetStats() const {
    std::lock_guard lock(m_mutex);

    DescriptorPoolAllocStats stats{};
    stats.totalPools = static_cast<u32>(m_pools.size());

    u32 exhausted = 0;
    u32 totalAlloc = 0;
    u32 totalCap = 0;

    for (const auto& pool : m_pools) {
        if (pool.exhausted) exhausted++;
        totalAlloc += pool.allocatedSets;
        totalCap += pool.maxSets;
    }

    stats.exhaustedPools = exhausted;
    stats.totalSetsAllocated = totalAlloc;
    stats.totalSetsCapacity = totalCap;
    stats.utilizationRatio = totalCap > 0
        ? static_cast<float>(totalAlloc) / static_cast<float>(totalCap)
        : 0.0f;
    stats.poolGrowthCount = m_poolGrowthCount;
    stats.allocationFailures = m_allocationFailures;

    return stats;
}

u32 DescriptorPoolAllocator::FindOrCreatePool(const std::vector<PoolSizeEntry>& requirements) {
    // Find an existing pool with capacity
    for (u32 i = 0; i < m_pools.size(); ++i) {
        if (!m_pools[i].exhausted && PoolCanFit(m_pools[i], requirements)) {
            return i;
        }
    }

    // No existing pool fits, create a new one if allowed
    if (!m_config.autoGrow) return UINT32_MAX;

    return CreateNewPool();
}

u32 DescriptorPoolAllocator::CreateNewPool() {
    if (m_pools.size() >= m_config.maxPools) {
        NGE_LOG_ERROR("Descriptor pool allocator: max pools reached ({})", m_config.maxPools);
        return UINT32_MAX;
    }

    DescriptorPoolInfo pool;
    pool.poolId = static_cast<u32>(m_pools.size());
    pool.maxSets = m_config.setsPerPool;
    pool.allocatedSets = 0;
    pool.exhausted = false;
    pool.typeCounts = m_config.defaultPoolSizes;

    // Initialize used counts to zero
    for (const auto& tc : pool.typeCounts) {
        pool.usedCounts.push_back({tc.type, 0});
    }

    m_pools.push_back(std::move(pool));
    m_poolGrowthCount++;

    NGE_LOG_DEBUG("Created descriptor pool {} (maxSets={})", pool.poolId, m_config.setsPerPool);

    return static_cast<u32>(m_pools.size() - 1);
}

bool DescriptorPoolAllocator::PoolCanFit(const DescriptorPoolInfo& pool, const std::vector<PoolSizeEntry>& requirements) const {
    if (pool.allocatedSets >= pool.maxSets) return false;

    for (const auto& req : requirements) {
        bool found = false;
        for (u32 i = 0; i < pool.typeCounts.size(); ++i) {
            if (pool.typeCounts[i].type == req.type) {
                u32 available = pool.typeCounts[i].count - pool.usedCounts[i].count;
                if (available < req.count) return false;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }

    return true;
}

} // namespace nge::rhi
