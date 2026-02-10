#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <mutex>
#include <unordered_map>

namespace nge::rhi {

// ─── GPU Multi-Queue Synchronization Manager ─────────────────────────────
// Orchestrates timeline semaphore synchronization across multiple GPU
// queues (graphics, compute, transfer). Tracks per-queue timeline values
// and inserts wait/signal operations to maintain correct execution order.
//
// Vulkan 1.2 timeline semaphores allow:
//   - Signal a value on one queue
//   - Wait on that value from another queue
//   - No need for binary semaphores or fences for cross-queue sync

// QueueType is defined in rhi_types.h (included via rhi_device.h)

struct QueueSyncPoint {
    QueueType queue;
    u64       timelineValue;
};

struct QueueSubmitSync {
    std::vector<QueueSyncPoint> waitPoints;   // Wait for these before executing
    QueueSyncPoint              signalPoint;  // Signal this after executing
};

struct QueueSyncConfig {
    bool enableAsyncCompute = true;
    bool enableAsyncTransfer = true;
};

struct QueueSyncStats {
    u64 graphicsTimelineValue;
    u64 asyncComputeTimelineValue;
    u64 transferTimelineValue;
    u32 totalSyncsThisFrame;
    u32 crossQueueWaitsThisFrame;
};

class QueueSyncManager {
public:
    bool Init(IDevice* device, const QueueSyncConfig& config = {});
    void Shutdown();

    // Per-frame lifecycle
    void BeginFrame();

    // Advance timeline on a queue (returns the new timeline value)
    u64 Signal(QueueType queue);

    // Create a sync dependency: targetQueue waits for sourceQueue's current value
    QueueSyncPoint CreateDependency(QueueType sourceQueue, QueueType targetQueue);

    // Build submission sync info for a queue submit
    QueueSubmitSync BuildSubmitSync(QueueType queue, const std::vector<QueueSyncPoint>& dependencies);

    // Wait for a specific timeline value on CPU (blocking)
    void CpuWait(QueueType queue, u64 timelineValue);

    // Wait for all queues to reach their current timeline values
    void WaitIdle();

    // Query current timeline value for a queue
    u64 GetCurrentValue(QueueType queue) const;

    // Get the semaphore handle for a queue (for Vulkan submit)
    u64 GetSemaphoreHandle(QueueType queue) const;

    QueueSyncStats GetStats() const;

    static const char* QueueTypeName(QueueType type);

private:
    struct QueueState {
        u64 timelineSemaphore = 0;  // VkSemaphore handle (timeline type)
        u64 currentValue = 0;
        u64 completedValue = 0;     // Last value confirmed completed by GPU
    };

    IDevice* m_device = nullptr;
    QueueSyncConfig m_config;
    QueueState m_queues[static_cast<u32>(QueueType::Count)];

    u32 m_totalSyncs = 0;
    u32 m_crossQueueWaits = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
