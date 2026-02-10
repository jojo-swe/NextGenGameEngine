#include "engine/rhi/common/rhi_subresource_state_tracker.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

static constexpr u32 QUEUE_FAMILY_IGNORED = 0xFFFFFFFF;

bool SubresourceStateTracker::Init(const SubresourceTrackerConfig& config) {
    m_config = config;
    m_images.reserve(config.maxImages);
    m_totalTransitions = 0;
    m_redundantTransitions = 0;
    m_queueTransfers = 0;

    NGE_LOG_INFO("Subresource state tracker initialized: maxImages={}, queueOwnership={}, detectRedundant={}",
                 config.maxImages, config.trackQueueOwnership, config.detectRedundantBarriers);
    return true;
}

void SubresourceStateTracker::Shutdown() {
    m_images.clear();
}

void SubresourceStateTracker::RegisterImage(u64 imageHandle, const std::string& debugName,
                                             u32 mipLevels, u32 arrayLayers,
                                             ImageLayout initialLayout) {
    std::lock_guard lock(m_mutex);

    if (m_images.size() >= m_config.maxImages) {
        NGE_LOG_WARN("Subresource state tracker: max images reached ({})", m_config.maxImages);
        return;
    }

    ImageStateInfo info;
    info.imageHandle = imageHandle;
    info.debugName = debugName;
    info.mipLevels = mipLevels;
    info.arrayLayers = arrayLayers;

    for (u32 mip = 0; mip < mipLevels; ++mip) {
        for (u32 layer = 0; layer < arrayLayers; ++layer) {
            SubresourceKey key{mip, layer};
            SubresourceState state;
            state.layout = initialLayout;
            state.accessMask = static_cast<u32>(AccessFlags::None);
            state.queueFamily = QUEUE_FAMILY_IGNORED;
            state.pendingTransition = false;
            state.pendingLayout = ImageLayout::Undefined;
            info.subresources[key] = state;
        }
    }

    m_images[imageHandle] = std::move(info);
}

void SubresourceStateTracker::UnregisterImage(u64 imageHandle) {
    std::lock_guard lock(m_mutex);
    m_images.erase(imageHandle);
}

BarrierRequest SubresourceStateTracker::TransitionSubresource(u64 imageHandle, u32 mipLevel, u32 arrayLayer,
                                                                ImageLayout newLayout, u32 newAccessMask) {
    std::lock_guard lock(m_mutex);

    BarrierRequest barrier{};
    barrier.imageHandle = imageHandle;
    barrier.mipLevel = mipLevel;
    barrier.arrayLayer = arrayLayer;
    barrier.srcQueueFamily = QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamily = QUEUE_FAMILY_IGNORED;

    auto imgIt = m_images.find(imageHandle);
    if (imgIt == m_images.end()) {
        NGE_LOG_WARN("Subresource state tracker: image 0x{:X} not registered", imageHandle);
        barrier.oldLayout = ImageLayout::Undefined;
        barrier.newLayout = newLayout;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = newAccessMask;
        return barrier;
    }

    SubresourceKey key{mipLevel, arrayLayer};
    auto& subresources = imgIt->second.subresources;
    auto subIt = subresources.find(key);

    if (subIt == subresources.end()) {
        barrier.oldLayout = ImageLayout::Undefined;
        barrier.newLayout = newLayout;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = newAccessMask;
        return barrier;
    }

    auto& state = subIt->second;

    // Check for redundant transition
    if (m_config.detectRedundantBarriers &&
        state.layout == newLayout && state.accessMask == newAccessMask) {
        m_redundantTransitions++;
        barrier.oldLayout = state.layout;
        barrier.newLayout = newLayout;
        barrier.srcAccessMask = state.accessMask;
        barrier.dstAccessMask = newAccessMask;
        return barrier;
    }

    barrier.oldLayout = state.layout;
    barrier.newLayout = newLayout;
    barrier.srcAccessMask = state.accessMask;
    barrier.dstAccessMask = newAccessMask;

    // Update state
    state.layout = newLayout;
    state.accessMask = newAccessMask;
    m_totalTransitions++;

    if (m_config.logTransitions) {
        NGE_LOG_DEBUG("Image 0x{:X} [{}] mip={} layer={}: layout {} -> {}",
                      imageHandle, imgIt->second.debugName, mipLevel, arrayLayer,
                      static_cast<u32>(barrier.oldLayout), static_cast<u32>(newLayout));
    }

    return barrier;
}

std::vector<BarrierRequest> SubresourceStateTracker::TransitionWholeImage(u64 imageHandle,
                                                                            ImageLayout newLayout,
                                                                            u32 newAccessMask) {
    std::lock_guard lock(m_mutex);
    std::vector<BarrierRequest> barriers;

    auto imgIt = m_images.find(imageHandle);
    if (imgIt == m_images.end()) return barriers;

    const auto& info = imgIt->second;

    for (auto& [key, state] : imgIt->second.subresources) {
        if (m_config.detectRedundantBarriers &&
            state.layout == newLayout && state.accessMask == newAccessMask) {
            m_redundantTransitions++;
            continue;
        }

        BarrierRequest barrier{};
        barrier.imageHandle = imageHandle;
        barrier.mipLevel = key.mipLevel;
        barrier.arrayLayer = key.arrayLayer;
        barrier.oldLayout = state.layout;
        barrier.newLayout = newLayout;
        barrier.srcAccessMask = state.accessMask;
        barrier.dstAccessMask = newAccessMask;
        barrier.srcQueueFamily = QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamily = QUEUE_FAMILY_IGNORED;

        state.layout = newLayout;
        state.accessMask = newAccessMask;
        m_totalTransitions++;

        barriers.push_back(std::move(barrier));
    }

    return barriers;
}

