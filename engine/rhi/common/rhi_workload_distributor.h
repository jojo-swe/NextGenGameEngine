#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Workload Distributor ────────────────────────────────────────────
// Schedules GPU workloads across multiple hardware queues (graphics,
// compute, transfer) to maximize parallelism. Tracks queue occupancy,
// balances load, and manages cross-queue synchronization points.
//
// Use cases:
//   - Distribute async compute alongside graphics
//   - Schedule DMA transfers on dedicated transfer queue
//   - Balance compute workloads across multiple compute queues
//   - Track per-queue occupancy for load balancing
//   - Manage timeline semaphore sync between queues

enum class QueueType : u8 {
    Graphics,
    Compute,
    Transfer,
};

enum class WorkloadPriority : u8 {
    Low,
    Normal,
    High,
    Critical,
};

struct QueueInfo {
    u32       queueId;
    QueueType type;
    u32       familyIndex;
    float     priority;           // Vulkan queue priority (0..1)
    u64       estimatedLoadNs;    // Current estimated load in nanoseconds
    u32       pendingSubmissions;
    u32       totalSubmissions;
    std::string debugName;
};

struct WorkloadDesc {
    u32              workloadId;
    QueueType        preferredQueue;
    WorkloadPriority priority;
    u64              estimatedDurationNs;   // Estimated GPU time
    u64              memorySizeBytes;        // Data footprint
    bool             requiresGraphics;      // Needs graphics pipeline
    bool             requiresTransfer;      // Needs DMA capability
    std::vector<u32> dependencies;          // Workload IDs that must complete first
    std::string      debugName;
};

struct ScheduleResult {
    u32 workloadId;
    u32 assignedQueueId;
    u64 estimatedStartNs;
    u64 estimatedEndNs;
};

struct DistributorConfig {
    u32  maxQueues = 8;
    u32  maxWorkloadsPerFrame = 256;
    bool enableLoadBalancing = true;
    bool preferDedicatedTransfer = true;   // Use transfer queue for copies
    bool preferAsyncCompute = true;        // Use compute queue for compute
    float loadImbalanceThreshold = 0.3f;   // Rebalance if queue loads differ >30%
};

struct DistributorStats {
    u32 totalQueues;
    u32 totalWorkloadsScheduled;
    u32 workloadsOnGraphics;
    u32 workloadsOnCompute;
    u32 workloadsOnTransfer;
    u64 totalEstimatedGpuTimeNs;
    float loadBalanceRatio;              // min/max queue load ratio (1.0 = perfect)
    u32 crossQueueSyncs;
};

class WorkloadDistributor {
public:
    bool Init(const DistributorConfig& config = {});
    void Shutdown();

    // Register a hardware queue
    u32 RegisterQueue(QueueType type, u32 familyIndex, float priority = 1.0f,
                       const std::string& name = "");

    // Submit a workload for scheduling
    u32 SubmitWorkload(QueueType preferred, WorkloadPriority priority,
                        u64 estimatedDurationNs, const std::string& name = "");

    // Submit with full descriptor
    u32 SubmitWorkload(const WorkloadDesc& desc);

    // Schedule all pending workloads. Returns assignment list.
    std::vector<ScheduleResult> Schedule();

    // Mark a workload as completed on its queue
    void MarkCompleted(u32 workloadId);

    // Get queue info
    const QueueInfo* GetQueueInfo(u32 queueId) const;

    // Find the best queue for a workload type
    u32 FindBestQueue(QueueType type) const;

    // Get load on a queue (estimated nanoseconds of pending work)
    u64 GetQueueLoad(u32 queueId) const;

    // Get all queues of a specific type
    std::vector<u32> GetQueuesOfType(QueueType type) const;

    u32 GetQueueCount() const;
    u32 GetPendingWorkloadCount() const;

    void ClearPending();
    void Reset();

    DistributorStats GetStats() const;

private:
    u32 SelectQueue(const WorkloadDesc& desc) const;
    u32 FindLeastLoadedQueue(QueueType type) const;

    DistributorConfig m_config;
    std::vector<QueueInfo> m_queues;
    std::vector<WorkloadDesc> m_pending;
    std::unordered_map<u32, u32> m_workloadToQueue; // workloadId -> queueId

    u32 m_nextWorkloadId = 0;
    u32 m_workloadsOnGraphics = 0;
    u32 m_workloadsOnCompute = 0;
    u32 m_workloadsOnTransfer = 0;
    u32 m_crossQueueSyncs = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
