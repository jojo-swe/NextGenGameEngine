#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <mutex>

namespace nge::rhi {

// ─── GPU Upload Ring Buffer ──────────────────────────────────────────────
// Lock-free ring buffer for per-frame dynamic constant and structured
// buffer uploads. Each frame gets a region of a persistently mapped
// staging buffer. Data is written by CPU and consumed by GPU in the
// same frame (no readback).
//
// Triple-buffered: frame N writes to region N % framesInFlight.

struct UploadAllocation {
    void*  cpuAddress = nullptr;   // Mapped pointer for CPU writes
    u64    gpuOffset = 0;          // Offset into the upload buffer
    u32    size = 0;
    bool   valid = false;
};

class UploadRingBuffer {
public:
    struct Config {
        u64 sizePerFrame = 4 * 1024 * 1024; // 4 MB per frame
        u32 framesInFlight = 3;
        u32 alignment = 256;                  // CBV alignment
    };

    bool Init(IDevice* device, const Config& config = {});
    void Shutdown();

    // Per-frame reset — reclaims the oldest frame's region
    void BeginFrame(u32 frameIndex);

    // Allocate from current frame's region
    UploadAllocation Allocate(u32 sizeBytes, u32 alignment = 0);

    // Convenience: allocate and copy data in one call
    UploadAllocation Upload(const void* data, u32 sizeBytes, u32 alignment = 0);

    // Get the underlying GPU buffer (for binding)
    BufferHandle GetBuffer() const { return m_buffer; }

    // Stats
    u64 GetUsedThisFrame() const { return m_frameOffset; }
    u64 GetCapacityPerFrame() const { return m_config.sizePerFrame; }
    f32 GetUtilization() const;

private:
    IDevice* m_device = nullptr;
    Config m_config;

    BufferHandle m_buffer;
    u8* m_mappedPtr = nullptr;       // Persistently mapped

    u32 m_currentFrame = 0;
    u64 m_frameBaseOffset = 0;       // Start of current frame's region
    u64 m_frameOffset = 0;           // Current write position within frame

    std::mutex m_mutex;
};

} // namespace nge::rhi
