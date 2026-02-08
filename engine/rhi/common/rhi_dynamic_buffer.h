#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <mutex>

namespace nge::rhi {

// ─── GPU Dynamic Buffer Allocator ────────────────────────────────────────
// Ring-buffer for per-draw constant data (uniform/storage buffer offsets).
// Allocates from a large pre-allocated GPU buffer using a simple bump
// allocator that wraps around each frame.
//
// Usage:
//   auto alloc = dynBuf.Allocate(sizeof(PerDrawData), 256);
//   memcpy(alloc.mappedPtr, &drawData, sizeof(PerDrawData));
//   // Bind alloc.buffer at alloc.offset with dynamic offset

struct DynamicBufferConfig {
    u64 bufferSizeBytes = 16 * 1024 * 1024; // 16 MB default
    u32 framesInFlight = 3;
    u32 minAlignment = 256;                   // Vulkan minUniformBufferOffsetAlignment
};

struct DynamicAllocation {
    BufferHandle buffer;
    u64          offset;
    u64          size;
    void*        mappedPtr;    // Persistently mapped pointer + offset
    bool         valid = false;
};

struct DynamicBufferStats {
    u64 totalSize;
    u64 usedThisFrame;
    u64 peakUsage;
    u32 allocationsThisFrame;
    f32 utilizationPercent;
};

class DynamicBufferAllocator {
public:
    bool Init(IDevice* device, const DynamicBufferConfig& config = {});
    void Shutdown();

    // Begin a new frame — reset the ring region for this frame
    void BeginFrame(u32 frameIndex);

    // Allocate from the ring buffer (returns persistently mapped pointer)
    DynamicAllocation Allocate(u64 size, u64 alignment = 0);

    // Allocate and copy data in one call
    template<typename T>
    DynamicAllocation AllocateAndWrite(const T& data, u64 alignment = 0) {
        auto alloc = Allocate(sizeof(T), alignment);
        if (alloc.valid && alloc.mappedPtr) {
            memcpy(alloc.mappedPtr, &data, sizeof(T));
        }
        return alloc;
    }

    // Get the underlying buffer handle
    BufferHandle GetBuffer() const { return m_buffer; }

    DynamicBufferStats GetStats() const;

private:
    u64 AlignUp(u64 value, u64 alignment) const {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    IDevice* m_device = nullptr;
    DynamicBufferConfig m_config;

    BufferHandle m_buffer;
    void*        m_mappedBase = nullptr; // Persistently mapped

    // Per-frame ring regions
    struct FrameRegion {
        u64 start;
        u64 current;
        u64 end;
    };
    std::vector<FrameRegion> m_frameRegions;
    u32 m_currentFrame = 0;

    u64 m_peakUsage = 0;
    u32 m_allocationsThisFrame = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
