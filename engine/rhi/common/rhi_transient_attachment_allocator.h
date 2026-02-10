#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Transient Attachment Allocator ───────────────────────────────────
// Per-frame scratch allocator for render target attachments (color, depth)
// with memory aliasing. Transient attachments exist only within a render
// pass and can share the same backing memory when lifetimes don't overlap.
//
// Use cases:
//   - GBuffer intermediate attachments (normal, albedo, PBR)
//   - Shadow map scratch targets
//   - Ping-pong post-process buffers
//   - MSAA resolve intermediates
//   - Tile-based GPU: VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT

enum class AttachmentFormat : u8 {
    RGBA8_UNORM,
    RGBA8_SRGB,
    RGBA16_FLOAT,
    RGBA32_FLOAT,
    R16_FLOAT,
    R32_FLOAT,
    RG16_FLOAT,
    RG32_FLOAT,
    R11G11B10_FLOAT,
    D32_FLOAT,
    D24_UNORM_S8_UINT,
    D32_FLOAT_S8_UINT,
};

struct TransientAttachmentDesc {
    u32              width;
    u32              height;
    AttachmentFormat format;
    u32              samples;       // MSAA (1, 2, 4, 8)
    u32              arrayLayers;   // 1 for 2D
    std::string      debugName;
};

struct TransientAttachmentHandle {
    u64 id;           // Unique allocation ID
    u64 memoryBlock;  // Backing memory block (aliased)
    u64 imageHandle;  // VkImage or equivalent
};

struct TransientAttachmentSlot {
    TransientAttachmentDesc desc;
    TransientAttachmentHandle handle;
    u32  firstPassUsed;
    u32  lastPassUsed;
    u64  memorySizeBytes;
    bool inUse;
};

struct TransientAllocatorConfig {
    u32  maxAttachments = 128;
    u64  memoryBudget = 256 * 1024 * 1024; // 256 MB
    bool enableAliasing = true;
    bool preferLazyAllocation = true;       // VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT
    bool trackLifetimes = true;
};

struct TransientAllocatorStats {
    u32 totalAllocations;
    u32 activeAllocations;
    u32 aliasedAllocations;    // Reused existing memory
    u64 totalMemoryUsed;
    u64 memoryWithoutAliasing; // Would-be usage without aliasing
    u64 memorySaved;           // Saved by aliasing
    u32 peakActiveCount;
    u32 totalRecycled;
};

class TransientAttachmentAllocator {
public:
    bool Init(const TransientAllocatorConfig& config = {});
    void Shutdown();

    // Allocate a transient attachment for use in a pass range
    TransientAttachmentHandle Allocate(const TransientAttachmentDesc& desc,
                                        u32 firstPass, u32 lastPass);

    // Release an attachment (mark as available for aliasing)
    void Release(u64 allocationId);

    // Begin a new frame (recycles all allocations)
    void BeginFrame();

    // Get info about an allocation
    const TransientAttachmentSlot* GetSlot(u64 allocationId) const;

    // Check if two allocations can alias (non-overlapping lifetimes)
    bool CanAlias(u64 allocA, u64 allocB) const;

    // Get current memory usage
    u64 GetMemoryUsed() const;

    // Get number of active allocations
    u32 GetActiveCount() const;

    void Reset();

    TransientAllocatorStats GetStats() const;

private:
    u64 ComputeAttachmentSize(const TransientAttachmentDesc& desc) const;
    u64 FindAliasingCandidate(const TransientAttachmentDesc& desc, u32 firstPass, u32 lastPass) const;
    u64 AllocateMemoryBlock(u64 size);

    TransientAllocatorConfig m_config;
    std::vector<TransientAttachmentSlot> m_slots;
    std::unordered_map<u64, u32> m_slotIndex; // allocationId -> index

    u64 m_nextId = 1;
    u64 m_totalMemoryUsed = 0;
    u64 m_memoryWithoutAliasing = 0;
    u32 m_totalAllocations = 0;
    u32 m_aliasedAllocations = 0;
    u32 m_peakActive = 0;
    u32 m_totalRecycled = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
