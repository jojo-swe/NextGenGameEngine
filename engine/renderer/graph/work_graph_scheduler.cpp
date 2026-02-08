#include "engine/renderer/graph/work_graph_scheduler.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <queue>
#include <unordered_set>

namespace nge::renderer {

void WorkGraphScheduler::Reset() {
    m_items.clear();
    m_nextTimelineValue = 1;
}

u32 WorkGraphScheduler::AddWork(const std::string& name, WorkQueueType queue,
                                  u64 estimatedCostNs,
                                  std::function<void(rhi::ICommandList*)> execute) {
    WorkItem item;
    item.id = static_cast<u32>(m_items.size());
    item.name = name;
    item.queue = queue;
    item.estimatedCostNs = estimatedCostNs;
    item.execute = std::move(execute);
    m_items.push_back(std::move(item));
    return item.id;
}

void WorkGraphScheduler::AddDependency(u32 itemId, u32 prerequisiteId) {
    if (itemId < m_items.size()) {
        m_items[itemId].dependencies.push_back(prerequisiteId);
    }
}

ExecutionPlan WorkGraphScheduler::Schedule() const {
    ExecutionPlan plan;
    plan.totalGraphicsNs = 0;
    plan.totalComputeNs = 0;
    plan.totalTransferNs = 0;
    plan.criticalPathNs = 0;

    if (m_items.empty()) return plan;

    // Topological sort respecting dependencies
    auto order = TopologicalSort();

    // Build sync points for cross-queue dependencies
    plan.syncPoints = BuildSyncPoints(order);

    // Assign to per-queue lists
    u32 gfxOrder = 0, compOrder = 0, xferOrder = 0;

    for (u32 itemId : order) {
        const auto& item = m_items[itemId];

        ScheduledWork work;
        work.itemId = itemId;
        work.queue = item.queue;
        work.estimatedStartNs = 0;
        work.estimatedEndNs = item.estimatedCostNs;

        switch (item.queue) {
            case WorkQueueType::Graphics:
                work.orderInQueue = gfxOrder++;
                plan.graphicsQueue.push_back(work);
                plan.totalGraphicsNs += item.estimatedCostNs;
                break;
            case WorkQueueType::AsyncCompute:
                work.orderInQueue = compOrder++;
                plan.computeQueue.push_back(work);
                plan.totalComputeNs += item.estimatedCostNs;
                break;
            case WorkQueueType::Transfer:
                work.orderInQueue = xferOrder++;
                plan.transferQueue.push_back(work);
                plan.totalTransferNs += item.estimatedCostNs;
                break;
        }
    }

    EstimateTiming(plan);
    return plan;
}

void WorkGraphScheduler::Execute(rhi::IDevice* device, const ExecutionPlan& plan) {
    // TODO: Submit work to actual device queues with timeline semaphore sync
    //
    // For each queue:
    //   1. Begin command buffer
    //   2. For each work item in queue order:
    //      a. Check if any sync point requires a wait before this item
    //      b. Execute the work item's command recording lambda
    //      c. Check if any sync point requires a signal after this item
    //   3. Submit command buffer with semaphore signal/wait info

    // Graphics queue
    for (const auto& work : plan.graphicsQueue) {
        const auto& item = m_items[work.itemId];
        if (item.execute) {
            // auto* cmd = device->GetCommandList();
            // item.execute(cmd);
        }
    }

    // Async compute queue
    for (const auto& work : plan.computeQueue) {
        const auto& item = m_items[work.itemId];
        if (item.execute) {
            // auto* cmd = device->GetAsyncComputeCommandList();
            // item.execute(cmd);
        }
    }

    // Transfer queue
    for (const auto& work : plan.transferQueue) {
        const auto& item = m_items[work.itemId];
        if (item.execute) {
            // auto* cmd = device->GetTransferCommandList();
            // item.execute(cmd);
        }
    }

    (void)device;
}

std::vector<u32> WorkGraphScheduler::TopologicalSort() const {
    u32 n = static_cast<u32>(m_items.size());
    std::vector<u32> inDegree(n, 0);
    std::vector<std::vector<u32>> dependents(n);

    for (const auto& item : m_items) {
        for (u32 dep : item.dependencies) {
            if (dep < n) {
                dependents[dep].push_back(item.id);
                inDegree[item.id]++;
            }
        }
    }

    // Priority queue: prefer items on the graphics queue first,
    // then by estimated cost (largest first for better overlap)
    auto cmp = [this](u32 a, u32 b) {
        if (m_items[a].queue != m_items[b].queue) {
            return static_cast<u32>(m_items[a].queue) > static_cast<u32>(m_items[b].queue);
        }
        return m_items[a].estimatedCostNs < m_items[b].estimatedCostNs;
    };
    std::priority_queue<u32, std::vector<u32>, decltype(cmp)> ready(cmp);

    for (u32 i = 0; i < n; ++i) {
        if (inDegree[i] == 0) ready.push(i);
    }

    std::vector<u32> order;
    order.reserve(n);

    while (!ready.empty()) {
        u32 current = ready.top();
        ready.pop();
        order.push_back(current);

        for (u32 dep : dependents[current]) {
            if (--inDegree[dep] == 0) {
                ready.push(dep);
            }
        }
    }

    if (order.size() != n) {
        NGE_LOG_ERROR("Work graph has circular dependencies!");
    }

    return order;
}

std::vector<SyncPoint> WorkGraphScheduler::BuildSyncPoints(const std::vector<u32>& order) const {
    std::vector<SyncPoint> syncs;
    u64 timelineValue = 1;

    // For each item, check if any dependency is on a different queue
    std::unordered_set<u64> addedSyncs; // Avoid duplicates

    for (u32 itemId : order) {
        const auto& item = m_items[itemId];
        for (u32 depId : item.dependencies) {
            if (depId >= m_items.size()) continue;
            const auto& dep = m_items[depId];

            if (dep.queue != item.queue) {
                u64 key = (static_cast<u64>(depId) << 32) | itemId;
                if (addedSyncs.count(key)) continue;
                addedSyncs.insert(key);

                SyncPoint sync;
                sync.signalItemId = depId;
                sync.waitItemId = itemId;
                sync.signalQueue = dep.queue;
                sync.waitQueue = item.queue;
                sync.timelineValue = timelineValue++;
                syncs.push_back(sync);
            }
        }
    }

    return syncs;
}

void WorkGraphScheduler::EstimateTiming(ExecutionPlan& plan) const {
    // Simple critical path: sum of longest dependency chain
    u32 n = static_cast<u32>(m_items.size());
    std::vector<u64> longestPath(n, 0);

    // Process in topological order
    auto order = TopologicalSort();
    for (u32 itemId : order) {
        const auto& item = m_items[itemId];
        u64 maxPredecessor = 0;
        for (u32 dep : item.dependencies) {
            if (dep < n) {
                maxPredecessor = std::max(maxPredecessor, longestPath[dep]);
            }
        }
        longestPath[itemId] = maxPredecessor + item.estimatedCostNs;
    }

    plan.criticalPathNs = *std::max_element(longestPath.begin(), longestPath.end());

    // Assign estimated start/end times per queue
    u64 gfxTime = 0, compTime = 0, xferTime = 0;
    for (auto& work : plan.graphicsQueue) {
        work.estimatedStartNs = gfxTime;
        work.estimatedEndNs = gfxTime + m_items[work.itemId].estimatedCostNs;
        gfxTime = work.estimatedEndNs;
    }
    for (auto& work : plan.computeQueue) {
        work.estimatedStartNs = compTime;
        work.estimatedEndNs = compTime + m_items[work.itemId].estimatedCostNs;
        compTime = work.estimatedEndNs;
    }
    for (auto& work : plan.transferQueue) {
        work.estimatedStartNs = xferTime;
        work.estimatedEndNs = xferTime + m_items[work.itemId].estimatedCostNs;
        xferTime = work.estimatedEndNs;
    }
}

} // namespace nge::renderer
