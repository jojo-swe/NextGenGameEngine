#include "engine/rhi/common/rhi_descriptor_update_batcher.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool DescriptorUpdateBatcher::Init(const DescUpdateBatcherConfig& config) {
    m_config = config;
    m_pendingWrites.reserve(config.maxPendingWrites);
    m_pendingCopies.reserve(config.maxPendingCopies);
    m_totalWritesQueued = 0;
    m_totalCopiesQueued = 0;
    m_totalFlushes = 0;
    m_totalWritesFlushed = 0;
    m_totalCopiesFlushed = 0;
    m_coalescedWrites = 0;

    NGE_LOG_INFO("Descriptor update batcher initialized: maxWrites={}, maxCopies={}, autoFlush={}, coalesce={}",
                 config.maxPendingWrites, config.maxPendingCopies, config.autoFlushThreshold, config.enableCoalescing);
    return true;
}

void DescriptorUpdateBatcher::Shutdown() {
    m_pendingWrites.clear();
    m_pendingCopies.clear();
    m_perSetWriteCount.clear();
}

void DescriptorUpdateBatcher::WriteBuffer(u64 descriptorSet, u32 binding, u32 arrayElement,
                                            DescWriteType type, u64 bufferHandle, u64 offset, u64 range) {
    std::lock_guard lock(m_mutex);

    DescriptorWrite write{};
    write.descriptorSet = descriptorSet;
    write.binding = binding;
    write.arrayElement = arrayElement;
    write.count = 1;
    write.type = type;
    write.bufferInfo.bufferHandle = bufferHandle;
    write.bufferInfo.offset = offset;
    write.bufferInfo.range = range;

    if (m_config.enableCoalescing && TryCoalesce(write)) {
        m_coalescedWrites++;
        m_totalWritesQueued++;
        return;
    }

    if (m_pendingWrites.size() >= m_config.maxPendingWrites) {
        NGE_LOG_WARN("Descriptor update batcher: max pending writes reached, auto-flushing");
        Flush();
    }

    m_pendingWrites.push_back(std::move(write));
    m_totalWritesQueued++;

    if (m_config.trackPerSetStats) {
        m_perSetWriteCount[descriptorSet]++;
    }

    AutoFlushIfNeeded();
}

void DescriptorUpdateBatcher::WriteImage(u64 descriptorSet, u32 binding, u32 arrayElement,
                                           DescWriteType type, u64 imageViewHandle, u64 samplerHandle, u32 imageLayout) {
    std::lock_guard lock(m_mutex);

    DescriptorWrite write{};
    write.descriptorSet = descriptorSet;
    write.binding = binding;
    write.arrayElement = arrayElement;
    write.count = 1;
    write.type = type;
    write.imageInfo.imageViewHandle = imageViewHandle;
    write.imageInfo.samplerHandle = samplerHandle;
    write.imageInfo.imageLayout = imageLayout;

    if (m_config.enableCoalescing && TryCoalesce(write)) {
        m_coalescedWrites++;
        m_totalWritesQueued++;
        return;
    }

    if (m_pendingWrites.size() >= m_config.maxPendingWrites) {
        NGE_LOG_WARN("Descriptor update batcher: max pending writes reached, auto-flushing");
        Flush();
    }

    m_pendingWrites.push_back(std::move(write));
    m_totalWritesQueued++;

    if (m_config.trackPerSetStats) {
        m_perSetWriteCount[descriptorSet]++;
    }

    AutoFlushIfNeeded();
}

void DescriptorUpdateBatcher::CopyDescriptor(u64 srcSet, u32 srcBinding, u32 srcArrayElement,
                                                u64 dstSet, u32 dstBinding, u32 dstArrayElement, u32 count) {
    std::lock_guard lock(m_mutex);

    if (m_pendingCopies.size() >= m_config.maxPendingCopies) {
        NGE_LOG_WARN("Descriptor update batcher: max pending copies reached");
        return;
    }

    DescriptorCopy copy{};
    copy.srcSet = srcSet;
    copy.srcBinding = srcBinding;
    copy.srcArrayElement = srcArrayElement;
    copy.dstSet = dstSet;
    copy.dstBinding = dstBinding;
    copy.dstArrayElement = dstArrayElement;
    copy.count = count;

    m_pendingCopies.push_back(std::move(copy));
    m_totalCopiesQueued++;
}

