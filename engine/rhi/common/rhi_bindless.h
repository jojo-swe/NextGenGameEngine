#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <queue>
#include <mutex>

namespace nge::rhi {

// ─── Bindless Descriptor Table Manager ───────────────────────────────────
// Manages a single large descriptor array (texture2D[], buffer[], sampler[])
// for bindless rendering. Shaders index into this table via material data.
//
// Key design:
//   - Fixed-size descriptor arrays per resource type
//   - Free-list allocation for descriptor indices
//   - Deferred release (wait N frames before reusing a slot)
//   - Thread-safe allocate/release

using BindlessIndex = u32;
inline constexpr BindlessIndex INVALID_BINDLESS = UINT32_MAX;

enum class BindlessType : u8 {
    Texture2D = 0,
    TextureCube,
    Texture3D,
    Buffer,
    Sampler,
    StorageImage,
    Count,
};

struct BindlessTableConfig {
    u32 maxTextures2D    = 16384;
    u32 maxTexturesCube  = 1024;
    u32 maxTextures3D    = 256;
    u32 maxBuffers       = 4096;
    u32 maxSamplers      = 128;
    u32 maxStorageImages = 1024;
    u32 releaseLatency   = 3; // Frames before a released slot can be reused
};

class BindlessTable {
public:
    bool Init(IDevice* device, const BindlessTableConfig& config = {});
    void Shutdown();

    // Allocate a bindless index for a resource
    BindlessIndex AllocateTexture2D(TextureHandle texture);
    BindlessIndex AllocateTextureCube(TextureHandle texture);
    BindlessIndex AllocateTexture3D(TextureHandle texture);
    BindlessIndex AllocateBuffer(BufferHandle buffer);
    BindlessIndex AllocateSampler(/* sampler handle */);
    BindlessIndex AllocateStorageImage(TextureHandle texture);

    // Release a bindless index (deferred by releaseLatency frames)
    void Release(BindlessType type, BindlessIndex index);

    // Update an existing slot with a new resource
    void UpdateTexture2D(BindlessIndex index, TextureHandle newTexture);
    void UpdateBuffer(BindlessIndex index, BufferHandle newBuffer);

    // Per-frame: process deferred releases
    void BeginFrame(u64 frameIndex);

    // Get descriptor set for shader binding
    // (In Vulkan, this is the VkDescriptorSet containing the bindless arrays)
    u64 GetDescriptorSet() const { return m_descriptorSet; }

    // Stats
    u32 GetAllocatedCount(BindlessType type) const;
    u32 GetCapacity(BindlessType type) const;
    f32 GetOccupancy(BindlessType type) const;

private:
    struct TypeTable {
        u32                   capacity = 0;
        std::vector<bool>     used;
        std::queue<u32>       freeList;

        struct DeferredRelease {
            u32 index;
            u64 releaseFrame;
        };
        std::vector<DeferredRelease> pendingReleases;

        u32 allocated = 0;
    };

    BindlessIndex AllocateSlot(BindlessType type);
    void WriteDescriptor(BindlessType type, BindlessIndex index, TextureHandle tex, BufferHandle buf);

    IDevice* m_device = nullptr;
    BindlessTableConfig m_config;
    u64 m_currentFrame = 0;

    TypeTable m_tables[static_cast<u32>(BindlessType::Count)];
    u64 m_descriptorSet = 0; // VkDescriptorSet handle

    std::mutex m_mutex;
};

} // namespace nge::rhi
