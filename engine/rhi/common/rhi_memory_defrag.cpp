#include "engine/rhi/common/rhi_memory_defrag.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool MemoryDefragmenter::Init(IDevice* device, const DefragConfig& config) {
    m_device = device;
    m_config = config;

    // Staging buffer for GPU→staging→GPU copies during defrag
    {
        BufferDesc desc;
        desc.size = config.maxBytesPerFrame;
        desc.usage = BufferUsage::TransferSrc | BufferUsage::TransferDst;
        desc.memoryUsage = MemoryUsage::GPU_Only;
        desc.debugName = "DefragStaging";
        m_stagingBuffer = device->CreateBuffer(desc);
    }

    NGE_LOG_INFO("Memory defragmenter initialized: {} MB/frame budget, {} moves/frame",
                 config.maxBytesPerFrame / (1024 * 1024), config.maxMovesPerFrame);
    return true;
}

void MemoryDefragmenter::Shutdown() {
    if (!m_device) return;
    if (m_stagingBuffer.IsValid()) {
        m_device->DestroyBuffer(m_stagingBuffer);
        m_stagingBuffer = {};
    }
    m_heaps.clear();
    m_pendingMoves.clear();
}

u32 MemoryDefragmenter::RegisterHeap(u64 heapHandle, u64 heapSize) {
    HeapState heap;
    heap.handle = heapHandle;
    heap.size = heapSize;
    u32 index = static_cast<u32>(m_heaps.size());
    m_heaps.push_back(std::move(heap));
    return index;
}

void MemoryDefragmenter::TrackAllocation(u32 heapIndex, const MemoryAllocation& alloc) {
    if (heapIndex >= m_heaps.size()) return;
    m_heaps[heapIndex].allocations.push_back(alloc);
    m_heaps[heapIndex].sorted = false;
}

void MemoryDefragmenter::UntrackAllocation(u32 heapIndex, u32 resourceId) {
    if (heapIndex >= m_heaps.size()) return;
    auto& allocs = m_heaps[heapIndex].allocations;
    allocs.erase(std::remove_if(allocs.begin(), allocs.end(),
        [resourceId](const MemoryAllocation& a) { return a.resourceId == resourceId; }),
        allocs.end());
}

void MemoryDefragmenter::SetMovable(u32 heapIndex, u32 resourceId, bool movable) {
    if (heapIndex >= m_heaps.size()) return;
    for (auto& alloc : m_heaps[heapIndex].allocations) {
        if (alloc.resourceId == resourceId) {
            alloc.movable = movable;
            break;
        }
    }
}

void MemoryDefragmenter::SetRelocateCallback(ResourceRelocateCallback callback) {
    m_relocateCallback = std::move(callback);
}

bool MemoryDefragmenter::DefragStep(ICommandList* cmd) {
    m_bytesMovedThisFrame = 0;
    m_movesThisFrame = 0;

    // Plan moves if none pending
    if (m_pendingMoves.empty()) {
        for (u32 i = 0; i < static_cast<u32>(m_heaps.size()); ++i) {
            if (NeedsDefrag(i)) {
                PlanMoves(i);
            }
        }
    }

    if (m_pendingMoves.empty()) {
        m_inProgress = false;
        return false;
    }

    m_inProgress = true;

    // Execute moves within budget
    while (!m_pendingMoves.empty() &&
           m_bytesMovedThisFrame < m_config.maxBytesPerFrame &&
           m_movesThisFrame < m_config.maxMovesPerFrame) {

        auto move = m_pendingMoves.back();
        m_pendingMoves.pop_back();

        if (ExecuteMove(cmd, move)) {
            m_movesThisFrame++;
            m_bytesMovedThisFrame += move.size;
        }
    }

    return !m_pendingMoves.empty();
}

void MemoryDefragmenter::SortAllocations(HeapState& heap) {
    std::sort(heap.allocations.begin(), heap.allocations.end(),
              [](const MemoryAllocation& a, const MemoryAllocation& b) {
                  return a.offset < b.offset;
              });
    heap.sorted = true;
}

