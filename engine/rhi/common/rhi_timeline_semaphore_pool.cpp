#include "engine/rhi/common/rhi_timeline_semaphore_pool.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool TimelineSemaphorePool::Init(IDevice* device, const TimelineSemaphorePoolConfig& config) {
    m_device = device;
    m_config = config;
    m_growthEvents = 0;

    GrowPool(config.initialPoolSize);

    NGE_LOG_INFO("Timeline semaphore pool initialized: initial={}, max={}, growth={}",
                 config.initialPoolSize, config.maxPoolSize, config.allowGrowth);
    return true;
}

void TimelineSemaphorePool::Shutdown() {
    for (auto& sem : m_semaphores) {
        // TODO: vkDestroySemaphore(device, sem.handle, nullptr);
        sem.handle = 0;
    }
    m_semaphores.clear();
    while (!m_available.empty()) m_available.pop();
}

u32 TimelineSemaphorePool::Acquire(const std::string& debugName) {
    std::lock_guard lock(m_mutex);

    if (m_available.empty()) {
        if (!m_config.allowGrowth || m_semaphores.size() >= m_config.maxPoolSize) {
            NGE_LOG_ERROR("Timeline semaphore pool exhausted: max={}", m_config.maxPoolSize);
            return UINT32_MAX;
        }
        u32 growCount = std::min(16u, m_config.maxPoolSize - static_cast<u32>(m_semaphores.size()));
        GrowPool(growCount);
        m_growthEvents++;
        NGE_LOG_WARN("Timeline semaphore pool grew by {}: now {} total", growCount, m_semaphores.size());
    }

    u32 id = m_available.front();
    m_available.pop();

    m_semaphores[id].inUse = true;
    m_semaphores[id].debugName = debugName;
    m_semaphores[id].currentValue = 0;

    return id;
}

void TimelineSemaphorePool::Release(u32 semaphoreId) {
    std::lock_guard lock(m_mutex);
    if (semaphoreId >= m_semaphores.size()) return;
    if (!m_semaphores[semaphoreId].inUse) return;

    m_semaphores[semaphoreId].inUse = false;
    m_semaphores[semaphoreId].debugName.clear();
    m_semaphores[semaphoreId].currentValue = 0;
    m_available.push(semaphoreId);
}

u64 TimelineSemaphorePool::GetHandle(u32 semaphoreId) const {
    std::lock_guard lock(m_mutex);
    if (semaphoreId >= m_semaphores.size()) return 0;
    return m_semaphores[semaphoreId].handle;
}

u64 TimelineSemaphorePool::GetNextSignalValue(u32 semaphoreId) {
    std::lock_guard lock(m_mutex);
    if (semaphoreId >= m_semaphores.size()) return 0;
    return ++m_semaphores[semaphoreId].currentValue;
}

u64 TimelineSemaphorePool::GetCurrentValue(u32 semaphoreId) const {
    std::lock_guard lock(m_mutex);
    if (semaphoreId >= m_semaphores.size()) return 0;
    return m_semaphores[semaphoreId].currentValue;
}

bool TimelineSemaphorePool::CpuWait(u32 semaphoreId, u64 value, u64 timeoutNs) {
    std::lock_guard lock(m_mutex);
    if (semaphoreId >= m_semaphores.size()) return false;

    // TODO: VkSemaphoreWaitInfo waitInfo{};
    // waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    // waitInfo.semaphoreCount = 1;
    // waitInfo.pSemaphores = &m_semaphores[semaphoreId].handle;
    // waitInfo.pValues = &value;
    // VkResult result = vkWaitSemaphores(device, &waitInfo, timeoutNs);
    // return result == VK_SUCCESS;

    (void)value;
    (void)timeoutNs;
    return true; // Stub
}

void TimelineSemaphorePool::CpuSignal(u32 semaphoreId, u64 value) {
    std::lock_guard lock(m_mutex);
    if (semaphoreId >= m_semaphores.size()) return;

    // TODO: VkSemaphoreSignalInfo signalInfo{};
    // signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
    // signalInfo.semaphore = m_semaphores[semaphoreId].handle;
    // signalInfo.value = value;
    // vkSignalSemaphore(device, &signalInfo);

    m_semaphores[semaphoreId].currentValue = value;
}

bool TimelineSemaphorePool::HasReached(u32 semaphoreId, u64 value) const {
    std::lock_guard lock(m_mutex);
    if (semaphoreId >= m_semaphores.size()) return false;

    // TODO: u64 counterValue;
    // vkGetSemaphoreCounterValue(device, m_semaphores[semaphoreId].handle, &counterValue);
    // return counterValue >= value;

    return m_semaphores[semaphoreId].currentValue >= value;
}

void TimelineSemaphorePool::ResetAll() {
    std::lock_guard lock(m_mutex);
    while (!m_available.empty()) m_available.pop();

    for (u32 i = 0; i < m_semaphores.size(); ++i) {
        m_semaphores[i].inUse = false;
        m_semaphores[i].currentValue = 0;
        m_semaphores[i].debugName.clear();
        m_available.push(i);
    }
}

TimelineSemaphorePoolStats TimelineSemaphorePool::GetStats() const {
    std::lock_guard lock(m_mutex);
    TimelineSemaphorePoolStats stats{};
    stats.totalSemaphores = static_cast<u32>(m_semaphores.size());
    stats.availableSemaphores = static_cast<u32>(m_available.size());
    stats.inUseSemaphores = stats.totalSemaphores - stats.availableSemaphores;
    stats.growthEvents = m_growthEvents;

    u64 highest = 0;
    for (const auto& sem : m_semaphores) {
        if (sem.currentValue > highest) highest = sem.currentValue;
    }
    stats.highestSignaledValue = highest;

    return stats;
}

void TimelineSemaphorePool::GrowPool(u32 count) {
    u32 startIdx = static_cast<u32>(m_semaphores.size());
    m_semaphores.resize(startIdx + count);

    for (u32 i = startIdx; i < startIdx + count; ++i) {
        auto& sem = m_semaphores[i];
        sem.currentValue = 0;
        sem.inUse = false;

        // TODO: VkSemaphoreTypeCreateInfo typeCI{};
        // typeCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        // typeCI.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        // typeCI.initialValue = 0;
        // VkSemaphoreCreateInfo ci{};
        // ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        // ci.pNext = &typeCI;
        // vkCreateSemaphore(device, &ci, nullptr, &sem.handle);

        sem.handle = static_cast<u64>(i + 1); // Stub handle
        m_available.push(i);
    }
}

} // namespace nge::rhi
