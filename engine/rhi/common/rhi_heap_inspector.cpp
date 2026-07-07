#include "engine/rhi/common/rhi_heap_inspector.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <numeric>

namespace nge::rhi {

bool HeapInspector::Init(IDevice* device, const HeapInspectorConfig& config) {
    m_device = device;
    m_config = config;
    m_currentFrame = 0;

    // TODO: Query VkPhysicalDeviceMemoryProperties for heap count and sizes
    // VkPhysicalDeviceMemoryProperties memProps;
    // vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    // for (u32 i = 0; i < memProps.memoryHeapCount; ++i) {
    //     HeapInfo heap;
    //     heap.index = i;
    //     heap.totalSize = memProps.memoryHeaps[i].size;
    //     heap.deviceLocal = (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
    //     m_heaps.push_back(heap);
    // }

    // Stub: create two heaps (device-local + host-visible)
    InspectorHeapInfo deviceHeap;
    deviceHeap.index = 0;
    deviceHeap.totalSize = 8ULL * 1024 * 1024 * 1024; // 8 GB
    deviceHeap.usedSize = 0;
    deviceHeap.budgetSize = 0;
    deviceHeap.budgetUsage = 0;
    deviceHeap.deviceLocal = true;
    deviceHeap.hostVisible = false;
    deviceHeap.hostCoherent = false;
    deviceHeap.allocationCount = 0;
    deviceHeap.fragmentationPercent = 0.0f;
    m_heaps.push_back(deviceHeap);

    InspectorHeapInfo hostHeap;
    hostHeap.index = 1;
    hostHeap.totalSize = 16ULL * 1024 * 1024 * 1024; // 16 GB
    hostHeap.usedSize = 0;
    hostHeap.budgetSize = 0;
    hostHeap.budgetUsage = 0;
    hostHeap.deviceLocal = false;
    hostHeap.hostVisible = true;
    hostHeap.hostCoherent = true;
    hostHeap.allocationCount = 0;
    hostHeap.fragmentationPercent = 0.0f;
    m_heaps.push_back(hostHeap);

    NGE_LOG_INFO("Heap inspector initialized: {} heaps, tracking={}", m_heaps.size(), config.trackAllocations);
    return true;
}

void HeapInspector::Shutdown() {
    if (!m_allocations.empty()) {
        NGE_LOG_WARN("Heap inspector shutdown with {} live allocations", m_allocations.size());
        for (const auto& [handle, alloc] : m_allocations) {
            if (alloc.alive) {
                NGE_LOG_WARN("  Leaked: '{}' {}KB heap={}", alloc.debugName,
                             alloc.size / 1024, alloc.heapIndex);
            }
        }
    }
    m_allocations.clear();
    m_heaps.clear();
}

void HeapInspector::TrackAllocation(u64 handle, u64 offset, u64 size, u32 heapIndex,
                                     AllocationCategory category, const std::string& debugName) {
    if (!m_config.trackAllocations) return;
    std::lock_guard lock(m_mutex);

    if (m_allocations.size() >= m_config.maxTrackedAllocations) {
        NGE_LOG_WARN("Heap inspector: max tracked allocations reached ({})", m_config.maxTrackedAllocations);
        return;
    }

    AllocationRecord rec;
    rec.handle = handle;
    rec.offset = offset;
    rec.size = size;
    rec.heapIndex = heapIndex;
    rec.category = category;
    rec.debugName = debugName;
    rec.frameAllocated = m_currentFrame;
    rec.alive = true;

    m_allocations[handle] = std::move(rec);
    RecalculateHeapStats();
}

void HeapInspector::TrackFree(u64 handle) {
    if (!m_config.trackAllocations) return;
    std::lock_guard lock(m_mutex);

    auto it = m_allocations.find(handle);
    if (it == m_allocations.end()) {
        NGE_LOG_WARN("Heap inspector: freeing untracked handle {:#x}", handle);
        return;
    }

    m_allocations.erase(it);
    RecalculateHeapStats();
}

void HeapInspector::UpdateBudget() {
    if (!m_config.queryBudget) return;
    std::lock_guard lock(m_mutex);

    // TODO: Query VK_EXT_memory_budget
    // VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProps{};
    // budgetProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
    // VkPhysicalDeviceMemoryProperties2 memProps2{};
    // memProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    // memProps2.pNext = &budgetProps;
    // vkGetPhysicalDeviceMemoryProperties2(physicalDevice, &memProps2);
    // for (u32 i = 0; i < heapCount; ++i) {
    //     m_heaps[i].budgetSize = budgetProps.heapBudget[i];
    //     m_heaps[i].budgetUsage = budgetProps.heapUsage[i];
    // }
}

std::vector<InspectorHeapInfo> HeapInspector::GetHeapInfos() const {
    std::lock_guard lock(m_mutex);
    return m_heaps;
}

InspectorHeapInfo HeapInspector::GetHeapInfo(u32 heapIndex) const {
    std::lock_guard lock(m_mutex);
    if (heapIndex < m_heaps.size()) return m_heaps[heapIndex];
    return {};
}

std::vector<HeapVisualizationBlock> HeapInspector::GetVisualizationBlocks(u32 heapIndex) const {
    std::lock_guard lock(m_mutex);
    std::vector<HeapVisualizationBlock> blocks;

    // Collect allocations for this heap, sorted by offset
    std::vector<const AllocationRecord*> heapAllocs;
    for (const auto& [handle, alloc] : m_allocations) {
        if (alloc.heapIndex == heapIndex && alloc.alive) {
            heapAllocs.push_back(&alloc);
        }
    }

    std::sort(heapAllocs.begin(), heapAllocs.end(),
        [](const AllocationRecord* a, const AllocationRecord* b) {
            return a->offset < b->offset;
        });

    // Build blocks with free gaps
    u64 currentOffset = 0;
    for (const auto* alloc : heapAllocs) {
        if (alloc->offset > currentOffset) {
            // Free gap
            HeapVisualizationBlock freeBlock;
            freeBlock.offset = currentOffset;
            freeBlock.size = alloc->offset - currentOffset;
            freeBlock.category = AllocationCategory::Other;
            freeBlock.name = "free";
            freeBlock.free = true;
            blocks.push_back(freeBlock);
        }

        HeapVisualizationBlock block;
        block.offset = alloc->offset;
        block.size = alloc->size;
        block.category = alloc->category;
        block.name = alloc->debugName;
        block.free = false;
        blocks.push_back(block);

        currentOffset = alloc->offset + alloc->size;
    }

    return blocks;
}

std::vector<AllocationRecord> HeapInspector::GetAllocationsByCategory(AllocationCategory category) const {
    std::lock_guard lock(m_mutex);
    std::vector<AllocationRecord> result;
    for (const auto& [handle, alloc] : m_allocations) {
        if (alloc.category == category && alloc.alive) {
            result.push_back(alloc);
        }
    }
    return result;
}

std::vector<AllocationRecord> HeapInspector::GetLargestAllocations(u32 count) const {
    std::lock_guard lock(m_mutex);
    std::vector<AllocationRecord> all;
    for (const auto& [handle, alloc] : m_allocations) {
        if (alloc.alive) all.push_back(alloc);
    }

    std::sort(all.begin(), all.end(),
        [](const AllocationRecord& a, const AllocationRecord& b) { return a.size > b.size; });

    if (all.size() > count) all.resize(count);
    return all;
}

bool HeapInspector::IsOverBudget() const {
    std::lock_guard lock(m_mutex);
    for (const auto& heap : m_heaps) {
        if (heap.budgetSize > 0 && heap.budgetUsage > heap.budgetSize) return true;
    }
    return false;
}

const char* HeapInspector::CategoryName(AllocationCategory cat) {
    switch (cat) {
        case AllocationCategory::RenderTarget:   return "RenderTarget";
        case AllocationCategory::DepthStencil:   return "DepthStencil";
        case AllocationCategory::Texture:        return "Texture";
        case AllocationCategory::VertexBuffer:   return "VertexBuffer";
        case AllocationCategory::IndexBuffer:    return "IndexBuffer";
        case AllocationCategory::UniformBuffer:  return "UniformBuffer";
        case AllocationCategory::StorageBuffer:  return "StorageBuffer";
        case AllocationCategory::StagingBuffer:  return "StagingBuffer";
        case AllocationCategory::Transient:      return "Transient";
        case AllocationCategory::Readback:       return "Readback";
        case AllocationCategory::Indirect:       return "Indirect";
        case AllocationCategory::QueryPool:      return "QueryPool";
        case AllocationCategory::Other:          return "Other";
        default:                                  return "Unknown";
    }
}

void HeapInspector::BeginFrame(u64 frameNumber) {
    m_currentFrame = frameNumber;
    UpdateBudget();
}

HeapInspectorStats HeapInspector::GetStats() const {
    std::lock_guard lock(m_mutex);
    HeapInspectorStats stats{};
    stats.totalHeaps = static_cast<u32>(m_heaps.size());

    for (const auto& heap : m_heaps) {
        stats.totalDeviceMemory += heap.totalSize;
        stats.totalUsedMemory += heap.usedSize;
        stats.totalBudget += heap.budgetSize;
        stats.totalBudgetUsage += heap.budgetUsage;
    }

    stats.totalAllocations = 0;
    stats.largestAllocationBytes = 0;
    for (const auto& [handle, alloc] : m_allocations) {
        if (alloc.alive) {
            stats.totalAllocations++;
            if (alloc.size > stats.largestAllocationBytes) {
                stats.largestAllocationBytes = static_cast<u32>(std::min(alloc.size, u64(UINT32_MAX)));
            }
            stats.bytesPerCategory[static_cast<u8>(alloc.category)] += alloc.size;
        }
    }

    return stats;
}

void HeapInspector::RecalculateHeapStats() {
    for (auto& heap : m_heaps) {
        heap.usedSize = 0;
        heap.allocationCount = 0;
    }

    for (const auto& [handle, alloc] : m_allocations) {
        if (!alloc.alive) continue;
        if (alloc.heapIndex < m_heaps.size()) {
            m_heaps[alloc.heapIndex].usedSize += alloc.size;
            m_heaps[alloc.heapIndex].allocationCount++;
        }
    }

    // Calculate fragmentation per heap
    for (auto& heap : m_heaps) {
        if (heap.allocationCount <= 1 || heap.usedSize == 0) {
            heap.fragmentationPercent = 0.0f;
            continue;
        }

        // Collect allocations for this heap sorted by offset
        std::vector<std::pair<u64, u64>> ranges;
        for (const auto& [handle, alloc] : m_allocations) {
            if (alloc.heapIndex == heap.index && alloc.alive) {
                ranges.emplace_back(alloc.offset, alloc.size);
            }
        }
        std::sort(ranges.begin(), ranges.end());

        u64 totalGaps = 0;
        for (size_t i = 1; i < ranges.size(); ++i) {
            u64 prevEnd = ranges[i - 1].first + ranges[i - 1].second;
            if (ranges[i].first > prevEnd) {
                totalGaps += ranges[i].first - prevEnd;
            }
        }

        u64 usedRange = 0;
        if (!ranges.empty()) {
            usedRange = ranges.back().first + ranges.back().second - ranges.front().first;
        }

        heap.fragmentationPercent = usedRange > 0
            ? static_cast<f32>(totalGaps) / static_cast<f32>(usedRange) * 100.0f
            : 0.0f;
    }
}

} // namespace nge::rhi
