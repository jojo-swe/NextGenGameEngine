#include "engine/rhi/common/rhi_async_copy.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool AsyncCopyEngine::Init(IDevice* device, QueueSyncManager* syncManager, const AsyncCopyConfig& config) {
    m_device = device;
    m_syncManager = syncManager;
    m_config = config;
    m_nextTicketId = 1;
    m_lastSignaledValue = 0;
    m_completedCount = 0;
    m_totalBytesTransferred = 0;
    m_batchesSubmitted = 0;

    // Priority queue: higher priority first
    auto cmp = [](const PendingCopy& a, const PendingCopy& b) {
        return a.request.priority < b.request.priority;
    };
    m_pendingQueue = decltype(m_pendingQueue)(cmp);

    NGE_LOG_INFO("Async copy engine initialized: max batch {} copies / {} MB",
                 config.maxBatchSize, config.maxBytesPerBatch / (1024 * 1024));
    return true;
}

void AsyncCopyEngine::Shutdown() {
    // Flush remaining copies
    Flush();
    ProcessCallbacks();
}

CopyTicket AsyncCopyEngine::Submit(const CopyRequest& request) {
    std::lock_guard lock(m_mutex);

    CopyTicket ticket;
    ticket.id = m_nextTicketId++;
    ticket.timelineValue = 0; // Assigned at flush
    ticket.valid = true;

    PendingCopy pending;
    pending.request = request;
    pending.ticket = ticket;
    m_pendingQueue.push(std::move(pending));

    return ticket;
}

CopyTicket AsyncCopyEngine::UploadBuffer(BufferHandle dst, u64 dstOffset, const void* data, u64 size) {
    CopyRequest req;
    req.type = CopyType::BufferToBuffer;
    req.dstBuffer = dst;
    req.dstOffset = dstOffset;
    req.sizeBytes = size;
    req.priority = 1;

    // TODO: Allocate staging buffer, memcpy data, then copy on transfer queue
    // For now, this queues the request; actual staging happens at Flush time
    (void)data;

    return Submit(req);
}

CopyTicket AsyncCopyEngine::UploadTexture(TextureHandle dst, u32 mipLevel, u32 arrayLayer,
                                            const void* data, u32 width, u32 height, u32 bytesPerPixel) {
    CopyRequest req;
    req.type = CopyType::BufferToImage;
    req.dstImage = dst;
    req.mipLevel = mipLevel;
    req.arrayLayer = arrayLayer;
    req.width = width;
    req.height = height;
    req.sizeBytes = static_cast<u64>(width) * height * bytesPerPixel;
    req.priority = 2; // Texture uploads higher priority

    (void)data;

    return Submit(req);
}

CopyTicket AsyncCopyEngine::ReadbackBuffer(BufferHandle src, u64 srcOffset,
                                             BufferHandle dst, u64 dstOffset, u64 size) {
    CopyRequest req;
    req.type = CopyType::BufferToBuffer;
    req.srcBuffer = src;
    req.srcOffset = srcOffset;
    req.dstBuffer = dst;
    req.dstOffset = dstOffset;
    req.sizeBytes = size;
    req.priority = 0; // Readbacks lower priority

    return Submit(req);
}

bool AsyncCopyEngine::IsComplete(const CopyTicket& ticket) const {
    if (!ticket.valid || ticket.timelineValue == 0) return false;
    u64 completed = m_syncManager->GetCurrentValue(QueueType::Transfer);
    return completed >= ticket.timelineValue;
}

void AsyncCopyEngine::Wait(const CopyTicket& ticket) {
    if (!ticket.valid || ticket.timelineValue == 0) return;
    m_syncManager->CpuWait(QueueType::Transfer, ticket.timelineValue);
}

u32 AsyncCopyEngine::Flush() {
    std::lock_guard lock(m_mutex);

    if (m_pendingQueue.empty()) return 0;

    // Build a batch
    std::vector<PendingCopy> batch;
    u64 batchBytes = 0;

    while (!m_pendingQueue.empty() &&
           batch.size() < m_config.maxBatchSize &&
           batchBytes < m_config.maxBytesPerBatch) {
        auto copy = m_pendingQueue.top();
        m_pendingQueue.pop();
        batchBytes += copy.request.sizeBytes;
        batch.push_back(std::move(copy));
    }

    if (batch.empty()) return 0;

    // Signal timeline for this batch
    u64 signalValue = m_syncManager->Signal(QueueType::Transfer);
    m_lastSignaledValue = signalValue;

    // TODO: Record command buffer on transfer queue
    // ICommandList* cmd = m_device->AllocateTransferCommandList();
    // for (const auto& copy : batch) {
    //     switch (copy.request.type) {
    //         case CopyType::BufferToBuffer:
    //             cmd->CopyBuffer(copy.request.srcBuffer, copy.request.srcOffset,
    //                            copy.request.dstBuffer, copy.request.dstOffset,
    //                            copy.request.sizeBytes);
    //             break;
    //         case CopyType::BufferToImage:
    //             cmd->CopyBufferToTexture(stagingBuffer, stagingOffset,
    //                                      copy.request.dstImage,
    //                                      copy.request.mipLevel, copy.request.arrayLayer,
    //                                      copy.request.width, copy.request.height);
    //             break;
    //         case CopyType::FillBuffer:
    //             cmd->FillBuffer(copy.request.dstBuffer, copy.request.dstOffset,
    //                            copy.request.sizeBytes, copy.request.fillValue);
    //             break;
    //     }
    // }
    // Submit with timeline semaphore signal
    // m_device->SubmitTransfer(cmd, signalValue);

    // Update tickets with the timeline value
    {
        std::lock_guard cbLock(m_callbackMutex);
        for (auto& copy : batch) {
            copy.ticket.timelineValue = signalValue;
            m_totalBytesTransferred += copy.request.sizeBytes;

            if (copy.request.onComplete) {
                m_completedCallbacks.push_back(std::move(copy));
            }
        }
    }

    m_completedCount += static_cast<u32>(batch.size());
    m_batchesSubmitted++;

    return static_cast<u32>(batch.size());
}

void AsyncCopyEngine::ProcessCallbacks() {
    std::lock_guard lock(m_callbackMutex);

    for (auto it = m_completedCallbacks.begin(); it != m_completedCallbacks.end(); ) {
        if (IsComplete(it->ticket)) {
            if (it->request.onComplete) {
                it->request.onComplete();
            }
            it = m_completedCallbacks.erase(it);
        } else {
            ++it;
        }
    }
}

void AsyncCopyEngine::Update(u64 frameNumber) {
    (void)frameNumber;
    // Auto-flush if enough pending copies
    {
        std::lock_guard lock(m_mutex);
        if (m_pendingQueue.size() >= m_config.maxBatchSize) {
            // Will flush below after releasing lock
        }
    }
    Flush();
    ProcessCallbacks();
}

AsyncCopyStats AsyncCopyEngine::GetStats() const {
    std::lock_guard lock(m_mutex);
    AsyncCopyStats stats{};
    stats.pendingCopies = static_cast<u32>(m_pendingQueue.size());
    stats.completedCopies = m_completedCount;
    stats.totalBytesTransferred = m_totalBytesTransferred;
    stats.batchesSubmitted = m_batchesSubmitted;
    stats.avgBatchLatencyMs = 0; // Would need timing to compute
    return stats;
}

} // namespace nge::rhi
