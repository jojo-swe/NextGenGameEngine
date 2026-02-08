#include "engine/rhi/common/rhi_image_layout_tracker.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

void ImageLayoutTracker::Init() {
    m_layouts.clear();
    m_transitionsThisFrame = 0;
    m_redundantAvoided = 0;
}

void ImageLayoutTracker::Shutdown() {
    m_layouts.clear();
}

void ImageLayoutTracker::SetInitialLayout(TextureHandle texture, ImageLayout layout,
                                            u32 mipLevels, u32 arrayLayers) {
    std::lock_guard lock(m_mutex);
    for (u32 mip = 0; mip < mipLevels; ++mip) {
        for (u32 layer = 0; layer < arrayLayers; ++layer) {
            SubresourceKey key{texture, mip, layer};
            m_layouts[key] = layout;
        }
    }
}

ImageLayout ImageLayoutTracker::GetLayout(TextureHandle texture, u32 mipLevel, u32 arrayLayer) const {
    std::lock_guard lock(m_mutex);
    SubresourceKey key{texture, mipLevel, arrayLayer};
    auto it = m_layouts.find(key);
    if (it != m_layouts.end()) return it->second;
    return ImageLayout::Undefined;
}

std::vector<LayoutTransition> ImageLayoutTracker::TransitionTo(TextureHandle texture, ImageLayout newLayout,
                                                                 u32 mipLevel, u32 arrayLayer) {
    std::lock_guard lock(m_mutex);
    std::vector<LayoutTransition> transitions;

    SubresourceKey key{texture, mipLevel, arrayLayer};
    auto it = m_layouts.find(key);
    ImageLayout oldLayout = (it != m_layouts.end()) ? it->second : ImageLayout::Undefined;

    if (oldLayout == newLayout) {
        m_redundantAvoided++;
        return transitions;
    }

    LayoutTransition t;
    t.texture = texture;
    t.mipLevel = mipLevel;
    t.arrayLayer = arrayLayer;
    t.oldLayout = oldLayout;
    t.newLayout = newLayout;
    transitions.push_back(t);

    m_layouts[key] = newLayout;
    m_transitionsThisFrame++;

    return transitions;
}

std::vector<LayoutTransition> ImageLayoutTracker::TransitionAll(TextureHandle texture, ImageLayout newLayout,
                                                                  u32 mipLevels, u32 arrayLayers) {
    std::lock_guard lock(m_mutex);
    std::vector<LayoutTransition> transitions;

    for (u32 mip = 0; mip < mipLevels; ++mip) {
        for (u32 layer = 0; layer < arrayLayers; ++layer) {
            SubresourceKey key{texture, mip, layer};
            auto it = m_layouts.find(key);
            ImageLayout oldLayout = (it != m_layouts.end()) ? it->second : ImageLayout::Undefined;

            if (oldLayout == newLayout) {
                m_redundantAvoided++;
                continue;
            }

            LayoutTransition t;
            t.texture = texture;
            t.mipLevel = mip;
            t.arrayLayer = layer;
            t.oldLayout = oldLayout;
            t.newLayout = newLayout;
            transitions.push_back(t);

            m_layouts[key] = newLayout;
            m_transitionsThisFrame++;
        }
    }

    return transitions;
}

void ImageLayoutTracker::RemoveTexture(TextureHandle texture) {
    std::lock_guard lock(m_mutex);
    for (auto it = m_layouts.begin(); it != m_layouts.end(); ) {
        if (it->first.texture == texture) {
            it = m_layouts.erase(it);
        } else {
            ++it;
        }
    }
}

void ImageLayoutTracker::BeginFrame() {
    m_transitionsThisFrame = 0;
    m_redundantAvoided = 0;
}

ImageLayoutTrackerStats ImageLayoutTracker::GetStats() const {
    std::lock_guard lock(m_mutex);
    ImageLayoutTrackerStats stats{};
    stats.trackedSubresources = static_cast<u32>(m_layouts.size());
    stats.transitionsThisFrame = m_transitionsThisFrame;
    stats.redundantTransitionsAvoided = m_redundantAvoided;
    return stats;
}

const char* ImageLayoutTracker::LayoutName(ImageLayout layout) {
    switch (layout) {
        case ImageLayout::Undefined:               return "Undefined";
        case ImageLayout::General:                 return "General";
        case ImageLayout::ColorAttachment:         return "ColorAttachment";
        case ImageLayout::DepthStencilAttachment:  return "DepthStencilAttachment";
        case ImageLayout::DepthStencilReadOnly:    return "DepthStencilReadOnly";
        case ImageLayout::ShaderReadOnly:          return "ShaderReadOnly";
        case ImageLayout::TransferSrc:             return "TransferSrc";
        case ImageLayout::TransferDst:             return "TransferDst";
        case ImageLayout::Present:                 return "Present";
        case ImageLayout::DepthAttachment:         return "DepthAttachment";
        case ImageLayout::StencilAttachment:       return "StencilAttachment";
        case ImageLayout::ReadOnlyOptimal:         return "ReadOnlyOptimal";
    }
    return "Unknown";
}

} // namespace nge::rhi
