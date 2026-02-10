#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Descriptor Pool Allocator ───────────────────────────────────────
// Manages a growable collection of descriptor pools, automatically creating
// new pools when existing ones are exhausted. Tracks per-pool usage and
// provides defragmentation hints.
//
// Use cases:
//   - Allocate descriptor sets from a managed pool hierarchy
//   - Automatic pool growth when capacity reached
//   - Per-pool and per-type usage tracking
//   - Pool reset for frame-based allocation patterns
//   - Defragmentation: detect underutilized pools
//   - Stats: allocations, pool count, fragmentation

enum class DescriptorType : u8 {
    Sampler,
    CombinedImageSampler,
    SampledImage,
    StorageImage,
    UniformBuffer,
    StorageBuffer,
    UniformBufferDynamic,
    StorageBufferDynamic,
    InputAttachment,
    Count,
};

struct PoolSizeEntry {
    DescriptorType type;
    u32            count;
};

struct DescriptorPoolInfo {
    u32                      poolId;
    u32                      maxSets;
    u32                      allocatedSets;
    std::vector<PoolSizeEntry> typeCounts;    // Capacity per type
    std::vector<PoolSizeEntry> usedCounts;    // Allocated per type
    bool                     exhausted;
};

struct DescriptorPoolAllocConfig {
    u32  maxPools = 64;
    u32  setsPerPool = 256;
    std::vector<PoolSizeEntry> defaultPoolSizes; // Default type counts per pool
    bool autoGrow = true;
};

struct DescriptorPoolAllocStats {
    u32   totalPools;
    u32   exhaustedPools;
    u32   totalSetsAllocated;
    u32   totalSetsCapacity;
    float utilizationRatio;
    u32   poolGrowthCount;
    u32   allocationFailures;
};

class DescriptorPoolAllocator {
public:
    bool Init(const DescriptorPoolAllocConfig& config = {});
    void Shutdown();

    // Allocate a descriptor set (returns pool ID + set index, or UINT32_MAX on failure)
    u32 Allocate(const std::vector<PoolSizeEntry>& requirements);

    // Free a descriptor set back to its pool
    void Free(u32 allocationId);

    // Reset all pools (free all sets, keep pools alive)
    void ResetAllPools();

    // Reset a specific pool
    void ResetPool(u32 poolId);

    // Get pool info
    const DescriptorPoolInfo* GetPoolInfo(u32 poolId) const;

    // Get number of pools
    u32 GetPoolCount() const;

    // Get total allocated sets across all pools
    u32 GetTotalAllocated() const;

    // Check if pools need compaction (many underutilized pools)
    bool NeedsCompaction(float threshold = 0.3f) const;

    // Get underutilized pool IDs
    std::vector<u32> GetUnderutilizedPools(float threshold = 0.3f) const;

    void Reset();

    DescriptorPoolAllocStats GetStats() const;

private:
    u32 FindOrCreatePool(const std::vector<PoolSizeEntry>& requirements);
    u32 CreateNewPool();
    bool PoolCanFit(const DescriptorPoolInfo& pool, const std::vector<PoolSizeEntry>& requirements) const;

    DescriptorPoolAllocConfig m_config;

    std::vector<DescriptorPoolInfo> m_pools;
    std::unordered_map<u32, u32> m_allocationToPool; // allocationId -> poolId

    u32 m_nextAllocationId = 0;
    u32 m_poolGrowthCount = 0;
    u32 m_allocationFailures = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
