#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <mutex>

namespace nge::rhi {

// ─── GPU Descriptor Set Allocator ────────────────────────────────────────
// Pooled descriptor set allocation with per-frame reset.
// Maintains a ring of VkDescriptorPools that are reset each frame.
// Grows automatically when a pool runs out of space.
//
// Two allocation modes:
//   1. Transient: Reset every frame (for per-draw descriptor sets)
//   2. Persistent: Long-lived (for global/material descriptor sets)

struct DescriptorPoolSizes {
    u32 samplers = 128;
    u32 combinedImageSamplers = 1024;
    u32 sampledImages = 4096;
    u32 storageImages = 512;
    u32 uniformBuffers = 1024;
    u32 storageBuffers = 2048;
    u32 inputAttachments = 64;
    u32 accelerationStructures = 64;
    u32 maxSets = 2048;
};

struct DescriptorAllocatorConfig {
    u32 framesInFlight = 3;
    DescriptorPoolSizes transientPoolSizes;
    DescriptorPoolSizes persistentPoolSizes;
};

struct DescriptorAllocatorStats {
    u32 transientPoolCount;
    u32 persistentPoolCount;
    u32 transientSetsAllocated;
    u32 persistentSetsAllocated;
    u32 poolExhaustedCount;   // Times a pool ran out and new one created
};

class DescriptorSetAllocator {
public:
    bool Init(IDevice* device, const DescriptorAllocatorConfig& config = {});
    void Shutdown();

    // Per-frame: reset transient pools for the current frame
    void BeginFrame(u32 frameIndex);

    // Allocate a transient descriptor set (reset next frame cycle)
    u64 AllocateTransient(u64 setLayoutHandle);

    // Allocate a persistent descriptor set (lives until explicitly freed)
    u64 AllocatePersistent(u64 setLayoutHandle);

    // Free a persistent descriptor set
    void FreePersistent(u64 descriptorSet);

    DescriptorAllocatorStats GetStats() const;

private:
    struct DescriptorPool {
        u64 handle = 0;          // VkDescriptorPool
        u32 allocatedSets = 0;
        u32 maxSets = 0;
        bool full = false;
    };

    struct FrameTransientPools {
        std::vector<DescriptorPool> pools;
        u32 activePoolIndex = 0;
    };

    DescriptorPool CreatePool(const DescriptorPoolSizes& sizes, bool freeIndividual);
    u64 AllocateFromPool(DescriptorPool& pool, u64 setLayoutHandle);

    IDevice* m_device = nullptr;
    DescriptorAllocatorConfig m_config;

    // Per-frame transient pools (ring buffered)
    std::vector<FrameTransientPools> m_transientFrames;
    u32 m_currentFrame = 0;

    // Persistent pools (grow as needed)
    std::vector<DescriptorPool> m_persistentPools;

    u32 m_transientSetsAllocated = 0;
    u32 m_persistentSetsAllocated = 0;
    u32 m_poolExhaustedCount = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
