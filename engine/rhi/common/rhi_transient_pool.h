#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/rhi/common/rhi_format_utils.h"
#include <vector>
#include <unordered_map>

namespace nge::rhi {

// ─── Transient Resource Pool ─────────────────────────────────────────────
// Manages per-frame GPU resources that exist only for the duration of a
// single frame. Resources with non-overlapping lifetimes are aliased to
// the same GPU memory, reducing VRAM consumption.
//
// The render graph provides lifetime information (first use pass, last use
// pass), and this pool assigns memory from a set of heaps with aliasing.
//
// Key design:
//   - Resources are allocated from shared VkDeviceMemory heaps
//   - Aliasing barriers emitted between conflicting resources
//   - Per-frame reset: all transient resources invalidated
//   - Texture and buffer support

struct TransientTextureDesc {
    u32    width;
    u32    height;
    u32    depth = 1;
    u32    mipLevels = 1;
    u32    arrayLayers = 1;
    Format format;
    TextureUsage usage;
    u32    sampleCount = 1;
    const char* debugName = nullptr;
};

struct TransientBufferDesc {
    u32         size;
    BufferUsage usage;
    const char* debugName = nullptr;
};

struct TransientResource {
    enum class Type : u8 { Texture, Buffer };
    Type          type;
    TextureHandle texture;
    BufferHandle  buffer;
    u64           memoryOffset;
    u64           memorySize;
    u32           heapIndex;
    u32           firstPassIndex;  // First pass that uses this resource
    u32           lastPassIndex;   // Last pass that uses this resource
};

class TransientResourcePool {
public:
    struct Config {
        u64 heapSize = 256 * 1024 * 1024; // 256 MB per heap
        u32 maxHeaps = 4;
        u32 maxResources = 512;
    };

    bool Init(IDevice* device, const Config& config = {});
    void Shutdown();

    // Per-frame lifecycle
    void BeginFrame();
    void EndFrame();

    // Allocate a transient texture (returns handle valid for this frame only)
    TextureHandle AllocateTexture(const TransientTextureDesc& desc,
                                    u32 firstPass, u32 lastPass);

    // Allocate a transient buffer
    BufferHandle AllocateBuffer(const TransientBufferDesc& desc,
                                  u32 firstPass, u32 lastPass);

    // Check if two resources can alias (non-overlapping lifetimes)
    bool CanAlias(u32 resourceA, u32 resourceB) const;

    // Insert aliasing barriers between resources sharing memory
    void InsertAliasingBarriers(ICommandList* cmd, u32 currentPass);

    // Stats
    u32 GetResourceCount() const { return static_cast<u32>(m_resources.size()); }
    u32 GetHeapCount() const { return static_cast<u32>(m_heaps.size()); }
    u64 GetTotalAllocated() const;
    u64 GetTotalAliased() const;   // Memory saved by aliasing
    f32 GetAliasingEfficiency() const;

private:
    struct Heap {
        u64 handle = 0;    // VkDeviceMemory as u64
        u64 capacity = 0;
        u64 peakUsage = 0;
    };

    struct AllocationSlot {
        u64 offset;
        u64 size;
        u32 firstPass;
        u32 lastPass;
        bool inUse;
    };

    u64 AllocateFromHeap(u32 heapIndex, u64 size, u64 alignment,
                           u32 firstPass, u32 lastPass);
    u32 FindOrCreateHeap(u64 requiredSize);

    IDevice* m_device = nullptr;
    Config m_config;

    std::vector<Heap> m_heaps;
    std::vector<TransientResource> m_resources;
    std::vector<std::vector<AllocationSlot>> m_heapAllocations; // Per-heap slots

    u64 m_totalAllocatedWithoutAliasing = 0;
    u64 m_totalActualMemory = 0;
};

} // namespace nge::rhi
