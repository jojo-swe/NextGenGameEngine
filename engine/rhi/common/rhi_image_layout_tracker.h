#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <unordered_map>
#include <vector>
#include <mutex>

namespace nge::rhi {

// ─── GPU Image Layout Tracker ────────────────────────────────────────────
// Tracks the current VkImageLayout for every image subresource and
// automatically generates layout transition barriers when the render
// graph or manual code requests a new layout.
//
// Integrates with the barrier tracker to batch transitions.

enum class ImageLayout : u8 {
    Undefined,
    General,
    ColorAttachment,
    DepthStencilAttachment,
    DepthStencilReadOnly,
    ShaderReadOnly,
    TransferSrc,
    TransferDst,
    Present,
    DepthAttachment,
    StencilAttachment,
    ReadOnlyOptimal,
};

struct SubresourceKey {
    TextureHandle texture;
    u32           mipLevel;
    u32           arrayLayer;

    bool operator==(const SubresourceKey& other) const {
        return texture == other.texture && mipLevel == other.mipLevel && arrayLayer == other.arrayLayer;
    }
};

struct SubresourceKeyHash {
    size_t operator()(const SubresourceKey& key) const {
        size_t h = std::hash<u32>{}(key.texture.id);
        h ^= std::hash<u32>{}(key.mipLevel) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<u32>{}(key.arrayLayer) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct LayoutTransition {
    TextureHandle texture;
    u32           mipLevel;
    u32           arrayLayer;
    ImageLayout   oldLayout;
    ImageLayout   newLayout;
};

struct ImageLayoutTrackerStats {
    u32 trackedSubresources;
    u32 transitionsThisFrame;
    u32 redundantTransitionsAvoided;
};

class ImageLayoutTracker {
public:
    void Init();
    void Shutdown();

    // Set initial layout for a texture (call when texture is created)
    void SetInitialLayout(TextureHandle texture, ImageLayout layout, u32 mipLevels = 1, u32 arrayLayers = 1);

    // Get current layout for a subresource
    ImageLayout GetLayout(TextureHandle texture, u32 mipLevel = 0, u32 arrayLayer = 0) const;

    // Request a layout transition (returns transition info, or empty if already in target layout)
    std::vector<LayoutTransition> TransitionTo(TextureHandle texture, ImageLayout newLayout,
                                                 u32 mipLevel = 0, u32 arrayLayer = 0);

    // Transition all mips/layers of a texture
    std::vector<LayoutTransition> TransitionAll(TextureHandle texture, ImageLayout newLayout,
                                                  u32 mipLevels = 1, u32 arrayLayers = 1);

    // Remove tracking for a destroyed texture
    void RemoveTexture(TextureHandle texture);

    // Reset per-frame stats
    void BeginFrame();

    ImageLayoutTrackerStats GetStats() const;

    // Convert layout enum to string
    static const char* LayoutName(ImageLayout layout);

private:
    std::unordered_map<SubresourceKey, ImageLayout, SubresourceKeyHash> m_layouts;

    u32 m_transitionsThisFrame = 0;
    u32 m_redundantAvoided = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
