#include "engine/rhi/common/rhi_buffer_suballocator.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool BufferSubAllocator::Init(IDevice* device, const SubAllocatorConfig& config) {
    m_device = device;
    m_config = config;
    m_allocationCount = 0;

    m_heaps.reserve(config.maxHeaps);

    NGE_LOG_INFO("Buffer suballocator initialized: {} MB heaps, max {} heaps",
                 config.heapSize / (1024 * 1024), config.maxHeaps);
    return true;
}

void BufferSubAllocator::Shutdown() {
    for (auto& heap : m_heaps) {
        if (heap.buffer.IsValid()) {
            m_device->DestroyBuffer(heap.buffer);
        }
    }
    m_heaps.clear();
    m_allocationCount = 0;
}

SubAllocation BufferSubAllocator::Allocate(u64 size, u32 alignment) {
    std::lock_guard lock(m_mutex);

    if (alignment == 0) alignment = m_config.defaultAlignment;

    // Try each existing heap
    for (u32 i = 0; i < static_cast<u32>(m_heaps.size()); ++i) {
        auto alloc = AllocateFromHeap(i, size, alignment);
        if (alloc.valid) return alloc;
    }

    // No space in existing heaps — create a new one
    if (m_heaps.size() < m_config.maxHeaps) {
        u32 heapIdx = CreateHeap();
        return AllocateFromHeap(heapIdx, size, alignment);
    }

    NGE_LOG_ERROR("Buffer suballocator: all heaps full ({} heaps, {} bytes requested)",
                  m_heaps.size(), size);
    return {};
}

void BufferSubAllocator::Free(const SubAllocation& alloc) {
    if (!alloc.valid) return;
    std::lock_guard lock(m_mutex);

    if (alloc.heapIndex >= m_heaps.size()) return;
    auto& heap = m_heaps[alloc.heapIndex];

    // Add free block
    FreeBlock block;
    block.offset = alloc.offset;
    block.size = alloc.size;
    heap.freeBlocks.push_back(block);
    heap.allocated -= alloc.size;
    m_allocationCount--;

    // Coalesce adjacent blocks in this heap
    CoalesceHeap(heap);
}

u32 BufferSubAllocator::Coalesce() {
    std::lock_guard lock(m_mutex);
    u32 totalCoalesced = 0;
    for (auto& heap : m_heaps) {
        u32 before = static_cast<u32>(heap.freeBlocks.size());
        CoalesceHeap(heap);
        u32 after = static_cast<u32>(heap.freeBlocks.size());
        totalCoalesced += (before - after);
    }
    return totalCoalesced;
}

SubAllocatorStats BufferSubAllocator::GetStats() const {
    std::lock_guard lock(m_mutex);
    SubAllocatorStats stats{};
    stats.heapCount = static_cast<u32>(m_heaps.size());
    stats.allocationCount = m_allocationCount;

    u64 largestFree = 0;
    for (const auto& heap : m_heaps) {
        stats.totalCapacity += heap.capacity;
        stats.totalAllocated += heap.allocated;
        for (const auto& block : heap.freeBlocks) {
            stats.totalFree += block.size;
            stats.freeBlockCount++;
            largestFree = std::max(largestFree, block.size);
        }
    }

    // Add unallocated tail space
    for (const auto& heap : m_heaps) {
        u64 usedPlusFree = heap.allocated;
        for (const auto& b : heap.freeBlocks) usedPlusFree += b.size;
        [[maybe_unused]] u64 tailFree = heap.capacity - usedPlusFree;
        // Tail free space is implicit — not tracked as a free block
    }

    stats.fragmentation = stats.totalFree > 0
        ? 1.0f - static_cast<f32>(largestFree) / static_cast<f32>(stats.totalFree)
        : 0.0f;

    return stats;
}

