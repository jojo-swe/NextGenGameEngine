#include "engine/rhi/common/rhi_command_pool.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

// ─── Command Pool Manager ────────────────────────────────────────────────

bool CommandPoolManager::Init(IDevice* device, u32 framesInFlight, QueueType queueType) {
    m_device = device;
    m_framesInFlight = framesInFlight;
    m_queueType = queueType;
    m_currentFrame = 0;

    m_pools.resize(framesInFlight);

    for (u32 i = 0; i < framesInFlight; ++i) {
        // TODO: Create VkCommandPool per frame
        // VkCommandPoolCreateInfo ci{};
        // ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        // ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        // ci.queueFamilyIndex = GetQueueFamily(queueType);
        // vkCreateCommandPool(device, &ci, nullptr, &pool);

        m_pools[i].poolHandle = static_cast<u64>(i + 1); // Stub
        m_pools[i].primaryUsed = 0;
        m_pools[i].secondaryUsed = 0;
        m_pools[i].recording = false;
    }

    NGE_LOG_INFO("Command pool manager initialized: {} pools, queue={}",
                 framesInFlight, static_cast<u32>(queueType));
    return true;
}

void CommandPoolManager::Shutdown() {
    // TODO: Destroy all VkCommandPools
    for (auto& pool : m_pools) {
        pool.primaryBuffers.clear();
        pool.secondaryBuffers.clear();
        pool.poolHandle = 0;
    }
    m_pools.clear();
}

void CommandPoolManager::BeginFrame(u32 frameIndex) {
    std::lock_guard lock(m_mutex);
    m_currentFrame = frameIndex % m_framesInFlight;
    ResetPool(m_currentFrame);
    m_pools[m_currentFrame].recording = true;
}

ICommandList* CommandPoolManager::GetCommandList() {
    std::lock_guard lock(m_mutex);
    auto& pool = m_pools[m_currentFrame];

    if (pool.primaryUsed < pool.primaryBuffers.size()) {
        return pool.primaryBuffers[pool.primaryUsed++];
    }

    // Allocate new command buffer from pool
    // TODO: vkAllocateCommandBuffers with VK_COMMAND_BUFFER_LEVEL_PRIMARY
    ICommandList* cmd = nullptr; // Stub — would be allocated from VkCommandPool
    pool.primaryBuffers.push_back(cmd);
    pool.primaryUsed++;
    return cmd;
}

ICommandList* CommandPoolManager::GetSecondaryCommandList() {
    std::lock_guard lock(m_mutex);
    auto& pool = m_pools[m_currentFrame];

    if (pool.secondaryUsed < pool.secondaryBuffers.size()) {
        return pool.secondaryBuffers[pool.secondaryUsed++];
    }

    // TODO: vkAllocateCommandBuffers with VK_COMMAND_BUFFER_LEVEL_SECONDARY
    ICommandList* cmd = nullptr; // Stub
    pool.secondaryBuffers.push_back(cmd);
    pool.secondaryUsed++;
    return cmd;
}

void CommandPoolManager::SubmitAll() {
    std::lock_guard lock(m_mutex);
    auto& pool = m_pools[m_currentFrame];

    // TODO: Submit all used primary command buffers
    // VkSubmitInfo submitInfo{};
    // submitInfo.commandBufferCount = pool.primaryUsed;
    // vkQueueSubmit(queue, 1, &submitInfo, fence);

    pool.recording = false;
}

void CommandPoolManager::ResetPool(u32 frameIndex) {
    auto& pool = m_pools[frameIndex];

    // TODO: vkResetCommandPool(device, pool.poolHandle, 0);
    pool.primaryUsed = 0;
    pool.secondaryUsed = 0;
    pool.recording = false;
}

u32 CommandPoolManager::GetActiveCommandListCount() const {
    u32 total = 0;
    for (const auto& pool : m_pools) {
        total += pool.primaryUsed + pool.secondaryUsed;
    }
    return total;
}

// ─── Thread-Local Command Pool ───────────────────────────────────────────

bool ThreadLocalCommandPool::Init(IDevice* device, QueueType queueType) {
    m_device = device;

    // TODO: Create VkCommandPool with VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
    m_poolHandle = 1; // Stub
    m_used = 0;

    (void)queueType;
    return true;
}

void ThreadLocalCommandPool::Shutdown() {
    // TODO: vkDestroyCommandPool
    m_buffers.clear();
    m_poolHandle = 0;
}

ICommandList* ThreadLocalCommandPool::GetCommandList() {
    if (m_used < m_buffers.size()) {
        return m_buffers[m_used++];
    }

    // TODO: Allocate from VkCommandPool
    ICommandList* cmd = nullptr; // Stub
    m_buffers.push_back(cmd);
    m_used++;
    return cmd;
}

void ThreadLocalCommandPool::Reset() {
    // TODO: vkResetCommandPool
    m_used = 0;
}

} // namespace nge::rhi
