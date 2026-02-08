#include "engine/rhi/common/rhi_command_pool_ring.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool CommandPoolRing::Init(IDevice* device, const CommandPoolRingConfig& config) {
    m_device = device;
    m_config = config;
    m_currentFrame = 0;
    m_totalAllocated = 0;

    m_frames.resize(config.framesInFlight);

    // Pre-allocate initial pools per frame
    for (u32 f = 0; f < config.framesInFlight; ++f) {
        m_frames[f].pools.reserve(config.initialPoolsPerFrame);
    }

    NGE_LOG_INFO("Command pool ring initialized: {} frames in flight, {} initial pools/frame",
                 config.framesInFlight, config.initialPoolsPerFrame);
    return true;
}

void CommandPoolRing::Shutdown() {
    for (auto& frame : m_frames) {
        for (auto& pool : frame.pools) {
            // TODO: vkDestroyCommandPool(device, pool.poolHandle, nullptr);
            pool.primaryBuffers.clear();
            pool.secondaryBuffers.clear();
        }
        frame.pools.clear();
    }
    m_frames.clear();
}

void CommandPoolRing::BeginFrame(u32 frameIndex) {
    std::lock_guard lock(m_mutex);
    m_currentFrame = frameIndex % m_config.framesInFlight;

    auto& frame = m_frames[m_currentFrame];
    for (auto& pool : frame.pools) {
        // TODO: vkResetCommandPool(device, pool.poolHandle, 0);
        pool.nextPrimary = 0;
        pool.nextSecondary = 0;
    }
}

ICommandList* CommandPoolRing::Allocate(QueueType queue) {
    std::lock_guard lock(m_mutex);

    auto& pool = GetOrCreatePool(m_currentFrame, queue);

    if (pool.nextPrimary < static_cast<u32>(pool.primaryBuffers.size())) {
        auto* cmd = pool.primaryBuffers[pool.nextPrimary++];
        m_totalAllocated++;
        return cmd;
    }

    // Need to allocate a new command buffer from the pool
    // TODO:
    // VkCommandBufferAllocateInfo allocInfo{};
    // allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    // allocInfo.commandPool = pool.poolHandle;
    // allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    // allocInfo.commandBufferCount = 1;
    // VkCommandBuffer cmdBuf;
    // vkAllocateCommandBuffers(device, &allocInfo, &cmdBuf);

    ICommandList* cmd = nullptr; // Stub — would wrap VkCommandBuffer
    pool.primaryBuffers.push_back(cmd);
    pool.nextPrimary = static_cast<u32>(pool.primaryBuffers.size());
    m_totalAllocated++;
    return cmd;
}

ICommandList* CommandPoolRing::AllocateSecondary(QueueType queue) {
    std::lock_guard lock(m_mutex);

    auto& pool = GetOrCreatePool(m_currentFrame, queue);

    if (pool.nextSecondary < static_cast<u32>(pool.secondaryBuffers.size())) {
        auto* cmd = pool.secondaryBuffers[pool.nextSecondary++];
        m_totalAllocated++;
        return cmd;
    }

    // TODO: Allocate VK_COMMAND_BUFFER_LEVEL_SECONDARY
    ICommandList* cmd = nullptr; // Stub
    pool.secondaryBuffers.push_back(cmd);
    pool.nextSecondary = static_cast<u32>(pool.secondaryBuffers.size());
    m_totalAllocated++;
    return cmd;
}

void CommandPoolRing::EndFrame() {
    // Nothing to do — pools are reset at BeginFrame of the recycled frame
}

CommandPoolRingStats CommandPoolRing::GetStats() const {
    std::lock_guard lock(m_mutex);
    CommandPoolRingStats stats{};
    stats.activeFrame = m_currentFrame;

    u32 totalPools = 0;
    u32 usedThisFrame = 0;
    for (const auto& frame : m_frames) {
        totalPools += static_cast<u32>(frame.pools.size());
    }

    if (m_currentFrame < m_frames.size()) {
        for (const auto& pool : m_frames[m_currentFrame].pools) {
            usedThisFrame += pool.nextPrimary + pool.nextSecondary;
        }
    }

    stats.totalPools = totalPools;
    stats.poolsUsedThisFrame = usedThisFrame;
    stats.commandBuffersAllocated = m_totalAllocated;
    return stats;
}

CommandPoolRing::FrameCommandPool& CommandPoolRing::GetOrCreatePool(u32 frameIdx, QueueType queue) {
    auto& frame = m_frames[frameIdx];

    for (auto& pool : frame.pools) {
        if (pool.queue == queue) return pool;
    }

    // Create new pool for this queue type
    FrameCommandPool newPool;
    newPool.queue = queue;

    // TODO:
    // VkCommandPoolCreateInfo ci{};
    // ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    // ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    // ci.queueFamilyIndex = getQueueFamilyIndex(queue);
    // vkCreateCommandPool(device, &ci, nullptr, &newPool.poolHandle);
    newPool.poolHandle = static_cast<u64>(frameIdx * 10 + static_cast<u32>(queue) + 1); // Stub

    frame.pools.push_back(std::move(newPool));
    return frame.pools.back();
}

} // namespace nge::rhi
