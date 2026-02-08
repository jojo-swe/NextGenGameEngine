#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <functional>

namespace nge::renderer {

// ─── GPU Work Graph Scheduler ────────────────────────────────────────────
// Resolves multi-queue dependencies and schedules work across graphics,
// async compute, and transfer queues. Produces an execution timeline
// with semaphore sync points.
//
// Input:  List of work items with queue affinity and dependencies
// Output: Ordered execution plan with semaphore signal/wait pairs

enum class WorkQueueType : u8 {
    Graphics,
    AsyncCompute,
    Transfer,
};

struct WorkItem {
    u32            id;
    std::string    name;
    WorkQueueType  queue;
    std::vector<u32> dependencies;  // IDs of items this depends on
    u64            estimatedCostNs; // Estimated GPU time (nanoseconds)
    std::function<void(rhi::ICommandList*)> execute;
};

struct SyncPoint {
    u32           signalItemId;
    u32           waitItemId;
    WorkQueueType signalQueue;
    WorkQueueType waitQueue;
    u64           timelineValue;
};

struct ScheduledWork {
    u32           itemId;
    WorkQueueType queue;
    u32           orderInQueue;     // Execution order within its queue
    u64           estimatedStartNs;
    u64           estimatedEndNs;
};

struct ExecutionPlan {
    std::vector<ScheduledWork> graphicsQueue;
    std::vector<ScheduledWork> computeQueue;
    std::vector<ScheduledWork> transferQueue;
    std::vector<SyncPoint>     syncPoints;
    u64                        totalGraphicsNs;
    u64                        totalComputeNs;
    u64                        totalTransferNs;
    u64                        criticalPathNs;  // Longest dependency chain
};

class WorkGraphScheduler {
public:
    void Reset();

    // Add work items
    u32 AddWork(const std::string& name, WorkQueueType queue, u64 estimatedCostNs,
                  std::function<void(rhi::ICommandList*)> execute = nullptr);

    // Add dependency: item depends on prerequisite
    void AddDependency(u32 itemId, u32 prerequisiteId);

    // Compute execution plan
    ExecutionPlan Schedule() const;

    // Execute the plan (submits to device queues with sync)
    void Execute(rhi::IDevice* device, const ExecutionPlan& plan);

    u32 GetWorkItemCount() const { return static_cast<u32>(m_items.size()); }

private:
    // Topological sort with queue-aware scheduling
    std::vector<u32> TopologicalSort() const;

    // Assign timeline semaphore values for cross-queue sync
    std::vector<SyncPoint> BuildSyncPoints(const std::vector<u32>& order) const;

    // Estimate timing (simple critical path analysis)
    void EstimateTiming(ExecutionPlan& plan) const;

    std::vector<WorkItem> m_items;
    u64 m_nextTimelineValue = 1;
};

} // namespace nge::renderer
