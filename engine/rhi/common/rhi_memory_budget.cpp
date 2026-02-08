#include "engine/rhi/common/rhi_memory_budget.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool MemoryBudgetTracker::Init(IDevice* device, const MemoryBudgetConfig& config) {
    m_device = device;
    m_config = config;
    m_lastPollFrame = 0;

    // TODO: Query VkPhysicalDeviceMemoryProperties for heap count and flags
    // VkPhysicalDeviceMemoryProperties memProps;
    // vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    // For each heap: memProps.memoryHeaps[i].size, memProps.memoryHeaps[i].flags

    // Stub: create 2 heaps (device-local + host-visible)
    m_heaps.resize(2);

    m_heaps[0].heapIndex = 0;
    m_heaps[0].totalSize = 8ULL * 1024 * 1024 * 1024; // 8 GB device-local
    m_heaps[0].budget = m_heaps[0].totalSize;
    m_heaps[0].usage = 0;
    m_heaps[0].deviceLocal = true;
    m_heaps[0].hostVisible = false;
    m_heaps[0].usagePercent = 0;

    m_heaps[1].heapIndex = 1;
    m_heaps[1].totalSize = 16ULL * 1024 * 1024 * 1024; // 16 GB host-visible
    m_heaps[1].budget = m_heaps[1].totalSize;
    m_heaps[1].usage = 0;
    m_heaps[1].deviceLocal = false;
    m_heaps[1].hostVisible = true;
    m_heaps[1].usagePercent = 0;

    PollBudget();

    NGE_LOG_INFO("Memory budget tracker initialized: {} heaps", m_heaps.size());
    for (const auto& h : m_heaps) {
        NGE_LOG_INFO("  Heap {}: {} MB total, {} MB budget, {}{}",
                     h.heapIndex, h.totalSize / (1024 * 1024), h.budget / (1024 * 1024),
                     h.deviceLocal ? "device-local" : "",
                     h.hostVisible ? "host-visible" : "");
    }

    return true;
}

void MemoryBudgetTracker::Shutdown() {
    m_heaps.clear();
}

void MemoryBudgetTracker::Update(u64 frameNumber) {
    if (frameNumber - m_lastPollFrame >= m_config.pollIntervalFrames) {
        PollBudget();
        m_lastPollFrame = frameNumber;
    }
}

void MemoryBudgetTracker::PollBudget() {
    std::lock_guard lock(m_mutex);

    // TODO: Query VK_EXT_memory_budget
    // VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProps{};
    // budgetProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
    // VkPhysicalDeviceMemoryProperties2 memProps2{};
    // memProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    // memProps2.pNext = &budgetProps;
    // vkGetPhysicalDeviceMemoryProperties2(physicalDevice, &memProps2);
    // for each heap: budgetProps.heapBudget[i], budgetProps.heapUsage[i]

    // Update usage percentages
    for (auto& heap : m_heaps) {
        heap.usagePercent = heap.budget > 0
            ? static_cast<f32>(heap.usage) / static_cast<f32>(heap.budget) * 100.0f
            : 0.0f;
    }

    // Check for warnings
    auto stats = GetStats();
    if (stats.budgetCritical) {
        NGE_LOG_ERROR("VRAM budget CRITICAL: {:.1f}% device-local used", stats.deviceLocalUsagePercent);
    } else if (stats.budgetWarning) {
        NGE_LOG_WARN("VRAM budget warning: {:.1f}% device-local used", stats.deviceLocalUsagePercent);
    }
}

void MemoryBudgetTracker::TrackAllocation(u32 heapIndex, u64 sizeBytes) {
    std::lock_guard lock(m_mutex);
    if (heapIndex < m_heaps.size()) {
        m_heaps[heapIndex].usage += sizeBytes;
        m_heaps[heapIndex].usagePercent = m_heaps[heapIndex].budget > 0
            ? static_cast<f32>(m_heaps[heapIndex].usage) / static_cast<f32>(m_heaps[heapIndex].budget) * 100.0f
            : 0.0f;
    }
}

void MemoryBudgetTracker::TrackFree(u32 heapIndex, u64 sizeBytes) {
    std::lock_guard lock(m_mutex);
    if (heapIndex < m_heaps.size()) {
        m_heaps[heapIndex].usage = (m_heaps[heapIndex].usage >= sizeBytes)
            ? m_heaps[heapIndex].usage - sizeBytes : 0;
        m_heaps[heapIndex].usagePercent = m_heaps[heapIndex].budget > 0
            ? static_cast<f32>(m_heaps[heapIndex].usage) / static_cast<f32>(m_heaps[heapIndex].budget) * 100.0f
            : 0.0f;
    }
}

