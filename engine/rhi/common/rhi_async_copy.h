#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/rhi/common/rhi_queue_sync.h"
#include <vector>
#include <queue>
#include <mutex>
#include <functional>

namespace nge::rhi {

// ─── GPU Async Copy Engine ───────────────────────────────────────────────
// Manages a dedicated transfer queue for DMA operations (texture uploads,
// buffer copies, readbacks) that run concurrently with graphics/compute
// work. Uses timeline semaphores for cross-queue synchronization.
//
// Operations are queued and batched into command buffers submitted on the
// transfer queue. Completion is tracked via timeline semaphore values.

enum class CopyType : u8 {
    BufferToBuffer,
    BufferToImage,
    ImageToBuffer,
    ImageToImage,
    FillBuffer,
};

struct CopyRequest {
    CopyType type;

    // Source
    BufferHandle  srcBuffer;
    TextureHandle srcImage;
    u64           srcOffset = 0;

    // Destination
    BufferHandle  dstBuffer;
    TextureHandle dstImage;
    u64           dstOffset = 0;

    // Size / region
    u64 sizeBytes = 0;
    u32 width = 0;
    u32 height = 0;
    u32 depth = 1;
    u32 mipLevel = 0;
    u32 arrayLayer = 0;

    // Fill value (for FillBuffer)
    u32 fillValue = 0;

    // Priority (higher = processed first)
    u32 priority = 0;

    // Completion callback
    std::function<void()> onComplete;
};

struct CopyTicket {
    u64  id;
    u64  timelineValue;
    bool valid = false;
};

struct AsyncCopyConfig {
    u32 maxPendingCopies = 256;
    u32 maxBatchSize = 32;         // Max copies per command buffer submission
    u64 maxBytesPerBatch = 64 * 1024 * 1024; // 64 MB per batch
};

struct AsyncCopyStats {
    u32 pendingCopies;
    u32 completedCopies;
    u64 totalBytesTransferred;
    u32 batchesSubmitted;
    f64 avgBatchLatencyMs;
};

class AsyncCopyEngine {
public:
    bool Init(IDevice* device, QueueSyncManager* syncManager, const AsyncCopyConfig& config = {});
    void Shutdown();

    // Queue a copy operation (returns a ticket for tracking)
    CopyTicket Submit(const CopyRequest& request);

    // Queue a buffer upload (convenience)
    CopyTicket UploadBuffer(BufferHandle dst, u64 dstOffset, const void* data, u64 size);

    // Queue a texture upload (convenience)
    CopyTicket UploadTexture(TextureHandle dst, u32 mipLevel, u32 arrayLayer,
                              const void* data, u32 width, u32 height, u32 bytesPerPixel);

    // Queue a readback (convenience)
    CopyTicket ReadbackBuffer(BufferHandle src, u64 srcOffset, BufferHandle dst, u64 dstOffset, u64 size);

    // Check if a copy has completed
    bool IsComplete(const CopyTicket& ticket) const;

    // Wait for a specific copy to complete (blocking)
    void Wait(const CopyTicket& ticket);

    // Flush: submit all pending copies as a batch
    u32 Flush();

    // Process completed callbacks (call from main thread)
    void ProcessCallbacks();

    // Per-frame update
    void Update(u64 frameNumber);

    AsyncCopyStats GetStats() const;

private:
    struct PendingCopy {
        CopyRequest request;
        CopyTicket  ticket;
    };

    IDevice* m_device = nullptr;
    QueueSyncManager* m_syncManager = nullptr;
    AsyncCopyConfig m_config;

    std::priority_queue<PendingCopy, std::vector<PendingCopy>,
        std::function<bool(const PendingCopy&, const PendingCopy&)>> m_pendingQueue;

    std::vector<PendingCopy> m_completedCallbacks;

    u64 m_nextTicketId = 1;
    u64 m_lastSignaledValue = 0;

    // Stats
    u32 m_completedCount = 0;
    u64 m_totalBytesTransferred = 0;
    u32 m_batchesSubmitted = 0;

    mutable std::mutex m_mutex;
    mutable std::mutex m_callbackMutex;
};

} // namespace nge::rhi
