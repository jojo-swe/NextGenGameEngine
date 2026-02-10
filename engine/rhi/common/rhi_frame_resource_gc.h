#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <functional>
#include <mutex>

namespace nge::rhi {

// ─── GPU Frame Resource Garbage Collector ─────────────────────────────────
// Defers GPU resource destruction until the GPU has finished using them.
// Resources are queued with a frame index; once that frame's fence signals,
// the destructor callback is invoked. Prevents use-after-free on in-flight
// resources across multiple buffered frames.
//
// Use cases:
//   - Deferred buffer/image/view destruction
//   - Deferred descriptor set release
//   - Deferred pipeline object cleanup
//   - Frame-based garbage collection (N frames of latency)
//   - Emergency flush for shutdown

enum class GCResourceType : u8 {
    Buffer,
    Image,
    ImageView,
    Sampler,
    DescriptorSet,
    Pipeline,
    RenderPass,
    Framebuffer,
    CommandBuffer,
    Other,
};

struct GCEntry {
    u64                       resourceHandle;
    GCResourceType            type;
    u32                       frameQueued;
    std::string               debugName;
    std::function<void(u64)>  destructor;    // Called with resourceHandle
};

struct FrameResourceGCConfig {
    u32  maxPendingEntries = 8192;
    u32  framesToDefer = 3;          // Wait N frames before destroying
    bool logDestructions = false;
};

struct FrameResourceGCStats {
    u32 totalQueued;
    u32 totalDestroyed;
    u32 pendingEntries;
    u32 pendingByType[10];           // Per GCResourceType
    u64 peakPending;
};

class FrameResourceGC {
public:
    bool Init(const FrameResourceGCConfig& config = {});
    void Shutdown();

    // Queue a resource for deferred destruction
    bool QueueDestroy(u64 handle, GCResourceType type, u32 currentFrame,
                       std::function<void(u64)> destructor,
                       const std::string& name = "");

    // Collect garbage for completed frames
    // Call once per frame with the oldest in-flight frame index
    u32 Collect(u32 completedFrame);

    // Flush all pending entries immediately (for shutdown)
    u32 FlushAll();

    // Get pending count
    u32 GetPendingCount() const;

    // Get pending count by type
    u32 GetPendingCountByType(GCResourceType type) const;

    // Check if a specific handle is pending destruction
    bool IsPending(u64 handle) const;

    void Reset();

    FrameResourceGCStats GetStats() const;

private:
    FrameResourceGCConfig m_config;

    std::vector<GCEntry> m_pendingEntries;

    u32 m_totalQueued = 0;
    u32 m_totalDestroyed = 0;
    u64 m_peakPending = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
