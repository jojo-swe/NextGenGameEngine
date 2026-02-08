#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Descriptor Pool Manager ─────────────────────────────────────────
// Auto-growing descriptor pool system with per-type allocation tracking.
// When a pool is exhausted, a new one is created transparently.
// Provides allocation statistics and fragmentation reporting.
//
// Each pool has a fixed capacity. When full, a new pool is appended.
// Per-frame transient pools are reset each frame; persistent pools
// live until explicitly freed or shutdown.

enum class DescriptorType : u8 {
    Sampler,
    SampledImage,
    StorageImage,
    UniformBuffer,
    StorageBuffer,
    UniformBufferDynamic,
    StorageBufferDynamic,
    InputAttachment,
    AccelerationStructure,
    Count,
};

struct PoolSizeRatio {
    DescriptorType type;
    f32            ratio;  // Proportion of pool dedicated to this type
};

struct DescriptorPoolConfig {
    u32 setsPerPool = 1024;
    std::vector<PoolSizeRatio> typeRatios = {
        {DescriptorType::SampledImage,          0.30f},
        {DescriptorType::StorageBuffer,         0.20f},
        {DescriptorType::UniformBuffer,         0.20f},
        {DescriptorType::StorageImage,          0.10f},
        {DescriptorType::Sampler,               0.05f},
        {DescriptorType::UniformBufferDynamic,  0.05f},
        {DescriptorType::StorageBufferDynamic,  0.05f},
        {DescriptorType::InputAttachment,       0.03f},
        {DescriptorType::AccelerationStructure, 0.02f},
    };
    u32 maxPools = 32;
    bool allowGrowth = true;
};

struct DescriptorPoolStats {
    u32 totalPools;
    u32 totalSetsAllocated;
    u32 totalSetsAvailable;
    u32 growthEvents;
    std::unordered_map<u8, u32> allocationsPerType;
    f32 fragmentationPercent;
};

class DescriptorPoolManager {
public:
    bool Init(IDevice* device, const DescriptorPoolConfig& config = {});
    void Shutdown();

    // Allocate a descriptor set
    u64 Allocate(const std::vector<DescriptorType>& types);

    // Free a descriptor set (returns to pool)
    void Free(u64 descriptorSet);

    // Reset all pools (for transient usage)
    void ResetAll();

    // Reset only transient pools (called per-frame)
    void ResetTransient();

    // Get current pool count
    u32 GetPoolCount() const;

    DescriptorPoolStats GetStats() const;

private:
    struct Pool {
        u64  handle;           // VkDescriptorPool
        u32  setsAllocated;
        u32  maxSets;
        bool full;
    };

    Pool CreatePool();
    Pool* FindAvailablePool();

    IDevice* m_device = nullptr;
    DescriptorPoolConfig m_config;
    std::vector<Pool> m_pools;
    u32 m_growthEvents = 0;
    u32 m_totalAllocations = 0;
    std::unordered_map<u8, u32> m_typeAllocations;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