void MemoryDefragmenter::PlanMoves(u32 heapIndex) {
    auto& heap = m_heaps[heapIndex];
    if (!heap.sorted) SortAllocations(heap);

    // Find gaps and plan moves to fill them
    u64 expectedOffset = 0;

    for (const auto& alloc : heap.allocations) {
        if (alloc.offset > expectedOffset && alloc.movable) {
            // There's a gap — this allocation can be moved left
            u64 alignedOffset = (expectedOffset + alloc.alignment - 1) & ~(alloc.alignment - 1);

            if (alignedOffset < alloc.offset) {
                PendingMove move;
                move.heapIndex = heapIndex;
                move.resourceId = alloc.resourceId;
                move.srcOffset = alloc.offset;
                move.dstOffset = alignedOffset;
                move.size = alloc.size;
                m_pendingMoves.push_back(move);
            }
        }
        expectedOffset = alloc.offset + alloc.size;
    }
}

bool MemoryDefragmenter::ExecuteMove(ICommandList* cmd, const PendingMove& move) {
    if (move.size > m_config.maxBytesPerFrame) {
        NGE_LOG_WARN("Defrag: allocation too large to move in one frame ({} MB)", move.size / (1024 * 1024));
        return false;
    }

    // GPU copy: src offset → staging → dst offset
    // Using the same heap, we need a staging intermediary to avoid overlap.

    // Step 1: Copy from source to staging buffer
    // cmd->CopyBufferRegion(heapBuffer, move.srcOffset, m_stagingBuffer, 0, move.size);
    // cmd->BufferBarrier(m_stagingBuffer, ResourceState::TransferDst, ResourceState::TransferSrc);

    // Step 2: Copy from staging to destination
    // cmd->CopyBufferRegion(m_stagingBuffer, 0, heapBuffer, move.dstOffset, move.size);
    // cmd->BufferBarrier(heapBuffer, ResourceState::TransferDst, ResourceState::ShaderRead);

    (void)cmd;

    // Update the allocation record
    auto& heap = m_heaps[move.heapIndex];
    for (auto& alloc : heap.allocations) {
        if (alloc.resourceId == move.resourceId) {
            alloc.offset = move.dstOffset;
            break;
        }
    }
    heap.sorted = false;

    // Notify the resource system to rebind
    if (m_relocateCallback) {
        m_relocateCallback(move.resourceId, move.srcOffset, move.dstOffset);
    }

    return true;
}

DefragStats MemoryDefragmenter::GetStats(u32 heapIndex) const {
    DefragStats stats{};
    if (heapIndex >= m_heaps.size()) return stats;

    const auto& heap = m_heaps[heapIndex];
    stats.totalHeapSize = heap.size;

    u64 usedMem = 0;
    u64 largestGap = 0;
    u64 prevEnd = 0;
    u32 gapCount = 0;

    // Need sorted order for gap analysis
    auto sorted = heap.allocations;
    std::sort(sorted.begin(), sorted.end(),
              [](const MemoryAllocation& a, const MemoryAllocation& b) {
                  return a.offset < b.offset;
              });

    for (const auto& alloc : sorted) {
        u64 gap = alloc.offset - prevEnd;
        if (gap > 0) {
            gapCount++;
            largestGap = std::max(largestGap, gap);
        }
        usedMem += alloc.size;
        prevEnd = alloc.offset + alloc.size;
    }

    // Gap at end of heap
    if (prevEnd < heap.size) {
        u64 endGap = heap.size - prevEnd;
        largestGap = std::max(largestGap, endGap);
    }

    stats.usedMemory = usedMem;
    stats.freeMemory = heap.size - usedMem;
    stats.largestFreeBlock = largestGap;
    stats.fragmentCount = gapCount;
    stats.movesPending = static_cast<u32>(m_pendingMoves.size());
    stats.movesCompleted = m_movesThisFrame;
    stats.bytesMoved = m_bytesMovedThisFrame;

    // Fragmentation metric: 1 - (largest free block / total free)
    if (stats.freeMemory > 0) {
        stats.fragmentation = 1.0f - static_cast<f32>(largestGap) / static_cast<f32>(stats.freeMemory);
    }

    return stats;
}

f32 MemoryDefragmenter::GetFragmentation(u32 heapIndex) const {
    return GetStats(heapIndex).fragmentation;
}

bool MemoryDefragmenter::NeedsDefrag(u32 heapIndex) const {
    if (!m_config.enableAutoDefrag) return false;
    return GetFragmentation(heapIndex) > m_config.fragmentationThreshold;
}

} // namespace nge::rhi
