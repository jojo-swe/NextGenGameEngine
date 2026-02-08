#include "engine/rhi/common/rhi_queue_sync.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool QueueSyncManager::Init(IDevice* device, const QueueSyncConfig& config) {
    m_device = device;
    m_config = config;
    m_totalSyncs = 0;
    m_crossQueueWaits = 0;

    // Create timeline semaphores for each queue
    for (u32 i = 0; i < static_cast<u32>(QueueType::Count); ++i) {
        m_queues[i].currentValue = 0;
        m_queues[i].completedValue = 0;

        // TODO: Create VkSemaphore with VK_SEMAPHORE_TYPE_TIMELINE
        // VkSemaphoreTypeCreateInfo timelineCI{};
        // timelineCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        // timelineCI.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        // timelineCI.initialValue = 0;
        // VkSemaphoreCreateInfo ci{};
        // ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        // ci.pNext = &timelineCI;
        // vkCreateSemaphore(device, &ci, nullptr, &m_queues[i].timelineSemaphore);
        m_queues[i].timelineSemaphore = static_cast<u64>(i + 1); // Stub
    }

    NGE_LOG_INFO("Queue sync manager initialized: graphics + async_compute={} + transfer={}",
                 config.enableAsyncCompute, config.enableAsyncTransfer);
    return true;
}

void QueueSyncManager::Shutdown() {
    // TODO: vkDestroySemaphore for each queue
    for (u32 i = 0; i < static_cast<u32>(QueueType::Count); ++i) {
        m_queues[i].timelineSemaphore = 0;
        m_queues[i].currentValue = 0;
        m_queues[i].completedValue = 0;
    }
}

void QueueSyncManager::BeginFrame() {
    m_totalSyncs = 0;
    m_crossQueueWaits = 0;
}

u64 QueueSyncManager::Signal(QueueType queue) {
    std::lock_guard lock(m_mutex);
    u32 idx = static_cast<u32>(queue);
    m_queues[idx].currentValue++;
    m_totalSyncs++;

    // TODO: The actual signal happens at vkQueueSubmit time via
    // VkTimelineSemaphoreSubmitInfo::pSignalSemaphoreValues

    return m_queues[idx].currentValue;
}

QueueSyncPoint QueueSyncManager::CreateDependency(QueueType sourceQueue, QueueType targetQueue) {
    std::lock_guard lock(m_mutex);

    QueueSyncPoint point;
    point.queue = sourceQueue;
    point.timelineValue = m_queues[static_cast<u32>(sourceQueue)].currentValue;

    if (sourceQueue != targetQueue) {
        m_crossQueueWaits++;
    }

    return point;
}

QueueSubmitSync QueueSyncManager::BuildSubmitSync(QueueType queue,
                                                    const std::vector<QueueSyncPoint>& dependencies) {
    std::lock_guard lock(m_mutex);

    QueueSubmitSync sync;
    sync.waitPoints = dependencies;

    // Advance and signal
    u32 idx = static_cast<u32>(queue);
    m_queues[idx].currentValue++;
    sync.signalPoint.queue = queue;
    sync.signalPoint.timelineValue = m_queues[idx].currentValue;

    m_totalSyncs++;

    // TODO: Build VkSubmitInfo2 with timeline semaphore wait/signal:
    // VkSemaphoreSubmitInfo waitInfos[N], signalInfos[1];
    // For each dependency:
    //   waitInfos[i].semaphore = GetSemaphoreHandle(dep.queue);
    //   waitInfos[i].value = dep.timelineValue;
    //   waitInfos[i].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    // signalInfos[0].semaphore = GetSemaphoreHandle(queue);
    // signalInfos[0].value = sync.signalPoint.timelineValue;

    return sync;
}

void QueueSyncManager::CpuWait(QueueType queue, u64 timelineValue) {
    // TODO:
    // VkSemaphoreWaitInfo waitInfo{};
    // waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    // waitInfo.semaphoreCount = 1;
    // waitInfo.pSemaphores = &m_queues[idx].timelineSemaphore;
    // waitInfo.pValues = &timelineValue;
    // vkWaitSemaphores(device, &waitInfo, UINT64_MAX);

    u32 idx = static_cast<u32>(queue);
    m_queues[idx].completedValue = std::max(m_queues[idx].completedValue, timelineValue);
    (void)idx;
}

void QueueSyncManager::WaitIdle() {
    for (u32 i = 0; i < static_cast<u32>(QueueType::Count); ++i) {
        CpuWait(static_cast<QueueType>(i), m_queues[i].currentValue);
    }
}

u64 QueueSyncManager::GetCurrentValue(QueueType queue) const {
    std::lock_guard lock(m_mutex);
    return m_queues[static_cast<u32>(queue)].currentValue;
}

u64 QueueSyncManager::GetSemaphoreHandle(QueueType queue) const {
    std::lock_guard lock(m_mutex);
    return m_queues[static_cast<u32>(queue)].timelineSemaphore;
}

QueueSyncStats QueueSyncManager::GetStats() const {
    std::lock_guard lock(m_mutex);
    QueueSyncStats stats;
    stats.graphicsTimelineValue = m_queues[static_cast<u32>(QueueType::Graphics)].currentValue;
    stats.asyncComputeTimelineValue = m_queues[static_cast<u32>(QueueType::AsyncCompute)].currentValue;
    stats.transferTimelineValue = m_queues[static_cast<u32>(QueueType::Transfer)].currentValue;
    stats.totalSyncsThisFrame = m_totalSyncs;
    stats.crossQueueWaitsThisFrame = m_crossQueueWaits;
    return stats;
}

const char* QueueSyncManager::QueueTypeName(QueueType type) {
    switch (type) {
        case QueueType::Graphics:     return "Graphics";
        case QueueType::AsyncCompute: return "AsyncCompute";
        case QueueType::Transfer:     return "Transfer";
        default:                      return "Unknown";
    }
}

} // namespace nge::rhi
