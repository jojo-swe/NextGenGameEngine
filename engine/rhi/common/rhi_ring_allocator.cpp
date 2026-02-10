#include "engine/rhi/common/rhi_ring_allocator.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool DynamicRingAllocator::Init(const RingAllocatorConfig& config) {
    m_config = config;
    m_head = 0;
    m_tail = 0;
    m_currentFrame = 0;
    m_peakUsed = 0;
    m_totalAllocations = 0;
    m_totalFramesCompleted = 0;
    m_allocationsFailed = 0;
    m_frameAllocations.clear();

    NGE_LOG_INFO("Dynamic ring allocator initialized: size={} MB, alignment={}, framesInFlight={}",
                 config.bufferSize / (1024 * 1024), config.alignment, config.maxFramesInFlight);
    return true;
}

void DynamicRingAllocator::Shutdown() {
    m_frameAllocations.clear();
}

RingAllocation DynamicRingAllocator::Allocate(u64 sizeBytes) {
    std::lock_guard lock(m_mutex);

    RingAllocation alloc{};
    alloc.valid = false;

    if (sizeBytes == 0) return alloc;

    u64 alignedSize = AlignUp(sizeBytes, m_config.alignment);

    // Check if we have enough space
    u64 freeSpace = GetFreeSpace();
    if (alignedSize > freeSpace) {
        m_allocationsFailed++;
        NGE_LOG_WARN("Ring allocator: allocation of {} bytes failed (free: {})", alignedSize, freeSpace);
        return alloc;
    }

    // Check for wrap-around
    u64 spaceToEnd = m_config.bufferSize - m_head;

    if (alignedSize <= spaceToEnd) {
        // Fits without wrapping
        alloc.offset = m_head;
        alloc.size = sizeBytes;
        alloc.alignedSize = alignedSize;
        alloc.frameIndex = m_currentFrame;
        alloc.valid = true;

        m_head = (m_head + alignedSize) % m_config.bufferSize;
    } else {
        // Need to wrap: waste remaining space at end, allocate from beginning
        // Check if there's enough space at the beginning
        if (alignedSize > m_tail) {
            m_allocationsFailed++;
            return alloc;
        }

        // Waste the end portion
        m_head = 0;

        alloc.offset = 0;
        alloc.size = sizeBytes;
        alloc.alignedSize = alignedSize;
        alloc.frameIndex = m_currentFrame;
        alloc.valid = true;

        m_head = alignedSize;
    }

    m_totalAllocations++;

    u64 used = GetUsedSpace();
    if (used > m_peakUsed) m_peakUsed = used;

    return alloc;
}

void DynamicRingAllocator::FrameCompleted(u32 frameIndex) {
    std::lock_guard lock(m_mutex);

    // Remove all frame allocations for this frame and advance tail
    auto it = std::remove_if(m_frameAllocations.begin(), m_frameAllocations.end(),
                              [frameIndex](const FrameAllocation& fa) {
                                  return fa.frameIndex == frameIndex;
                              });

    if (it != m_frameAllocations.end()) {
        // Find the max endOffset for this frame to advance tail
        for (auto check = it; check != m_frameAllocations.end(); ++check) {
            // These are the ones being removed
        }
    }

    m_frameAllocations.erase(it, m_frameAllocations.end());

    // Advance tail to the oldest remaining frame's start
    if (!m_frameAllocations.empty()) {
        m_tail = m_frameAllocations.front().startOffset;
    } else {
        m_tail = m_head; // All reclaimed
    }

    m_totalFramesCompleted++;
}

void DynamicRingAllocator::BeginFrame() {
    std::lock_guard lock(m_mutex);

    // Record frame start position
    FrameAllocation fa;
    fa.startOffset = m_head;
    fa.endOffset = m_head;
    fa.frameIndex = m_currentFrame;

    m_frameAllocations.push_back(fa);
    m_currentFrame++;
}

u64 DynamicRingAllocator::GetWriteOffset() const {
    std::lock_guard lock(m_mutex);
    return m_head;
}

u64 DynamicRingAllocator::GetFreeSpace() const {
    if (m_head >= m_tail) {
        return m_config.bufferSize - (m_head - m_tail);
    }
    return m_tail - m_head;
}

u64 DynamicRingAllocator::GetUsedSpace() const {
    std::lock_guard lock(m_mutex);
    return m_config.bufferSize - GetFreeSpace();
}

bool DynamicRingAllocator::CanAllocate(u64 sizeBytes) const {
    std::lock_guard lock(m_mutex);
    u64 alignedSize = AlignUp(sizeBytes, m_config.alignment);
    return alignedSize <= GetFreeSpace();
}

u32 DynamicRingAllocator::GetCurrentFrame() const {
    std::lock_guard lock(m_mutex);
    return m_currentFrame;
}

void DynamicRingAllocator::Reset() {
    std::lock_guard lock(m_mutex);
    m_head = 0;
    m_tail = 0;
    m_currentFrame = 0;
    m_peakUsed = 0;
    m_totalAllocations = 0;
    m_totalFramesCompleted = 0;
    m_allocationsFailed = 0;
    m_frameAllocations.clear();
}

RingAllocatorStats DynamicRingAllocator::GetStats() const {
    std::lock_guard lock(m_mutex);

    RingAllocatorStats stats{};
    stats.bufferSize = m_config.bufferSize;
    stats.freeBytes = GetFreeSpace();
    stats.usedBytes = m_config.bufferSize - stats.freeBytes;
    stats.peakUsedBytes = m_peakUsed;
    stats.totalAllocations = m_totalAllocations;
    stats.totalFramesCompleted = m_totalFramesCompleted;
    stats.allocationsFailed = m_allocationsFailed;
    stats.utilization = m_config.bufferSize > 0
        ? static_cast<float>(stats.usedBytes) / static_cast<float>(m_config.bufferSize)
        : 0.0f;

    return stats;
}

u64 DynamicRingAllocator::AlignUp(u64 value, u64 alignment) const {
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace nge::rhi
