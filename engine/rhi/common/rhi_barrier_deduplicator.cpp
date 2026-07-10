#include "engine/rhi/common/rhi_barrier_deduplicator.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool BarrierDeduplicator::Init(const BarrierDedupConfig& config) {
    m_config = config;
    m_pending.reserve(config.maxBarriersPerBatch);
    m_totalSubmitted = 0;
    m_redundantRemoved = 0;
    m_merged = 0;
    m_totalFlushed = 0;
    m_barriersAfterDedup = 0;

    NGE_LOG_INFO("Barrier deduplicator initialized: removeRedundant={}, merge={}, maxPerBatch={}",
                 config.removeRedundant, config.mergeCompatible, config.maxBarriersPerBatch);
    return true;
}

void BarrierDeduplicator::Shutdown() {
    m_pending.clear();
}

void BarrierDeduplicator::QueueBarrier(const BarrierDesc& barrier) {
    std::lock_guard lock(m_mutex);
    m_totalSubmitted++;

    // Early redundancy check
    if (m_config.removeRedundant && IsRedundant(barrier)) {
        m_redundantRemoved++;
        return;
    }

    m_pending.push_back(barrier);
}

std::vector<BarrierDesc> BarrierDeduplicator::Flush() {
    std::lock_guard lock(m_mutex);

    if (m_pending.empty()) {
        m_totalFlushed++;
        return {};
    }

    std::vector<BarrierDesc> result;

    if (m_config.mergeCompatible) {
        // Merge barriers on the same resource
        std::vector<bool> consumed(m_pending.size(), false);

        for (u32 i = 0; i < static_cast<u32>(m_pending.size()); ++i) {
            if (consumed[i]) continue;

            BarrierDesc merged = m_pending[i];

            for (u32 j = i + 1; j < static_cast<u32>(m_pending.size()); ++j) {
                if (consumed[j]) continue;

                if (AreOnSameResource(merged, m_pending[j])) {
                    merged = MergeBarriers(merged, m_pending[j]);
                    consumed[j] = true;
                    m_merged++;
                }
            }

            result.push_back(std::move(merged));
        }
    } else {
        result = std::move(m_pending);
    }

    m_barriersAfterDedup += static_cast<u32>(result.size());
    m_totalFlushed++;
    m_pending.clear();

    return result;
}

u32 BarrierDeduplicator::GetPendingCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_pending.size());
}

bool BarrierDeduplicator::IsRedundant(const BarrierDesc& barrier) const {
    // A barrier is redundant if src and dst states are identical
    if (barrier.resourceType == BarrierResourceType::Image) {
        if (barrier.oldLayout == barrier.newLayout &&
            barrier.srcAccessMask == barrier.dstAccessMask &&
            barrier.srcStageMask == barrier.dstStageMask &&
            barrier.srcQueueFamily == barrier.dstQueueFamily) {
            return true;
        }
    } else {
        if (barrier.srcAccessMask == barrier.dstAccessMask &&
            barrier.srcStageMask == barrier.dstStageMask &&
            barrier.srcQueueFamily == barrier.dstQueueFamily) {
            return true;
        }
    }
    return false;
}

void BarrierDeduplicator::DiscardAll() {
    std::lock_guard lock(m_mutex);
    m_pending.clear();
}

void BarrierDeduplicator::Reset() {
    std::lock_guard lock(m_mutex);
    m_pending.clear();
    m_totalSubmitted = 0;
    m_redundantRemoved = 0;
    m_merged = 0;
    m_totalFlushed = 0;
    m_barriersAfterDedup = 0;
}

BarrierDedupStats BarrierDeduplicator::GetStats() const {
    std::lock_guard lock(m_mutex);

    BarrierDedupStats stats{};
    stats.totalBarriersSubmitted = m_totalSubmitted;
    stats.redundantRemoved = m_redundantRemoved;
    stats.merged = m_merged;
    stats.totalBatchesFlushed = m_totalFlushed;
    stats.barriersAfterDedup = m_barriersAfterDedup;
    stats.reductionRatio = m_totalSubmitted > 0
        ? static_cast<float>(m_redundantRemoved + m_merged) / static_cast<float>(m_totalSubmitted)
        : 0.0f;

    return stats;
}

bool BarrierDeduplicator::AreOnSameResource(const BarrierDesc& a, const BarrierDesc& b) const {
    if (a.resourceType != b.resourceType) return false;
    if (a.resourceHandle != b.resourceHandle) return false;

    // For images, also check subresource overlap
    if (a.resourceType == BarrierResourceType::Image) {
        bool mipOverlap = !(a.baseMipLevel + a.mipCount < b.baseMipLevel ||
                             b.baseMipLevel + b.mipCount < a.baseMipLevel);
        bool layerOverlap = !(a.baseArrayLayer + a.layerCount < b.baseArrayLayer ||
                               b.baseArrayLayer + b.layerCount < a.baseArrayLayer);
        return mipOverlap && layerOverlap;
    }

    return true;
}

BarrierDesc BarrierDeduplicator::MergeBarriers(const BarrierDesc& a, const BarrierDesc& b) const {
    BarrierDesc merged = a;

    // Merge stage masks (union)
    merged.srcStageMask |= b.srcStageMask;
    merged.dstStageMask |= b.dstStageMask;

    // Merge access masks (union)
    merged.srcAccessMask |= b.srcAccessMask;
    merged.dstAccessMask |= b.dstAccessMask;

    // For images: use the most specific layout transition
    // If layouts differ, prefer the later barrier's new layout
    if (a.resourceType == BarrierResourceType::Image) {
        merged.oldLayout = a.oldLayout;
        merged.newLayout = b.newLayout;

        // Merge subresource ranges
        u32 minMip = std::min(a.baseMipLevel, b.baseMipLevel);
        u32 maxMip = std::max(a.baseMipLevel + a.mipCount, b.baseMipLevel + b.mipCount);
        merged.baseMipLevel = minMip;
        merged.mipCount = maxMip - minMip;

        u32 minLayer = std::min(a.baseArrayLayer, b.baseArrayLayer);
        u32 maxLayer = std::max(a.baseArrayLayer + a.layerCount, b.baseArrayLayer + b.layerCount);
        merged.baseArrayLayer = minLayer;
        merged.layerCount = maxLayer - minLayer;
    }

    return merged;
}

} // namespace nge::rhi
