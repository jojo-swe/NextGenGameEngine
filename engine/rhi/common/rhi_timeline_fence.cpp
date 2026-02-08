#include "engine/rhi/common/rhi_timeline_fence.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

// ─── Timeline Fence ──────────────────────────────────────────────────────

bool TimelineFence::Init(IDevice* device, u64 initialValue) {
    m_device = device;
    m_nextValue = initialValue + 1;

    // TODO: Create VkSemaphore with VK_SEMAPHORE_TYPE_TIMELINE
    // VkSemaphoreTypeCreateInfo typeInfo{};
    // typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    // typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    // typeInfo.initialValue = initialValue;
    //
    // VkSemaphoreCreateInfo ci{};
    // ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    // ci.pNext = &typeInfo;
    // VkSemaphore sem;
    // vkCreateSemaphore(device, &ci, nullptr, &sem);
    // m_semaphore = reinterpret_cast<u64>(sem);

    m_semaphore = 1; // Stub

    NGE_LOG_DEBUG("Timeline fence initialized (initial value: {})", initialValue);
    return true;
}

void TimelineFence::Shutdown() {
    // TODO: vkDestroySemaphore
    m_semaphore = 0;
}

u64 TimelineFence::Signal(ICommandList* cmd) {
    std::lock_guard lock(m_mutex);
    u64 value = m_nextValue++;
    Signal(cmd, value);
    return value;
}

void TimelineFence::Signal(ICommandList* cmd, u64 value) {
    // TODO: Add timeline semaphore signal to command list submission
    // The actual signal happens when the command list is submitted:
    // VkTimelineSemaphoreSubmitInfo timelineInfo{};
    // timelineInfo.signalSemaphoreValueCount = 1;
    // timelineInfo.pSignalSemaphoreValues = &value;
    (void)cmd;
    (void)value;
}

void TimelineFence::WaitCPU(u64 value, u64 timeoutNs) {
    // TODO: vkWaitSemaphores
    // VkSemaphoreWaitInfo waitInfo{};
    // waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    // waitInfo.semaphoreCount = 1;
    // VkSemaphore sem = reinterpret_cast<VkSemaphore>(m_semaphore);
    // waitInfo.pSemaphores = &sem;
    // waitInfo.pValues = &value;
    // vkWaitSemaphores(device, &waitInfo, timeoutNs);
    (void)value;
    (void)timeoutNs;
}

bool TimelineFence::IsComplete(u64 value) const {
    return GetCompletedValue() >= value;
}

u64 TimelineFence::GetCompletedValue() const {
    // TODO: vkGetSemaphoreCounterValue
    // u64 value;
    // vkGetSemaphoreCounterValue(device, semaphore, &value);
    // return value;
    return m_nextValue - 1; // Stub: assume instant completion
}

// ─── Frame Fence ─────────────────────────────────────────────────────────

bool FrameFence::Init(IDevice* device, u32 framesInFlight) {
    m_framesInFlight = framesInFlight;
    m_frameIndex = 0;
    m_frameCount = 0;
    return m_fence.Init(device, 0);
}

void FrameFence::Shutdown() {
    WaitAll();
    m_fence.Shutdown();
}

void FrameFence::BeginFrame() {
    // If we're too far ahead of the GPU, wait
    if (m_frameCount >= m_framesInFlight) {
        u64 waitValue = m_frameCount - m_framesInFlight + 1;
        m_fence.WaitCPU(waitValue);
    }
}

void FrameFence::EndFrame(ICommandList* cmd) {
    m_frameCount++;
    m_fence.Signal(cmd, m_frameCount);
    m_frameIndex = static_cast<u32>(m_frameCount % m_framesInFlight);
}

void FrameFence::WaitAll() {
    if (m_frameCount > 0) {
        m_fence.WaitCPU(m_frameCount);
    }
}

u32 FrameFence::GetGPULag() const {
    u64 completed = m_fence.GetCompletedValue();
    return static_cast<u32>(m_frameCount - completed);
}

// ─── Deletion Queue ──────────────────────────────────────────────────────

void DeletionQueue::Init(TimelineFence* fence) {
    m_fence = fence;
}

void DeletionQueue::Enqueue(DeleteFunc func, u64 afterValue) {
    m_pending.push_back({std::move(func), afterValue});
}

void DeletionQueue::Enqueue(DeleteFunc func) {
    u64 afterValue = m_fence ? m_fence->GetPendingValue() : 0;
    Enqueue(std::move(func), afterValue);
}

void DeletionQueue::Flush() {
    if (!m_fence) return;

    auto it = m_pending.begin();
    while (it != m_pending.end()) {
        if (m_fence->IsComplete(it->afterValue)) {
            it->func();
            it = m_pending.erase(it);
        } else {
            ++it;
        }
    }
}

void DeletionQueue::FlushAll() {
    for (auto& pending : m_pending) {
        pending.func();
    }
    m_pending.clear();
}

} // namespace nge::rhi
