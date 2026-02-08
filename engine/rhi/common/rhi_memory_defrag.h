#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <functional>

namespace nge::rhi {

// ─── GPU Memory Defragmenter ─────────────────────────────────────────────
// Compacts long-lived GPU allocations to reduce fragmentation.
// Operates on a per-heap basis, moving allocations to fill gaps.
//
// Strategy:
//   1. Scan heap for gaps between allocations
//   2. Find movable allocations that can fill gaps
//   3. Copy data via staging buffer (GPU→staging→GPU)
//   4. Update resource handles (rebind to new offsets)
//   5. Runs incrementally — a budget limits work per frame
//
// Not for transient resources (those use the transient pool with aliasing).

struct MemoryAllocation {
    u64  offset;
    u64  size;
    u64  alignment;
    u32  resourceId;
    bool movable;       // false for resources currently in use by GPU
    bool isTexture;     // true = texture, false = buffer
};

struct DefragStats {
    u64 totalHeapSize;
    u64 usedMemory;
    u64 freeMemory;
    u64 largestFreeBlock;
    u32 fragmentCount;       // Number of free gaps
    f32 fragmentation;       // 0.0 = perfect, 1.0 = fully fragmented
    u32 movesPending;
    u32 movesCompleted;
    u64 bytesMoved;
};

struct DefragConfig {
    u64 maxBytesPerFrame = 16 * 1024 * 1024; // 16 MB budget per frame
    u32 maxMovesPerFrame = 32;
    bool enableAutoDefrag = true;
    f32  fragmentationThreshold = 0.3f; // Trigger defrag above this
};

class MemoryDefragmenter {
public:
    using ResourceRelocateCallback = std::function<void(u32 resourceId, u64 oldOffset, u64 newOffset)>;

    bool Init(IDevice* device, const DefragConfig& config = {});
    void Shutdown();

    // Register a heap for defragmentation tracking
    u32 RegisterHeap(u64 heapHandle, u64 heapSize);

    // Track an allocation within a heap
    void TrackAllocation(u32 heapIndex, const MemoryAllocation& alloc);
    void UntrackAllocation(u32 heapIndex, u32 resourceId);

    // Mark a resource as movable/immovable (e.g., locked while GPU reads it)
    void SetMovable(u32 heapIndex, u32 resourceId, bool movable);

    // Run one incremental defrag step (call once per frame)
    // Returns true if there is more work to do
    bool DefragStep(ICommandList* cmd);

    // Set callback for when a resource is relocated
    void SetRelocateCallback(ResourceRelocateCallback callback);

    // Query
    DefragStats GetStats(u32 heapIndex) const;
    f32 GetFragmentation(u32 heapIndex) const;
    bool NeedsDefrag(u32 heapIndex) const;
    bool IsDefragInProgress() const { return m_inProgress; }

    const DefragConfig& GetConfig() const { return m_config; }

private:
    struct HeapState {
        u64 handle;
        u64 size;
        std::vector<MemoryAllocation> allocations;
        bool sorted = false;
    };

    struct PendingMove {
        u32 heapIndex;
        u32 resourceId;
        u64 srcOffset;
        u64 dstOffset;
        u64 size;
    };

    void SortAllocations(HeapState& heap);
    void PlanMoves(u32 heapIndex);
    bool ExecuteMove(ICommandList* cmd, const PendingMove& move);

    IDevice* m_device = nullptr;
    DefragConfig m_config;

    std::vector<HeapState> m_heaps;
    std::vector<PendingMove> m_pendingMoves;

    ResourceRelocateCallback m_relocateCallback;

    // Staging buffer for data movement
    BufferHandle m_stagingBuffer;

    bool m_inProgress = false;
    u64 m_bytesMovedThisFrame = 0;
    u32 m_movesThisFrame = 0;
};

} // namespace nge::rhi