u32 DescriptorUpdateBatcher::Flush() {
    std::lock_guard lock(m_mutex);

    u32 writesCount = static_cast<u32>(m_pendingWrites.size());
    u32 copiesCount = static_cast<u32>(m_pendingCopies.size());

    if (writesCount == 0 && copiesCount == 0) return 0;

    // TODO: Convert to VkWriteDescriptorSet array and call vkUpdateDescriptorSets
    // VkWriteDescriptorSet* vkWrites = ...;
    // for (const auto& w : m_pendingWrites) { convert w to VkWriteDescriptorSet }
    // VkCopyDescriptorSet* vkCopies = ...;
    // for (const auto& c : m_pendingCopies) { convert c to VkCopyDescriptorSet }
    // vkUpdateDescriptorSets(device, writesCount, vkWrites, copiesCount, vkCopies);

    m_totalWritesFlushed += writesCount;
    m_totalCopiesFlushed += copiesCount;
    m_totalFlushes++;

    m_pendingWrites.clear();
    m_pendingCopies.clear();
    m_perSetWriteCount.clear();

    NGE_LOG_DEBUG("Descriptor batcher flushed: {} writes, {} copies", writesCount, copiesCount);

    return writesCount + copiesCount;
}

u32 DescriptorUpdateBatcher::FlushSet(u64 descriptorSet) {
    std::lock_guard lock(m_mutex);

    // Partition: move writes for this set to a separate batch
    std::vector<DescriptorWrite> setWrites;
    std::vector<DescriptorWrite> remaining;

    for (auto& w : m_pendingWrites) {
        if (w.descriptorSet == descriptorSet) {
            setWrites.push_back(std::move(w));
        } else {
            remaining.push_back(std::move(w));
        }
    }

    m_pendingWrites = std::move(remaining);

    if (setWrites.empty()) return 0;

    // TODO: vkUpdateDescriptorSets for setWrites only

    u32 count = static_cast<u32>(setWrites.size());
    m_totalWritesFlushed += count;
    m_totalFlushes++;
    m_perSetWriteCount.erase(descriptorSet);

    return count;
}

u32 DescriptorUpdateBatcher::GetPendingWriteCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_pendingWrites.size());
}

u32 DescriptorUpdateBatcher::GetPendingCopyCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_pendingCopies.size());
}

bool DescriptorUpdateBatcher::HasPendingWrites(u64 descriptorSet) const {
    std::lock_guard lock(m_mutex);

    for (const auto& w : m_pendingWrites) {
        if (w.descriptorSet == descriptorSet) return true;
    }

    return false;
}

void DescriptorUpdateBatcher::DiscardAll() {
    std::lock_guard lock(m_mutex);
    m_pendingWrites.clear();
    m_pendingCopies.clear();
    m_perSetWriteCount.clear();
}

void DescriptorUpdateBatcher::Reset() {
    std::lock_guard lock(m_mutex);
    m_pendingWrites.clear();
    m_pendingCopies.clear();
    m_perSetWriteCount.clear();
    m_totalWritesQueued = 0;
    m_totalCopiesQueued = 0;
    m_totalFlushes = 0;
    m_totalWritesFlushed = 0;
    m_totalCopiesFlushed = 0;
    m_coalescedWrites = 0;
}

DescUpdateBatcherStats DescriptorUpdateBatcher::GetStats() const {
    std::lock_guard lock(m_mutex);
    DescUpdateBatcherStats stats{};
    stats.totalWritesQueued = m_totalWritesQueued;
    stats.totalCopiesQueued = m_totalCopiesQueued;
    stats.totalFlushes = m_totalFlushes;
    stats.totalWritesFlushed = m_totalWritesFlushed;
    stats.totalCopiesFlushed = m_totalCopiesFlushed;
    stats.coalescedWrites = m_coalescedWrites;
    stats.pendingWrites = static_cast<u32>(m_pendingWrites.size());
    stats.pendingCopies = static_cast<u32>(m_pendingCopies.size());
    stats.uniqueSetsUpdated = static_cast<u32>(m_perSetWriteCount.size());
    return stats;
}

void DescriptorUpdateBatcher::AutoFlushIfNeeded() {
    if (m_pendingWrites.size() >= m_config.autoFlushThreshold) {
        // Flush without re-locking (caller already holds lock)
        u32 writesCount = static_cast<u32>(m_pendingWrites.size());
        u32 copiesCount = static_cast<u32>(m_pendingCopies.size());

        m_totalWritesFlushed += writesCount;
        m_totalCopiesFlushed += copiesCount;
        m_totalFlushes++;

        m_pendingWrites.clear();
        m_pendingCopies.clear();
        m_perSetWriteCount.clear();

        NGE_LOG_DEBUG("Descriptor batcher auto-flushed: {} writes, {} copies", writesCount, copiesCount);
    }
}

bool DescriptorUpdateBatcher::TryCoalesce(const DescriptorWrite& write) {
    // Look for an existing write to the same set + binding, replace it
    for (auto& existing : m_pendingWrites) {
        if (existing.descriptorSet == write.descriptorSet &&
            existing.binding == write.binding &&
            existing.arrayElement == write.arrayElement &&
            existing.type == write.type) {
            // Replace the existing write with the new one
            existing = write;
            return true;
        }
    }
    return false;
}

} // namespace nge::rhi
