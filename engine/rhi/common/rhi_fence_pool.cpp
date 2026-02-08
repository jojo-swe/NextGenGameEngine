#include "engine/rhi/common/rhi_fence_pool.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool FencePool::Init(IDevice* device, u32 initialCount) {
    m_device = device;
    m_totalCreated = 0;
    m_activeCount = 0;
    m_nextId = 1;

    m_available.reserve(initialCount);
    m_active.reserve(initialCount);

    for (u32 i = 0; i < initialCount; ++i) {
        m_available.push_back(CreateFence());
    }

    NGE_LOG_INFO("Fence pool initialized: {} pre-allocated fences", initialCount);
    return true;
}

void FencePool::Shutdown() {
    // TODO: vkDestroyFence for each fence
    // for (auto& f : m_available) vkDestroyFence(device, f.handle, nullptr);
    // for (auto& f : m_active)    vkDestroyFence(device, f.handle, nullptr);
    m_available.clear();
    m_active.clear();
    m_activeCount = 0;
}

FenceHandle FencePool::Acquire() {
    std::lock_guard lock(m_mutex);

    FenceHandle fence;
    if (!m_available.empty()) {
        fence = m_available.back();
        m_available.pop_back();
    } else {
        fence = CreateFence();
    }

    m_active.push_back(fence);
    m_activeCount++;
    return fence;
}

void FencePool::WaitAndRecycle(FenceHandle fence) {
    if (!fence.IsValid()) return;

    Wait(fence);

    std::lock_guard lock(m_mutex);

    // Remove from active list
    auto it = std::find_if(m_active.begin(), m_active.end(),
        [&](const FenceHandle& f) { return f.id == fence.id; });
    if (it != m_active.end()) {
        m_active.erase(it);
        m_activeCount--;
    }

    // TODO: vkResetFences(device, 1, &fence.handle);

    // Return to available pool
    m_available.push_back(fence);
}

bool FencePool::IsSignaled(FenceHandle fence) const {
    if (!fence.IsValid()) return false;
    // TODO: VkResult result = vkGetFenceStatus(device, fence.handle);
    // return result == VK_SUCCESS;
    return true; // Stub: always signaled
}

void FencePool::Wait(FenceHandle fence, u64 timeoutNs) const {
    if (!fence.IsValid()) return;
    // TODO: vkWaitForFences(device, 1, &fence.handle, VK_TRUE, timeoutNs);
    (void)timeoutNs;
}

u32 FencePool::RecycleSignaled() {
    std::lock_guard lock(m_mutex);
    u32 recycled = 0;

    for (auto it = m_active.begin(); it != m_active.end(); ) {
        if (IsSignaled(*it)) {
            // TODO: vkResetFences(device, 1, &it->handle);
            m_available.push_back(*it);
            it = m_active.erase(it);
            m_activeCount--;
            recycled++;
        } else {
            ++it;
        }
    }

    return recycled;
}

u32 FencePool::GetPooledCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_available.size());
}

FenceHandle FencePool::CreateFence() {
    FenceHandle fence;
    // TODO: VkFenceCreateInfo ci{};
    // ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // vkCreateFence(device, &ci, nullptr, &fence.handle);
    fence.handle = static_cast<u64>(m_totalCreated + 1);
    fence.id = m_nextId++;
    m_totalCreated++;
    return fence;
}

} // namespace nge::rhi
