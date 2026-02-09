#include "engine/rhi/common/rhi_async_readback.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool AsyncReadbackManager::Init(const AsyncReadbackConfig& config) {
    m_config = config;
    m_nextRequestId = 1;
    m_completedCount = 0;
    m_failedCount = 0;
    m_totalBytesRead = 0;
    m_stagingUsed = 0;

    // Initialize staging ring as one large free region
    m_freeRegions.clear();
    m_freeRegions.push_back({0, config.stagingBufferSize, false});

    m_slots.reserve(config.maxPendingRequests);

    // TODO: Create VkBuffer with VK_BUFFER_USAGE_TRANSFER_DST_BIT
    // and VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT
    // for each ring slot

    NGE_LOG_INFO("Async readback manager initialized: staging={}MB, maxPending={}, latency={}",
                 config.stagingBufferSize / (1024 * 1024), config.maxPendingRequests,
                 config.defaultLatencyFrames);
    return true;
}

void AsyncReadbackManager::Shutdown() {
    m_slots.clear();
    m_freeRegions.clear();
    // TODO: Destroy staging buffers
}

u32 AsyncReadbackManager::RequestReadback(const ReadbackRequest& request) {
    std::lock_guard lock(m_mutex);

    if (m_slots.size() >= m_config.maxPendingRequests) {
        NGE_LOG_WARN("Async readback: max pending requests reached ({})", m_config.maxPendingRequests);
        if (request.callback) request.callback(nullptr, 0, false);
        m_failedCount++;
        return 0;
    }

    u64 stagingOffset = AllocateStaging(request.size);
    if (stagingOffset == UINT64_MAX) {
        NGE_LOG_WARN("Async readback: staging buffer full, cannot allocate {} bytes", request.size);
        if (request.callback) request.callback(nullptr, 0, false);
        m_failedCount++;
        return 0;
    }

    u32 requestId = m_nextRequestId++;

    ReadbackSlot slot;
    slot.stagingBuffer = 0; // TODO: actual VkBuffer handle
    slot.stagingOffset = stagingOffset;
    slot.size = request.size;
    slot.requestId = requestId;
    slot.readyAtFrame = request.frameIssued + request.framesToWait;
    slot.status = ReadbackStatus::Pending;
    slot.callback = request.callback;
    slot.debugName = request.debugName;

    m_slots.push_back(std::move(slot));

    // TODO: vkCmdCopyBuffer(cmdBuffer, srcBuffer, stagingBuffer, 1, &copyRegion);
    // TODO: Signal fence for this frame

    m_stagingUsed += request.size;

    return requestId;
}

void AsyncReadbackManager::CancelRequest(u32 requestId) {
    std::lock_guard lock(m_mutex);

    auto it = std::find_if(m_slots.begin(), m_slots.end(),
        [requestId](const ReadbackSlot& s) { return s.requestId == requestId; });

    if (it != m_slots.end()) {
        FreeStaging(it->stagingOffset, it->size);
        m_stagingUsed -= it->size;
        m_slots.erase(it);
    }
}

void AsyncReadbackManager::Update(u32 currentFrame) {
    std::lock_guard lock(m_mutex);

    auto it = m_slots.begin();
    while (it != m_slots.end()) {
        if (it->status == ReadbackStatus::Pending && currentFrame >= it->readyAtFrame) {
            // TODO: Check fence, map staging buffer
            // void* mappedData = nullptr;
            // vkMapMemory(device, stagingMemory, it->stagingOffset, it->size, 0, &mappedData);

            it->status = ReadbackStatus::Ready;

            // Deliver via callback
            if (it->callback) {
                // Simulate mapped pointer (in real impl, use vkMapMemory result)
                it->callback(nullptr, it->size, true);
            }

            it->status = ReadbackStatus::Delivered;
            m_completedCount++;
            m_totalBytesRead += it->size;

            // TODO: vkUnmapMemory(device, stagingMemory);

            // Free staging region
            FreeStaging(it->stagingOffset, it->size);
            m_stagingUsed -= it->size;

            it = m_slots.erase(it);
        } else {
            ++it;
        }
    }
}

void AsyncReadbackManager::FlushAll(u32 currentFrame) {
    std::lock_guard lock(m_mutex);

    // Force all pending requests to be ready
    for (auto& slot : m_slots) {
        if (slot.status == ReadbackStatus::Pending) {
            slot.readyAtFrame = currentFrame; // Make ready now
        }
    }

    // Unlock and call Update
    m_mutex.unlock();
    Update(currentFrame);
    m_mutex.lock();
}

ReadbackStatus AsyncReadbackManager::GetRequestStatus(u32 requestId) const {
    std::lock_guard lock(m_mutex);

    for (const auto& slot : m_slots) {
        if (slot.requestId == requestId) return slot.status;
    }

    // Not found — either delivered or never existed
    return ReadbackStatus::Delivered;
}

u32 AsyncReadbackManager::GetPendingCount() const {
    std::lock_guard lock(m_mutex);
    u32 count = 0;
    for (const auto& slot : m_slots) {
        if (slot.status == ReadbackStatus::Pending) count++;
    }
    return count;
}

AsyncReadbackStats AsyncReadbackManager::GetStats() const {
    std::lock_guard lock(m_mutex);
    AsyncReadbackStats stats{};
    stats.pendingRequests = 0;
    for (const auto& slot : m_slots) {
        if (slot.status == ReadbackStatus::Pending) stats.pendingRequests++;
    }
    stats.completedRequests = m_completedCount;
    stats.failedRequests = m_failedCount;
    stats.totalBytesReadback = m_totalBytesRead;
    stats.stagingBufferUsed = m_stagingUsed;
    stats.stagingBufferTotal = m_config.stagingBufferSize;
    stats.utilizationPercent = m_config.stagingBufferSize > 0 ?
        static_cast<f32>(m_stagingUsed) / static_cast<f32>(m_config.stagingBufferSize) * 100.0f : 0.0f;
    return stats;
}

u64 AsyncReadbackManager::AllocateStaging(u64 size) {
    // Simple first-fit allocator
    for (auto& region : m_freeRegions) {
        if (!region.inUse && region.size >= size) {
            u64 offset = region.offset;

            if (region.size == size) {
                region.inUse = true;
            } else {
                // Split: shrink free region
                u64 remainOffset = region.offset + size;
                u64 remainSize = region.size - size;
                region.offset = remainOffset;
                region.size = remainSize;
            }

            return offset;
        }
    }

    return UINT64_MAX; // No space
}

void AsyncReadbackManager::FreeStaging(u64 offset, u64 size) {
    // Add free region and attempt coalescing
    StagingRegion freed;
    freed.offset = offset;
    freed.size = size;
    freed.inUse = false;

    m_freeRegions.push_back(freed);

    // Sort by offset and coalesce adjacent regions
    std::sort(m_freeRegions.begin(), m_freeRegions.end(),
              [](const StagingRegion& a, const StagingRegion& b) { return a.offset < b.offset; });

    std::vector<StagingRegion> coalesced;
    for (const auto& r : m_freeRegions) {
        if (r.inUse) continue;
        if (!coalesced.empty() && coalesced.back().offset + coalesced.back().size == r.offset) {
            coalesced.back().size += r.size;
        } else {
            coalesced.push_back(r);
        }
    }

    m_freeRegions = std::move(coalesced);
}

} // namespace nge::rhi