std::vector<BarrierRequest> SubresourceStateTracker::TransitionMipRange(u64 imageHandle,
                                                                          u32 baseMip, u32 mipCount,
                                                                          u32 baseLayer, u32 layerCount,
                                                                          ImageLayout newLayout, u32 newAccessMask) {
    std::lock_guard lock(m_mutex);
    std::vector<BarrierRequest> barriers;

    auto imgIt = m_images.find(imageHandle);
    if (imgIt == m_images.end()) return barriers;

    for (u32 mip = baseMip; mip < baseMip + mipCount; ++mip) {
        for (u32 layer = baseLayer; layer < baseLayer + layerCount; ++layer) {
            SubresourceKey key{mip, layer};
            auto subIt = imgIt->second.subresources.find(key);
            if (subIt == imgIt->second.subresources.end()) continue;

            auto& state = subIt->second;

            if (m_config.detectRedundantBarriers &&
                state.layout == newLayout && state.accessMask == newAccessMask) {
                m_redundantTransitions++;
                continue;
            }

            BarrierRequest barrier{};
            barrier.imageHandle = imageHandle;
            barrier.mipLevel = mip;
            barrier.arrayLayer = layer;
            barrier.oldLayout = state.layout;
            barrier.newLayout = newLayout;
            barrier.srcAccessMask = state.accessMask;
            barrier.dstAccessMask = newAccessMask;
            barrier.srcQueueFamily = QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamily = QUEUE_FAMILY_IGNORED;

            state.layout = newLayout;
            state.accessMask = newAccessMask;
            m_totalTransitions++;

            barriers.push_back(std::move(barrier));
        }
    }

    return barriers;
}

BarrierRequest SubresourceStateTracker::TransferQueueOwnership(u64 imageHandle, u32 mipLevel, u32 arrayLayer,
                                                                 u32 srcQueue, u32 dstQueue) {
    std::lock_guard lock(m_mutex);

    BarrierRequest barrier{};
    barrier.imageHandle = imageHandle;
    barrier.mipLevel = mipLevel;
    barrier.arrayLayer = arrayLayer;
    barrier.srcQueueFamily = srcQueue;
    barrier.dstQueueFamily = dstQueue;

    auto imgIt = m_images.find(imageHandle);
    if (imgIt == m_images.end()) return barrier;

    SubresourceKey key{mipLevel, arrayLayer};
    auto subIt = imgIt->second.subresources.find(key);
    if (subIt == imgIt->second.subresources.end()) return barrier;

    auto& state = subIt->second;
    barrier.oldLayout = state.layout;
    barrier.newLayout = state.layout; // Layout doesn't change during ownership transfer
    barrier.srcAccessMask = state.accessMask;
    barrier.dstAccessMask = state.accessMask;

    state.queueFamily = dstQueue;
    m_queueTransfers++;

    return barrier;
}

const SubresourceState* SubresourceStateTracker::GetSubresourceState(u64 imageHandle, u32 mipLevel, u32 arrayLayer) const {
    std::lock_guard lock(m_mutex);

    auto imgIt = m_images.find(imageHandle);
    if (imgIt == m_images.end()) return nullptr;

    SubresourceKey key{mipLevel, arrayLayer};
    auto subIt = imgIt->second.subresources.find(key);
    if (subIt == imgIt->second.subresources.end()) return nullptr;

    return &subIt->second;
}

ImageLayout SubresourceStateTracker::GetLayout(u64 imageHandle, u32 mipLevel, u32 arrayLayer) const {
    const auto* state = GetSubresourceState(imageHandle, mipLevel, arrayLayer);
    return state ? state->layout : ImageLayout::Undefined;
}

bool SubresourceStateTracker::IsWholeImageInLayout(u64 imageHandle, ImageLayout layout) const {
    std::lock_guard lock(m_mutex);

    auto imgIt = m_images.find(imageHandle);
    if (imgIt == m_images.end()) return false;

    for (const auto& [key, state] : imgIt->second.subresources) {
        if (state.layout != layout) return false;
    }

    return true;
}

bool SubresourceStateTracker::IsTracked(u64 imageHandle) const {
    std::lock_guard lock(m_mutex);
    return m_images.count(imageHandle) > 0;
}

void SubresourceStateTracker::Reset() {
    std::lock_guard lock(m_mutex);
    m_images.clear();
    m_totalTransitions = 0;
    m_redundantTransitions = 0;
    m_queueTransfers = 0;
}

SubresourceTrackerStats SubresourceStateTracker::GetStats() const {
    std::lock_guard lock(m_mutex);
    SubresourceTrackerStats stats{};
    stats.totalImages = static_cast<u32>(m_images.size());

    u32 totalSub = 0;
    u32 pending = 0;
    for (const auto& [handle, info] : m_images) {
        totalSub += static_cast<u32>(info.subresources.size());
        for (const auto& [key, state] : info.subresources) {
            if (state.pendingTransition) pending++;
        }
    }

    stats.totalSubresources = totalSub;
    stats.totalTransitions = m_totalTransitions;
    stats.redundantTransitions = m_redundantTransitions;
    stats.pendingTransitions = pending;
    stats.queueOwnershipTransfers = m_queueTransfers;

    return stats;
}

} // namespace nge::rhi
