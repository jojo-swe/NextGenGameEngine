#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::rhi {

// ─── GPU Dynamic Buffer Ring Allocator ───────────────────────────────────
// Lock-free ring buffer allocator for per-frame dynamic GPU data (uniform
// buffers, vertex uploads, staging). Manages a fixed-size buffer with
// frame-based fencing to reclaim space after GPU completes.
//
// Use cases:
//   - Per-frame constant buffer uploads (camera, transforms)
//   - Dynamic vertex/index buffer streaming
//   - Staging buffer for texture uploads
//   - Avoids per-frame buffer creation overhead
//   - Frame-based reclamation with configurable frame latency

struct RingAllocation {
    u64  offset;       // Byte offset into the ring buffer
    u64  size;         // Allocation size in bytes
    u64  alignedSize;  // Size after alignment padding
    u32  frameIndex;   // Frame this allocation belongs to
    bool valid;
};

struct RingAllocatorConfig {
    u64  bufferSize = 16 * 1024 * 1024;  // 16 MB default
    u64  alignment = 256;                 // Uniform buffer alignment (Vulkan min)
    u32  maxFramesInFlight = 3;
};

struct RingAllocatorStats {
    u64 bufferSize;
    u64 usedBytes;
    u64 freeBytes;
    u64 peakUsedBytes;
    u32 totalAllocations;
    u32 totalFramesCompleted;
    u32 allocationsFailed;
    float utilization;
};

class DynamicRingAllocator {
public:
    bool Init(const RingAllocatorConfig& config = {});
    void Shutdown();

    // Allocate from the ring buffer. Returns allocation with offset.
    RingAllocation Allocate(u64 sizeBytes);

    // Signal that a frame's GPU work is complete; reclaim its allocations.
    void FrameCompleted(u32 frameIndex);

    // Begin a new frame (advances internal frame counter)
    void BeginFrame();

    // Get current write offset
    u64 GetWriteOffset() const;

    // Get available space
    u64 GetFreeSpace() const;

    // Get used space
    u64 GetUsedSpace() const;

    // Check if an allocation of given size would succeed
    bool CanAllocate(u64 sizeBytes) const;

    // Get current frame index
    u32 GetCurrentFrame() const;

    void Reset();

    RingAllocatorStats GetStats() const;

private:
    u64 AlignUp(u64 value, u64 alignment) const;

    RingAllocatorConfig m_config;

    u64 m_head = 0;          // Write position
    u64 m_tail = 0;          // Oldest unconsumed position
    u32 m_currentFrame = 0;

    // Per-frame tracking: how much was allocated each frame
    struct FrameAllocation {
        u64 startOffset;
        u64 endOffset;
        u32 frameIndex;
    };
    std::vector<FrameAllocation> m_frameAllocations;

    u64 m_peakUsed = 0;
    u32 m_totalAllocations = 0;
    u32 m_totalFramesCompleted = 0;
    u32 m_allocationsFailed = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
