#include "engine/rhi/common/rhi_workload_distributor.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool WorkloadDistributor::Init(const DistributorConfig& config) {
    m_config = config;
    m_queues.reserve(config.maxQueues);
    m_pending.reserve(config.maxWorkloadsPerFrame);
    m_nextWorkloadId = 0;
    m_workloadsOnGraphics = 0;
    m_workloadsOnCompute = 0;
    m_workloadsOnTransfer = 0;
    m_crossQueueSyncs = 0;

    NGE_LOG_INFO("Workload distributor initialized: maxQueues={}, loadBalance={}, asyncCompute={}, dedicatedTransfer={}",
                 config.maxQueues, config.enableLoadBalancing, config.preferAsyncCompute, config.preferDedicatedTransfer);
    return true;
}

void WorkloadDistributor::Shutdown() {
    m_queues.clear();
    m_pending.clear();
    m_workloadToQueue.clear();
}

u32 WorkloadDistributor::RegisterQueue(QueueType type, u32 familyIndex, float priority,
                                         const std::string& name) {
    std::lock_guard lock(m_mutex);

    if (m_queues.size() >= m_config.maxQueues) {
        NGE_LOG_ERROR("Workload distributor: max queues reached ({})", m_config.maxQueues);
        return UINT32_MAX;
    }

    QueueInfo info;
    info.queueId = static_cast<u32>(m_queues.size());
    info.type = type;
    info.familyIndex = familyIndex;
    info.priority = priority;
    info.estimatedLoadNs = 0;
    info.pendingSubmissions = 0;
    info.totalSubmissions = 0;
    info.debugName = name;

    u32 id = info.queueId;
    m_queues.push_back(std::move(info));
    return id;
}

u32 WorkloadDistributor::SubmitWorkload(QueueType preferred, WorkloadPriority priority,
                                          u64 estimatedDurationNs, const std::string& name) {
    WorkloadDesc desc;
    desc.workloadId = 0; // Assigned below
    desc.preferredQueue = preferred;
    desc.priority = priority;
    desc.estimatedDurationNs = estimatedDurationNs;
    desc.memorySizeBytes = 0;
    desc.requiresGraphics = (preferred == QueueType::Graphics);
    desc.requiresTransfer = (preferred == QueueType::Transfer);
    desc.debugName = name;

    return SubmitWorkload(desc);
}

u32 WorkloadDistributor::SubmitWorkload(const WorkloadDesc& desc) {
    std::lock_guard lock(m_mutex);

    if (m_pending.size() >= m_config.maxWorkloadsPerFrame) {
        NGE_LOG_WARN("Workload distributor: max workloads per frame reached ({})", m_config.maxWorkloadsPerFrame);
        return UINT32_MAX;
    }

    WorkloadDesc workload = desc;
    workload.workloadId = m_nextWorkloadId++;

    m_pending.push_back(std::move(workload));
    return m_pending.back().workloadId;
}

std::vector<ScheduleResult> WorkloadDistributor::Schedule() {
    std::lock_guard lock(m_mutex);

    std::vector<ScheduleResult> results;

    // Sort pending by priority (Critical first)
    std::sort(m_pending.begin(), m_pending.end(),
              [](const WorkloadDesc& a, const WorkloadDesc& b) {
                  return static_cast<u8>(a.priority) > static_cast<u8>(b.priority);
              });

    for (const auto& workload : m_pending) {
        u32 queueId = SelectQueue(workload);

        if (queueId == UINT32_MAX) {
            NGE_LOG_WARN("No suitable queue for workload '{}', skipping", workload.debugName);
            continue;
        }

        auto& queue = m_queues[queueId];

        ScheduleResult result;
        result.workloadId = workload.workloadId;
        result.assignedQueueId = queueId;
        result.estimatedStartNs = queue.estimatedLoadNs;
        result.estimatedEndNs = queue.estimatedLoadNs + workload.estimatedDurationNs;

        queue.estimatedLoadNs += workload.estimatedDurationNs;
        queue.pendingSubmissions++;
        queue.totalSubmissions++;

        m_workloadToQueue[workload.workloadId] = queueId;

        switch (queue.type) {
            case QueueType::Graphics: m_workloadsOnGraphics++; break;
            case QueueType::Compute:  m_workloadsOnCompute++; break;
            case QueueType::Transfer: m_workloadsOnTransfer++; break;
        }

        // Track cross-queue syncs from dependencies
        for (u32 depId : workload.dependencies) {
            auto depIt = m_workloadToQueue.find(depId);
            if (depIt != m_workloadToQueue.end() && depIt->second != queueId) {
                m_crossQueueSyncs++;
            }
        }

        results.push_back(result);
    }

    m_pending.clear();
    return results;
}

void WorkloadDistributor::MarkCompleted(u32 workloadId) {
    std::lock_guard lock(m_mutex);

    auto it = m_workloadToQueue.find(workloadId);
    if (it == m_workloadToQueue.end()) return;

    u32 queueId = it->second;
    if (queueId < m_queues.size()) {
        auto& queue = m_queues[queueId];
        if (queue.pendingSubmissions > 0) queue.pendingSubmissions--;
    }

    m_workloadToQueue.erase(it);
}

