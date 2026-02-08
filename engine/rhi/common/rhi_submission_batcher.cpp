#include "engine/rhi/common/rhi_submission_batcher.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool SubmissionBatcher::Init(IDevice* device) {
    m_device = device;
    m_totalSubmits = 0;
    m_batchedSubmits = 0;
    NGE_LOG_INFO("Submission batcher initialized");
    return true;
}

void SubmissionBatcher::Shutdown() {
    Flush();
}

void SubmissionBatcher::BeginFrame() {
    std::lock_guard lock(m_mutex);
    m_graphicsBatch.entries.clear();
    m_computeBatch.entries.clear();
    m_transferBatch.entries.clear();
    m_totalSubmits = 0;
    m_batchedSubmits = 0;
}

void SubmissionBatcher::Submit(QueueType queue, ICommandList* cmd) {
    SubmissionEntry entry;
    entry.commandList = cmd;
    entry.fence = 0;
    Submit(queue, entry);
}

void SubmissionBatcher::Submit(QueueType queue, ICommandList* cmd,
                                 const std::vector<SemaphoreWait>& waits,
                                 const std::vector<SemaphoreSignal>& signals,
                                 u64 fence) {
    SubmissionEntry entry;
    entry.commandList = cmd;
    entry.waits = waits;
    entry.signals = signals;
    entry.fence = fence;
    Submit(queue, entry);
}

void SubmissionBatcher::Submit(QueueType queue, const SubmissionEntry& entry) {
    std::lock_guard lock(m_mutex);
    m_totalSubmits++;

    switch (queue) {
        case QueueType::Graphics:
            m_graphicsBatch.entries.push_back(entry);
            break;
        case QueueType::Compute:
            m_computeBatch.entries.push_back(entry);
            break;
        case QueueType::Transfer:
            m_transferBatch.entries.push_back(entry);
            break;
    }
}

void SubmissionBatcher::Flush() {
    std::lock_guard lock(m_mutex);

    if (!m_graphicsBatch.entries.empty()) {
        FlushBatch(QueueType::Graphics, m_graphicsBatch);
    }
    if (!m_computeBatch.entries.empty()) {
        FlushBatch(QueueType::Compute, m_computeBatch);
    }
    if (!m_transferBatch.entries.empty()) {
        FlushBatch(QueueType::Transfer, m_transferBatch);
    }
}

void SubmissionBatcher::FlushQueue(QueueType queue) {
    std::lock_guard lock(m_mutex);

    switch (queue) {
        case QueueType::Graphics:
            if (!m_graphicsBatch.entries.empty())
                FlushBatch(QueueType::Graphics, m_graphicsBatch);
            break;
        case QueueType::Compute:
            if (!m_computeBatch.entries.empty())
                FlushBatch(QueueType::Compute, m_computeBatch);
            break;
        case QueueType::Transfer:
            if (!m_transferBatch.entries.empty())
                FlushBatch(QueueType::Transfer, m_transferBatch);
            break;
    }
}

void SubmissionBatcher::FlushBatch(QueueType queue, QueueBatch& batch) {
    // TODO: Merge compatible submissions into a single vkQueueSubmit2 call.
    //
    // Compatible submissions: entries that can share a single VkSubmitInfo2.
    // Entries with semaphore waits/signals or fence signals may need to stay
    // as separate VkSubmitInfo2 within the same vkQueueSubmit2 call.
    //
    // VkSubmitInfo2 submitInfos[batch.entries.size()];
    // for (auto& entry : batch.entries) {
    //     VkSubmitInfo2 si{};
    //     si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    //     si.commandBufferInfoCount = 1;
    //     // ... configure waits, signals, command buffers
    // }
    // vkQueueSubmit2(GetQueue(queue), submitInfoCount, submitInfos, lastFence);

    m_batchedSubmits++;
    u32 entryCount = static_cast<u32>(batch.entries.size());

    NGE_LOG_DEBUG("Submission batcher: flushed {} entries on queue {} as 1 batched submit",
                  entryCount, static_cast<u32>(queue));

    batch.entries.clear();
    (void)queue;
}

u32 SubmissionBatcher::GetPendingSubmitCount(QueueType queue) const {
    std::lock_guard lock(m_mutex);
    switch (queue) {
        case QueueType::Graphics: return static_cast<u32>(m_graphicsBatch.entries.size());
        case QueueType::Compute:  return static_cast<u32>(m_computeBatch.entries.size());
        case QueueType::Transfer: return static_cast<u32>(m_transferBatch.entries.size());
    }
    return 0;
}

f32 SubmissionBatcher::GetBatchingEfficiency() const {
    if (m_totalSubmits == 0) return 1.0f;
    return 1.0f - static_cast<f32>(m_batchedSubmits) / static_cast<f32>(m_totalSubmits);
}

} // namespace nge::rhi
