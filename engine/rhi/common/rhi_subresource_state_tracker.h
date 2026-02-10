#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Subresource State Tracker ───────────────────────────────────────
// Tracks per-mip, per-layer image layout and access state for Vulkan
// images. Enables automatic barrier insertion by knowing exact current
// state of every subresource.
//
// Use cases:
//   - Automatic layout transition barrier generation
//   - Detect redundant barriers (already in correct layout)
//   - Split barriers: track pending transitions
//   - Debug: detect layout mismatches / undefined transitions
//   - Render graph integration: provide current state per subresource

enum class ImageLayout : u8 {
    Undefined,
    General,
    ColorAttachment,
    DepthStencilAttachment,
    DepthStencilReadOnly,
    ShaderReadOnly,
    TransferSrc,
    TransferDst,
    PresentSrc,
    DepthReadOnlyStencilAttachment,
    DepthAttachmentStencilReadOnly,
    FragmentShadingRate,
};

enum class AccessFlags : u32 {
    None                = 0x0000,
    VertexRead          = 0x0001,
    IndexRead           = 0x0002,
    UniformRead         = 0x0004,
    InputAttachmentRead = 0x0008,
    ShaderRead          = 0x0010,
    ShaderWrite         = 0x0020,
    ColorAttachmentRead = 0x0040,
    ColorAttachmentWrite= 0x0080,
    DepthStencilRead    = 0x0100,
    DepthStencilWrite   = 0x0200,
    TransferRead        = 0x0400,
    TransferWrite       = 0x0800,
    HostRead            = 0x1000,
    HostWrite           = 0x2000,
    MemoryRead          = 0x4000,
    MemoryWrite         = 0x8000,
};

inline AccessFlags operator|(AccessFlags a, AccessFlags b) {
    return static_cast<AccessFlags>(static_cast<u32>(a) | static_cast<u32>(b));
}

inline bool HasAccess(u32 mask, AccessFlags flag) {
    return (mask & static_cast<u32>(flag)) != 0;
}

struct SubresourceKey {
    u32 mipLevel;
    u32 arrayLayer;

    bool operator==(const SubresourceKey& other) const {
        return mipLevel == other.mipLevel && arrayLayer == other.arrayLayer;
    }
};

struct SubresourceKeyHash {
    size_t operator()(const SubresourceKey& k) const {
        return std::hash<u64>()(static_cast<u64>(k.mipLevel) << 32 | k.arrayLayer);
    }
};

struct SubresourceState {
    ImageLayout layout;
    u32         accessMask;       // AccessFlags bitmask
    u32         queueFamily;      // VK_QUEUE_FAMILY_IGNORED = 0xFFFFFFFF
    bool        pendingTransition;
    ImageLayout pendingLayout;
};

struct ImageStateInfo {
    u64         imageHandle;
    std::string debugName;
    u32         mipLevels;
    u32         arrayLayers;
    std::unordered_map<SubresourceKey, SubresourceState, SubresourceKeyHash> subresources;
};

struct BarrierRequest {
    u64         imageHandle;
    u32         mipLevel;
    u32         arrayLayer;
    ImageLayout oldLayout;
    ImageLayout newLayout;
    u32         srcAccessMask;
    u32         dstAccessMask;
    u32         srcQueueFamily;
    u32         dstQueueFamily;
};

struct SubresourceTrackerConfig {
    u32  maxImages = 4096;
    bool trackQueueOwnership = true;
    bool detectRedundantBarriers = true;
    bool logTransitions = false;
};

struct SubresourceTrackerStats {
    u32 totalImages;
    u32 totalSubresources;
    u32 totalTransitions;
    u32 redundantTransitions;
    u32 pendingTransitions;
    u32 queueOwnershipTransfers;
};

class SubresourceStateTracker {
public:
    bool Init(const SubresourceTrackerConfig& config = {});
    void Shutdown();

    // Register an image with initial layout for all subresources
    void RegisterImage(u64 imageHandle, const std::string& debugName,
                        u32 mipLevels, u32 arrayLayers,
                        ImageLayout initialLayout = ImageLayout::Undefined);

    // Remove a tracked image
    void UnregisterImage(u64 imageHandle);

    // Transition a single subresource, returns barrier request
    BarrierRequest TransitionSubresource(u64 imageHandle, u32 mipLevel, u32 arrayLayer,
                                          ImageLayout newLayout, u32 newAccessMask);

    // Transition all subresources of an image
    std::vector<BarrierRequest> TransitionWholeImage(u64 imageHandle,
                                                       ImageLayout newLayout, u32 newAccessMask);

    // Transition a mip range
    std::vector<BarrierRequest> TransitionMipRange(u64 imageHandle,
                                                     u32 baseMip, u32 mipCount,
                                                     u32 baseLayer, u32 layerCount,
                                                     ImageLayout newLayout, u32 newAccessMask);

    // Queue ownership transfer
    BarrierRequest TransferQueueOwnership(u64 imageHandle, u32 mipLevel, u32 arrayLayer,
                                           u32 srcQueue, u32 dstQueue);

    // Query current state
    const SubresourceState* GetSubresourceState(u64 imageHandle, u32 mipLevel, u32 arrayLayer) const;
    ImageLayout GetLayout(u64 imageHandle, u32 mipLevel, u32 arrayLayer) const;

    // Check if all subresources are in the same layout
    bool IsWholeImageInLayout(u64 imageHandle, ImageLayout layout) const;

    // Check if image is registered
    bool IsTracked(u64 imageHandle) const;

    void Reset();

    SubresourceTrackerStats GetStats() const;

private:
    SubresourceTrackerConfig m_config;
    std::unordered_map<u64, ImageStateInfo> m_images;

    u32 m_totalTransitions = 0;
    u32 m_redundantTransitions = 0;
    u32 m_queueTransfers = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
