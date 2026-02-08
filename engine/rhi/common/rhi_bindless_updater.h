#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <mutex>

namespace nge::rhi {

// ─── Bindless Resource Table Updater ─────────────────────────────────────
// Batches descriptor writes for bindless rendering. Instead of issuing
// individual vkUpdateDescriptorSets calls, collects all pending writes
// and flushes them in a single batch per frame.
//
// Manages a global descriptor table where each texture/buffer/sampler
// is assigned a persistent index. Shaders access resources by index
// rather than through descriptor sets.

enum class BindlessResourceType : u8 {
    SampledImage,
    StorageImage,
    UniformBuffer,
    StorageBuffer,
    Sampler,
    AccelerationStructure,
};

struct BindlessEntry {
    u32                 index;
    BindlessResourceType type;
    u64                 resourceHandle;  // TextureHandle or BufferHandle
    u32                 mipLevel = 0;    // For image views
    u32                 arrayLayer = 0;
    bool                valid = false;
};

struct BindlessWrite {
    u32                 index;
    BindlessResourceType type;
    u64                 resourceHandle;
    u32                 mipLevel = 0;
    u32                 arrayLayer = 0;
};

struct BindlessTableConfig {
    u32 maxSampledImages = 16384;
    u32 maxStorageImages = 4096;
    u32 maxUniformBuffers = 4096;
    u32 maxStorageBuffers = 8192;
    u32 maxSamplers = 256;
    u32 maxAccelerationStructures = 256;
};

struct BindlessTableStats {
    u32 sampledImageCount;
    u32 storageImageCount;
    u32 uniformBufferCount;
    u32 storageBufferCount;
    u32 samplerCount;
    u32 pendingWrites;
    u32 totalUpdatesThisFrame;
};

class BindlessTableUpdater {
public:
    bool Init(IDevice* device, const BindlessTableConfig& config = {});
    void Shutdown();

    // Allocate a slot in the bindless table
    u32 AllocateSlot(BindlessResourceType type);

    // Free a slot (marks as available for reuse)
    void FreeSlot(u32 index, BindlessResourceType type);

    // Queue a descriptor write (batched, not immediate)
    void Write(const BindlessWrite& write);

    // Convenience: write a texture at a specific index
    void WriteSampledImage(u32 index, TextureHandle texture, u32 mipLevel = 0, u32 arrayLayer = 0);
    void WriteStorageImage(u32 index, TextureHandle texture, u32 mipLevel = 0);
    void WriteStorageBuffer(u32 index, BufferHandle buffer);
    void WriteUniformBuffer(u32 index, BufferHandle buffer);
    void WriteSampler(u32 index, SamplerHandle sampler);

    // Flush all pending writes to the GPU descriptor set
    u32 Flush();

    // Begin new frame (resets per-frame stats)
    void BeginFrame();

    // Get the bindless descriptor set for binding
    u64 GetDescriptorSet() const { return m_descriptorSet; }

    BindlessTableStats GetStats() const;
    const BindlessTableConfig& GetConfig() const { return m_config; }

private:
    struct FreeList {
        std::vector<u32> freeIndices;
        u32 nextIndex = 0;
        u32 maxCount = 0;
    };

    FreeList& GetFreeList(BindlessResourceType type);
    const FreeList& GetFreeList(BindlessResourceType type) const;

    IDevice* m_device = nullptr;
    BindlessTableConfig m_config;

    // Per-type free lists
    FreeList m_sampledImageFreeList;
    FreeList m_storageImageFreeList;
    FreeList m_uniformBufferFreeList;
    FreeList m_storageBufferFreeList;
    FreeList m_samplerFreeList;
    FreeList m_accelerationStructureFreeList;

    // Pending writes
    std::vector<BindlessWrite> m_pendingWrites;

    // Descriptor set handle
    u64 m_descriptorSet = 0;

    u32 m_totalUpdatesThisFrame = 0;
    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