SubAllocation BufferSubAllocator::AllocateFromHeap(u32 heapIndex, u64 size, u32 alignment) {
    auto& heap = m_heaps[heapIndex];

    // First-fit from free list
    for (auto it = heap.freeBlocks.begin(); it != heap.freeBlocks.end(); ++it) {
        u64 alignedOffset = (it->offset + alignment - 1) & ~(static_cast<u64>(alignment) - 1);
        u64 padding = alignedOffset - it->offset;
        u64 totalNeeded = padding + size;

        if (totalNeeded <= it->size) {
            SubAllocation alloc;
            alloc.buffer = heap.buffer;
            alloc.offset = alignedOffset;
            alloc.size = size;
            alloc.heapIndex = heapIndex;
            alloc.valid = true;

            // Shrink or remove free block.
            // NOTE: alignment padding at the block's start is currently NOT
            // re-added as a free block, so those bytes are lost until Reset.
            if (it->size - totalNeeded > 0) {
                it->offset = alignedOffset + size;
                it->size -= totalNeeded;
                if (it->size == 0) {
                    heap.freeBlocks.erase(it);
                }
            } else {
                heap.freeBlocks.erase(it);
            }

            heap.allocated += size;
            m_allocationCount++;
            return alloc;
        }
    }

    // Try tail space (heap capacity - highest allocated offset)
    u64 highWater = 0;
    for (const auto& block : heap.freeBlocks) {
        highWater = std::max(highWater, block.offset + block.size);
    }
    // Calculate actual high water mark from allocated + free blocks
    u64 usedEnd = heap.allocated;
    for (const auto& block : heap.freeBlocks) {
        usedEnd = std::max(usedEnd, block.offset + block.size);
    }
    // Simple approach: track high water as allocated
    u64 tailOffset = heap.allocated;
    for (const auto& block : heap.freeBlocks) {
        if (block.offset + block.size > tailOffset) {
            tailOffset = block.offset + block.size;
        }
    }

    u64 alignedTail = (tailOffset + alignment - 1) & ~(static_cast<u64>(alignment) - 1);
    if (alignedTail + size <= heap.capacity) {
        SubAllocation alloc;
        alloc.buffer = heap.buffer;
        alloc.offset = alignedTail;
        alloc.size = size;
        alloc.heapIndex = heapIndex;
        alloc.valid = true;

        heap.allocated += size + (alignedTail - tailOffset);
        m_allocationCount++;
        return alloc;
    }

    return {}; // No space
}

u32 BufferSubAllocator::CreateHeap() {
    BufferDesc desc;
    desc.size = m_config.heapSize;
    desc.usage = m_config.usage;
    desc.memoryUsage = m_config.memoryUsage;
    desc.debugName = std::string(m_config.debugName ? m_config.debugName : "SubAllocHeap") +
                     "_" + std::to_string(m_heaps.size());

    Heap heap;
    heap.buffer = m_device->CreateBuffer(desc);
    heap.capacity = m_config.heapSize;
    heap.allocated = 0;

    u32 idx = static_cast<u32>(m_heaps.size());
    m_heaps.push_back(std::move(heap));

    NGE_LOG_DEBUG("Buffer suballocator: created heap {} ({} MB)",
                  idx, m_config.heapSize / (1024 * 1024));
    return idx;
}

void BufferSubAllocator::CoalesceHeap(Heap& heap) {
    if (heap.freeBlocks.size() < 2) return;

    // Sort by offset
    std::sort(heap.freeBlocks.begin(), heap.freeBlocks.end(),
        [](const FreeBlock& a, const FreeBlock& b) { return a.offset < b.offset; });

    // Merge adjacent blocks
    std::vector<FreeBlock> merged;
    merged.push_back(heap.freeBlocks[0]);

    for (u32 i = 1; i < static_cast<u32>(heap.freeBlocks.size()); ++i) {
        auto& last = merged.back();
        const auto& current = heap.freeBlocks[i];

        if (last.offset + last.size == current.offset) {
            last.size += current.size; // Merge
        } else {
            merged.push_back(current);
        }
    }

    heap.freeBlocks = std::move(merged);
}

} // namespace nge::rhi