const QueueInfo* WorkloadDistributor::GetQueueInfo(u32 queueId) const {
    std::lock_guard lock(m_mutex);

    if (queueId >= m_queues.size()) return nullptr;
    return &m_queues[queueId];
}

u32 WorkloadDistributor::FindBestQueue(QueueType type) const {
    std::lock_guard lock(m_mutex);
    return FindLeastLoadedQueue(type);
}

u64 WorkloadDistributor::GetQueueLoad(u32 queueId) const {
    std::lock_guard lock(m_mutex);

    if (queueId >= m_queues.size()) return 0;
    return m_queues[queueId].estimatedLoadNs;
}

std::vector<u32> WorkloadDistributor::GetQueuesOfType(QueueType type) const {
    std::lock_guard lock(m_mutex);

    std::vector<u32> result;
    for (const auto& q : m_queues) {
        if (q.type == type) result.push_back(q.queueId);
    }
    return result;
}

u32 WorkloadDistributor::GetQueueCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_queues.size());
}

u32 WorkloadDistributor::GetPendingWorkloadCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_pending.size());
}

void WorkloadDistributor::ClearPending() {
    std::lock_guard lock(m_mutex);
    m_pending.clear();
}

void WorkloadDistributor::Reset() {
    std::lock_guard lock(m_mutex);
    m_queues.clear();
    m_pending.clear();
    m_workloadToQueue.clear();
    m_nextWorkloadId = 0;
    m_workloadsOnGraphics = 0;
    m_workloadsOnCompute = 0;
    m_workloadsOnTransfer = 0;
    m_crossQueueSyncs = 0;
}

DistributorStats WorkloadDistributor::GetStats() const {
    std::lock_guard lock(m_mutex);

    DistributorStats stats{};
    stats.totalQueues = static_cast<u32>(m_queues.size());
    stats.totalWorkloadsScheduled = m_workloadsOnGraphics + m_workloadsOnCompute + m_workloadsOnTransfer;
    stats.workloadsOnGraphics = m_workloadsOnGraphics;
    stats.workloadsOnCompute = m_workloadsOnCompute;
    stats.workloadsOnTransfer = m_workloadsOnTransfer;
    stats.crossQueueSyncs = m_crossQueueSyncs;

    u64 totalGpuTime = 0;
    u64 minLoad = UINT64_MAX;
    u64 maxLoad = 0;

    for (const auto& q : m_queues) {
        totalGpuTime += q.estimatedLoadNs;
        if (q.estimatedLoadNs < minLoad) minLoad = q.estimatedLoadNs;
        if (q.estimatedLoadNs > maxLoad) maxLoad = q.estimatedLoadNs;
    }

    stats.totalEstimatedGpuTimeNs = totalGpuTime;
    stats.loadBalanceRatio = (maxLoad > 0) ? static_cast<float>(minLoad) / static_cast<float>(maxLoad) : 1.0f;

    return stats;
}

u32 WorkloadDistributor::SelectQueue(const WorkloadDesc& desc) const {
    // If requires graphics, must use graphics queue
    if (desc.requiresGraphics) {
        return FindLeastLoadedQueue(QueueType::Graphics);
    }

    // If requires transfer and we prefer dedicated transfer
    if (desc.requiresTransfer && m_config.preferDedicatedTransfer) {
        u32 transferQueue = FindLeastLoadedQueue(QueueType::Transfer);
        if (transferQueue != UINT32_MAX) return transferQueue;
        // Fallback to graphics
        return FindLeastLoadedQueue(QueueType::Graphics);
    }

    // Prefer async compute for compute workloads
    if (desc.preferredQueue == QueueType::Compute && m_config.preferAsyncCompute) {
        u32 computeQueue = FindLeastLoadedQueue(QueueType::Compute);
        if (computeQueue != UINT32_MAX) return computeQueue;
        // Fallback to graphics (can do compute)
        return FindLeastLoadedQueue(QueueType::Graphics);
    }

    // Load-balanced selection
    if (m_config.enableLoadBalancing) {
        u32 preferred = FindLeastLoadedQueue(desc.preferredQueue);
        if (preferred != UINT32_MAX) return preferred;
    }

    // Direct preferred queue
    for (const auto& q : m_queues) {
        if (q.type == desc.preferredQueue) return q.queueId;
    }

    // Any queue
    if (!m_queues.empty()) return 0;

    return UINT32_MAX;
}

u32 WorkloadDistributor::FindLeastLoadedQueue(QueueType type) const {
    u32 bestId = UINT32_MAX;
    u64 bestLoad = UINT64_MAX;

    for (const auto& q : m_queues) {
        if (q.type == type && q.estimatedLoadNs < bestLoad) {
            bestLoad = q.estimatedLoadNs;
            bestId = q.queueId;
        }
    }

    return bestId;
}

} // namespace nge::rhi
