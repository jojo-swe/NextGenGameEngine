#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <mutex>

namespace nge::rhi {

// ─── GPU Submission Batcher ──────────────────────────────────────────────
// Batches command buffer submissions to minimize vkQueueSubmit calls
// per frame. Each submit has overhead (~5-20μs), so batching reduces
// CPU-side driver cost significantly.
//
// Collects command buffers, semaphore waits/signals, and fence signals
// into batched submissions per queue. Flushes once at frame end.

struct SemaphoreWait {
    u64 semaphore;       // VkSemaphore handle
    u64 value;           // Timeline value (0 for binary)
    u32 stageMask;       // VkPipelineStageFlags2
};

struct SemaphoreSignal {
    u64 semaphore;
    u64 value;
};

struct SubmissionEntry {
    ICommandList*              commandList;
    std::vector<SemaphoreWait>   waits;
    std::vector<SemaphoreSignal> signals;
    u64                        fence;  // Optional fence to signal (0 = none)
};

class SubmissionBatcher {
public:
    bool Init(IDevice* device);
    void Shutdown();

    // Per-frame reset
    void BeginFrame();

    // Queue a command buffer for submission
    void Submit(QueueType queue, ICommandList* cmd);

    // Queue with semaphore sync
    void Submit(QueueType queue, ICommandList* cmd,
                  const std::vector<SemaphoreWait>& waits,
                  const std::vector<SemaphoreSignal>& signals,
                  u64 fence = 0);

    // Queue a raw submission entry
    void Submit(QueueType queue, const SubmissionEntry& entry);

    // Flush all queued submissions (call once at frame end)
    void Flush();

    // Force flush a specific queue (for mid-frame sync points)
    void FlushQueue(QueueType queue);

    // Stats
    u32 GetPendingSubmitCount(QueueType queue) const;
    u32 GetTotalSubmitsThisFrame() const { return m_totalSubmits; }
    u32 GetBatchedSubmitsThisFrame() const { return m_batchedSubmits; }
    f32 GetBatchingEfficiency() const;

private:
    struct QueueBatch {
        std::vector<SubmissionEntry> entries;
    };

    void FlushBatch(QueueType queue, QueueBatch& batch);

    IDevice* m_device = nullptr;

    QueueBatch m_graphicsBatch;
    QueueBatch m_computeBatch;
    QueueBatch m_transferBatch;

    u32 m_totalSubmits = 0;
    u32 m_batchedSubmits = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
