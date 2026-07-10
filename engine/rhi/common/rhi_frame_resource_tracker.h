#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace nge::rhi {

// ─── GPU Frame Resource Tracker ──────────────────────────────────────────
// Tracks all GPU resource allocations/deallocations per frame for
// leak detection, usage profiling, and debug diagnostics.
//
// Use cases:
//   - Detect resource leaks (allocated but never freed)
//   - Track per-frame allocation churn
//   - Identify high-watermark resource usage
//   - CI regression testing for memory budgets
//   - Debug resource lifetime issues

enum class FrameResourceType : u8 {
    Buffer,
    Image,
    ImageView,
    Sampler,
    DescriptorSet,
    Pipeline,
    RenderPass,
    Framebuffer,
    CommandBuffer,
    Fence,
    Semaphore,
    QueryPool,
    ShaderModule,
};

struct FrameTrackedResource {
    u64          handle;
    FrameResourceType type;
    u64          sizeBytes;
    u32          frameCreated;
    u32          frameLastUsed;
    std::string  debugName;
    std::string  creationCallsite; // Optional: file:line
    bool         isTransient;
};

struct FrameResourceSnapshot {
    u32 frameIndex;
    u32 totalAllocations;
    u32 totalDeallocations;
    u64 totalBytesAllocated;
    u64 totalBytesFreed;
    u64 netBytesAlive;
    u32 peakResourceCount;
};

struct FrameResourceTrackerConfig {
    bool enabled = true;
    bool trackCallsites = false;  // Capture file:line (slower)
    u32  leakCheckInterval = 300; // Check for leaks every N frames
    u32  staleResourceFrames = 60;// Resource unused for N frames = warning
    u32  maxTrackedResources = 65536;
};

struct FrameResourceTrackerStats {
    u32 currentResourceCount;
    u64 currentBytesAlive;
    u32 allocationsThisFrame;
    u32 deallocationsThisFrame;
    u32 potentialLeaks;
    u32 staleResources;
    u64 peakBytesAlive;
    u32 peakResourceCount;
};

class FrameResourceTracker {
public:
    bool Init(const FrameResourceTrackerConfig& config = {});
    void Shutdown();

    // Track resource creation
    void OnResourceCreated(u64 handle, FrameResourceType type, u64 sizeBytes,
                            const std::string& debugName = "",
                            bool isTransient = false);

    // Track resource destruction
    void OnResourceDestroyed(u64 handle);

    // Mark a resource as used this frame
    void OnResourceUsed(u64 handle);

    // Per-frame update: snapshot + leak/stale checks
    void EndFrame(u32 frameIndex);

    // Get all potential leaks (created > N frames ago, never destroyed)
    std::vector<FrameTrackedResource> GetPotentialLeaks(u32 currentFrame, u32 minAge = 300) const;

    // Get stale resources (not used for N frames)
    std::vector<FrameTrackedResource> GetStaleResources(u32 currentFrame) const;

    // Get resource by handle
    const FrameTrackedResource* GetResource(u64 handle) const;

    // Get count by type
    u32 GetCountByType(FrameResourceType type) const;

    // Get frame snapshots (rolling history)
    const std::vector<FrameResourceSnapshot>& GetHistory() const { return m_history; }

    // Clear all tracking
    void Reset();

    FrameResourceTrackerStats GetStats() const;

private:
    FrameResourceTrackerConfig m_config;
    std::unordered_map<u64, FrameTrackedResource> m_resources;
    std::vector<FrameResourceSnapshot> m_history;

    u32 m_allocsThisFrame = 0;
    u32 m_deallocsThisFrame = 0;
    u64 m_bytesAllocThisFrame = 0;
    u64 m_bytesFreedThisFrame = 0;
    u64 m_currentBytesAlive = 0;
    u64 m_peakBytesAlive = 0;
    u32 m_peakResourceCount = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
