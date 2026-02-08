#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Memory Heap Inspector ───────────────────────────────────────────
// Per-heap allocation tracking with visualization data for the editor
// profiler panel. Tracks every GPU allocation's size, type, and age,
// providing fragmentation analysis and memory waterfall data.
//
// Integrates with VK_EXT_memory_budget for real-time budget monitoring.

enum class AllocationCategory : u8 {
    RenderTarget,
    DepthStencil,
    Texture,
    VertexBuffer,
    IndexBuffer,
    UniformBuffer,
    StorageBuffer,
    StagingBuffer,
    Transient,
    Readback,
    Indirect,
    QueryPool,
    Other,
    Count,
};

struct AllocationRecord {
    u64                handle;
    u64                offset;
    u64                size;
    u32                heapIndex;
    AllocationCategory category;
    std::string        debugName;
    u64                frameAllocated;
    bool               alive;
};

struct HeapInfo {
    u32  index;
    u64  totalSize;
    u64  usedSize;
    u64  budgetSize;       // From VK_EXT_memory_budget
    u64  budgetUsage;
    bool deviceLocal;
    bool hostVisible;
    bool hostCoherent;
    u32  allocationCount;
    f32  fragmentationPercent;
};

struct HeapVisualizationBlock {
    u64                offset;
    u64                size;
    AllocationCategory category;
    std::string        name;
    bool               free;
};

struct HeapInspectorConfig {
    bool trackAllocations = true;
    bool queryBudget = true;
    u32  maxTrackedAllocations = 65536;
};

struct HeapInspectorStats {
    u32 totalHeaps;
    u64 totalDeviceMemory;
    u64 totalUsedMemory;
    u64 totalBudget;
    u64 totalBudgetUsage;
    u32 totalAllocations;
    u32 largestAllocationBytes;
    std::unordered_map<u8, u64> bytesPerCategory;
};

class HeapInspector {
public:
    bool Init(IDevice* device, const HeapInspectorConfig& config = {});
    void Shutdown();

    // Record a new allocation
    void TrackAllocation(u64 handle, u64 offset, u64 size, u32 heapIndex,
                         AllocationCategory category, const std::string& debugName = "");

    // Record a deallocation
    void TrackFree(u64 handle);

    // Update budget info from VK_EXT_memory_budget (call once per frame)
    void UpdateBudget();

    // Get info for all heaps
    std::vector<HeapInfo> GetHeapInfos() const;

    // Get info for a specific heap
    HeapInfo GetHeapInfo(u32 heapIndex) const;

    // Get visualization blocks for a heap (for memory waterfall display)
    std::vector<HeapVisualizationBlock> GetVisualizationBlocks(u32 heapIndex) const;

    // Get allocations by category
    std::vector<AllocationRecord> GetAllocationsByCategory(AllocationCategory category) const;

    // Get the N largest allocations
    std::vector<AllocationRecord> GetLargestAllocations(u32 count) const;

    // Check if any heap is over budget
    bool IsOverBudget() const;

    // Get category name string
    static const char* CategoryName(AllocationCategory cat);

    // Per-frame update
    void BeginFrame(u64 frameNumber);

    HeapInspectorStats GetStats() const;

private:
    void RecalculateHeapStats();

    IDevice* m_device = nullptr;
    HeapInspectorConfig m_config;

    std::unordered_map<u64, AllocationRecord> m_allocations;
    std::vector<HeapInfo> m_heaps;
    u64 m_currentFrame = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
