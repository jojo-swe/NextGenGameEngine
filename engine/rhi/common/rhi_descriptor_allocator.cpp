#include "engine/rhi/common/rhi_descriptor_allocator.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool DescriptorSetAllocator::Init(IDevice* device, const DescriptorAllocatorConfig& config) {
    m_device = device;
    m_config = config;
    m_currentFrame = 0;
    m_transientSetsAllocated = 0;
    m_persistentSetsAllocated = 0;
    m_poolExhaustedCount = 0;

    // Create per-frame transient pools
    m_transientFrames.resize(config.framesInFlight);
    for (u32 f = 0; f < config.framesInFlight; ++f) {
        auto pool = CreatePool(config.transientPoolSizes, false);
        m_transientFrames[f].pools.push_back(pool);
        m_transientFrames[f].activePoolIndex = 0;
    }

    // Create initial persistent pool
    auto persistentPool = CreatePool(config.persistentPoolSizes, true);
    m_persistentPools.push_back(persistentPool);

    NGE_LOG_INFO("Descriptor set allocator initialized: {} frames, {} max transient sets/pool, {} max persistent sets/pool",
                 config.framesInFlight, config.transientPoolSizes.maxSets, config.persistentPoolSizes.maxSets);
    return true;
}

void DescriptorSetAllocator::Shutdown() {
    for (auto& frame : m_transientFrames) {
        for (auto& pool : frame.pools) {
            // TODO: vkDestroyDescriptorPool(device, pool.handle, nullptr);
        }
    }
    for (auto& pool : m_persistentPools) {
        // TODO: vkDestroyDescriptorPool(device, pool.handle, nullptr);
    }
    m_transientFrames.clear();
    m_persistentPools.clear();
}

void DescriptorSetAllocator::BeginFrame(u32 frameIndex) {
    std::lock_guard lock(m_mutex);
    m_currentFrame = frameIndex % m_config.framesInFlight;

    auto& frame = m_transientFrames[m_currentFrame];
    for (auto& pool : frame.pools) {
        // TODO: vkResetDescriptorPool(device, pool.handle, 0);
        pool.allocatedSets = 0;
        pool.full = false;
    }
    frame.activePoolIndex = 0;
}

u64 DescriptorSetAllocator::AllocateTransient(u64 setLayoutHandle) {
    std::lock_guard lock(m_mutex);

    auto& frame = m_transientFrames[m_currentFrame];

    // Try current active pool
    if (frame.activePoolIndex < frame.pools.size()) {
        auto& pool = frame.pools[frame.activePoolIndex];
        if (!pool.full) {
            u64 set = AllocateFromPool(pool, setLayoutHandle);
            if (set != 0) {
                m_transientSetsAllocated++;
                return set;
            }
        }
    }

    // Current pool exhausted, try next or create new
    frame.activePoolIndex++;
    if (frame.activePoolIndex >= frame.pools.size()) {
        auto newPool = CreatePool(m_config.transientPoolSizes, false);
        frame.pools.push_back(newPool);
        m_poolExhaustedCount++;
    }

    auto& pool = frame.pools[frame.activePoolIndex];
    u64 set = AllocateFromPool(pool, setLayoutHandle);
    if (set != 0) {
        m_transientSetsAllocated++;
    }
    return set;
}

u64 DescriptorSetAllocator::AllocatePersistent(u64 setLayoutHandle) {
    std::lock_guard lock(m_mutex);

    // Try existing pools
    for (auto& pool : m_persistentPools) {
        if (!pool.full) {
            u64 set = AllocateFromPool(pool, setLayoutHandle);
            if (set != 0) {
                m_persistentSetsAllocated++;
                return set;
            }
        }
    }

    // All pools exhausted, create new
    auto newPool = CreatePool(m_config.persistentPoolSizes, true);
    m_persistentPools.push_back(newPool);
    m_poolExhaustedCount++;

    u64 set = AllocateFromPool(m_persistentPools.back(), setLayoutHandle);
    if (set != 0) {
        m_persistentSetsAllocated++;
    }
    return set;
}

void DescriptorSetAllocator::FreePersistent(u64 descriptorSet) {
    std::lock_guard lock(m_mutex);
    // TODO: vkFreeDescriptorSets(device, poolHandle, 1, &descriptorSet);
    // Only works with VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
    (void)descriptorSet;
}

DescriptorAllocatorStats DescriptorSetAllocator::GetStats() const {
    std::lock_guard lock(m_mutex);
    DescriptorAllocatorStats stats{};

    for (const auto& frame : m_transientFrames) {
        stats.transientPoolCount += static_cast<u32>(frame.pools.size());
    }
    stats.persistentPoolCount = static_cast<u32>(m_persistentPools.size());
    stats.transientSetsAllocated = m_transientSetsAllocated;
    stats.persistentSetsAllocated = m_persistentSetsAllocated;
    stats.poolExhaustedCount = m_poolExhaustedCount;

    return stats;
}

DescriptorSetAllocator::DescriptorPool DescriptorSetAllocator::CreatePool(
    const DescriptorPoolSizes& sizes, bool freeIndividual) {
    DescriptorPool pool;
    pool.maxSets = sizes.maxSets;
    pool.allocatedSets = 0;
    pool.full = false;

    // TODO:
    // VkDescriptorPoolSize poolSizes[] = {
    //     { VK_DESCRIPTOR_TYPE_SAMPLER, sizes.samplers },
    //     { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sizes.combinedImageSamplers },
    //     { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, sizes.sampledImages },
    //     { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, sizes.storageImages },
    //     { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, sizes.uniformBuffers },
    //     { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, sizes.storageBuffers },
    //     { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, sizes.inputAttachments },
    //     { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, sizes.accelerationStructures },
    // };
    // VkDescriptorPoolCreateInfo ci{};
    // ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    // ci.flags = freeIndividual ? VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT : 0;
    // ci.maxSets = sizes.maxSets;
    // ci.poolSizeCount = 8;
    // ci.pPoolSizes = poolSizes;
    // vkCreateDescriptorPool(device, &ci, nullptr, &pool.handle);

    static u64 nextHandle = 100;
    pool.handle = nextHandle++;
    (void)freeIndividual;

    return pool;
}

u64 DescriptorSetAllocator::AllocateFromPool(DescriptorPool& pool, u64 setLayoutHandle) {
    if (pool.allocatedSets >= pool.maxSets) {
        pool.full = true;
        return 0;
    }

    // TODO:
    // VkDescriptorSetAllocateInfo allocInfo{};
    // allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    // allocInfo.descriptorPool = pool.handle;
    // allocInfo.descriptorSetCount = 1;
    // VkDescriptorSetLayout layout = (VkDescriptorSetLayout)setLayoutHandle;
    // allocInfo.pSetLayouts = &layout;
    // VkDescriptorSet set;
    // VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &set);
    // if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
    //     pool.full = true;
    //     return 0;
    // }

    pool.allocatedSets++;
    (void)setLayoutHandle;
    return pool.handle * 10000 + pool.allocatedSets; // Stub handle
}

} // namespace nge::rhi