AllocationAdvice MemoryBudgetTracker::GetAdvice(u32 heapIndex, u64 requestedBytes) const {
    std::lock_guard lock(m_mutex);
    if (heapIndex >= m_heaps.size()) return AllocationAdvice::DenyAllocation;

    const auto& heap = m_heaps[heapIndex];
    u64 projectedUsage = heap.usage + requestedBytes;
    f32 projectedPercent = heap.budget > 0
        ? static_cast<f32>(projectedUsage) / static_cast<f32>(heap.budget)
        : 1.0f;

    if (projectedPercent >= 1.0f) return AllocationAdvice::DenyAllocation;
    if (projectedPercent >= m_config.criticalThreshold) return AllocationAdvice::EvictUnused;
    if (projectedPercent >= m_config.warningThreshold) return AllocationAdvice::Cautious;
    return AllocationAdvice::Ok;
}

AllocationAdvice MemoryBudgetTracker::GetDeviceLocalAdvice(u64 requestedBytes) const {
    std::lock_guard lock(m_mutex);
    for (const auto& heap : m_heaps) {
        if (heap.deviceLocal) {
            u64 projectedUsage = heap.usage + requestedBytes;
            f32 projectedPercent = heap.budget > 0
                ? static_cast<f32>(projectedUsage) / static_cast<f32>(heap.budget)
                : 1.0f;

            if (projectedPercent >= 1.0f) return AllocationAdvice::DenyAllocation;
            if (projectedPercent >= m_config.criticalThreshold) return AllocationAdvice::EvictUnused;
            if (projectedPercent >= m_config.warningThreshold) return AllocationAdvice::Cautious;
            return AllocationAdvice::Ok;
        }
    }
    return AllocationAdvice::DenyAllocation;
}

MemoryBudgetStats MemoryBudgetTracker::GetStats() const {
    std::lock_guard lock(m_mutex);
    MemoryBudgetStats stats{};
    stats.heapCount = static_cast<u32>(m_heaps.size());

    for (const auto& heap : m_heaps) {
        if (heap.deviceLocal) {
            stats.totalDeviceLocalBudget += heap.budget;
            stats.totalDeviceLocalUsage += heap.usage;
        }
        if (heap.hostVisible) {
            stats.totalHostVisibleBudget += heap.budget;
            stats.totalHostVisibleUsage += heap.usage;
        }
    }

    stats.deviceLocalUsagePercent = stats.totalDeviceLocalBudget > 0
        ? static_cast<f32>(stats.totalDeviceLocalUsage) / static_cast<f32>(stats.totalDeviceLocalBudget) * 100.0f
        : 0.0f;
    stats.hostVisibleUsagePercent = stats.totalHostVisibleBudget > 0
        ? static_cast<f32>(stats.totalHostVisibleUsage) / static_cast<f32>(stats.totalHostVisibleBudget) * 100.0f
        : 0.0f;

    stats.budgetWarning = (stats.deviceLocalUsagePercent / 100.0f) >= m_config.warningThreshold;
    stats.budgetCritical = (stats.deviceLocalUsagePercent / 100.0f) >= m_config.criticalThreshold;

    return stats;
}

u64 MemoryBudgetTracker::GetAvailableDeviceLocal() const {
    std::lock_guard lock(m_mutex);
    u64 available = 0;
    for (const auto& heap : m_heaps) {
        if (heap.deviceLocal && heap.budget > heap.usage) {
            available += heap.budget - heap.usage;
        }
    }
    return available;
}

u64 MemoryBudgetTracker::GetAvailableHostVisible() const {
    std::lock_guard lock(m_mutex);
    u64 available = 0;
    for (const auto& heap : m_heaps) {
        if (heap.hostVisible && heap.budget > heap.usage) {
            available += heap.budget - heap.usage;
        }
    }
    return available;
}

const char* MemoryBudgetTracker::AdviceName(AllocationAdvice advice) {
    switch (advice) {
        case AllocationAdvice::Ok:              return "Ok";
        case AllocationAdvice::Cautious:        return "Cautious";
        case AllocationAdvice::EvictUnused:     return "EvictUnused";
        case AllocationAdvice::DenyAllocation:  return "DenyAllocation";
    }
    return "Unknown";
}

} // namespace nge::rhi
