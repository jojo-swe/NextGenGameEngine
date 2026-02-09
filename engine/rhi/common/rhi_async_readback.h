#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Async Readback Manager ──────────────────────────────────────────
// Non-blocking GPU→CPU data transfer using a ring buffer of staging
// buffers. Supports multi-frame latency readbacks with callbacks.
//
// Pipeline:
//   1. Request readback: copy GPU resource → staging buffer
//   2. Fence signals when copy completes (N frames later)
//   3. Map staging buffer, invoke callback with data pointer
//   4. Recycle staging buffer back into ring
//
// Use cases:
//   - Occlusion query result readback
//   - GPU profiler timestamp readback
//   - Screenshot/capture
//   - Compute shader result readback
//   - Texture streaming feedback buffer readback

using ReadbackCallback = std::function<void(const void* data, u64 size, bool success)>;

struct ReadbackRequest {
    u64         sourceResource;   // GPU resource handle
    u64         offset;           // Byte offset in source
    u64         size;             // Bytes to read back
    u32         frameIssued;      // Frame when request was made
    u32         framesToWait;     // Frames to wait before readback (default 2)
    ReadbackCallback callback;
    std::string debugName;
};

enum class ReadbackStatus : u8 {
    Pending,     // Waiting for GPU copy to complete
    Ready,       // Data available in staging buffer
    Delivered,   // Callback invoked, staging buffer recycled
    Failed,      // Copy failed or timed out
};

struct ReadbackSlot {
    u64             stagingBuffer;   // Staging buffer handle
    u64             stagingOffset;
    u64             size;
    u32             requestId;
    u32             readyAtFrame;    // Frame when data will be ready
    ReadbackStatus  status;
    ReadbackCallback callback;
    std::string     debugName;
};

struct AsyncReadbackConfig {
    u64  stagingBufferSize = 16 * 1024 * 1024; // 16 MB ring buffer
    u32  maxPendingRequests = 256;
    u32  defaultLatencyFrames = 2;              // GPU→CPU latency
    u32  ringSlots = 3;                         // Triple-buffered staging
};

struct AsyncReadbackStats {
    u32 pendingRequests;
    u32 completedRequests;
    u32 failedRequests;
    u64 totalBytesReadback;
    u64 stagingBufferUsed;
    u64 stagingBufferTotal;
    f32 utilizationPercent;
};

class AsyncReadbackManager {
public:
    bool Init(const AsyncReadbackConfig& config = {});
    void Shutdown();

    // Submit a readback request. Returns a request ID.
    u32 RequestReadback(const ReadbackRequest& request);

    // Cancel a pending request
    void CancelRequest(u32 requestId);

    // Per-frame update: check fences, deliver completed readbacks
    void Update(u32 currentFrame);

    // Force-flush all pending readbacks (blocking)
    void FlushAll(u32 currentFrame);

    // Query status of a specific request
    ReadbackStatus GetRequestStatus(u32 requestId) const;

    // Get the number of pending requests
    u32 GetPendingCount() const;

    AsyncReadbackStats GetStats() const;

private:
    struct StagingRegion {
        u64 offset;
        u64 size;
        bool inUse;
    };

    u64 AllocateStaging(u64 size);
    void FreeStaging(u64 offset, u64 size);

    AsyncReadbackConfig m_config;

    std::vector<ReadbackSlot> m_slots;
    std::vector<StagingRegion> m_freeRegions;

    u32 m_nextRequestId = 1;
    u32 m_completedCount = 0;
    u32 m_failedCount = 0;
    u64 m_totalBytesRead = 0;
    u64 m_stagingUsed = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
