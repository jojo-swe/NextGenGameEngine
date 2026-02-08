#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <functional>
#include <mutex>

namespace nge::rhi {

// ─── GPU Readback Ring Buffer ────────────────────────────────────────────
// Manages async GPU → CPU data transfers with frame-latency-aware readback.
// Uses a ring buffer of readback slots to avoid stalling the GPU.
//
// Usage:
//   u32 slot = readback.Submit(cmd, srcBuffer, offset, size, callback);
//   // ... N frames later ...
//   readback.Poll(frameIndex); // Invokes callbacks for completed reads

class ReadbackRingBuffer {
public:
    using ReadbackCallback = std::function<void(const void* data, u32 size)>;

    struct Config {
        u32 ringSize = 64;              // Max in-flight readback requests
        u32 maxReadbackSize = 64 * 1024; // Max single readback (64 KB)
        u32 framesInFlight = 3;         // GPU latency before readback is safe
    };

    bool Init(IDevice* device, const Config& config = {});
    void Shutdown();

    // Submit a readback request (copies GPU buffer region to staging, calls callback when ready)
    u32 Submit(ICommandList* cmd, BufferHandle srcBuffer, u32 srcOffset, u32 size,
               ReadbackCallback callback);

    // Submit texture readback (single mip, single layer)
    u32 SubmitTexture(ICommandList* cmd, TextureHandle srcTexture, u32 mipLevel,
                       u32 width, u32 height, u32 bytesPerPixel, ReadbackCallback callback);

    // Poll completed readbacks (call once per frame)
    void Poll(u64 currentFrame);

    // Stats
    u32 GetPendingCount() const;
    u32 GetCompletedCount() const { return m_completedCount; }

private:
    struct ReadbackSlot {
        BufferHandle    stagingBuffer;
        u32             size = 0;
        u64             submitFrame = 0;
        ReadbackCallback callback;
        bool            active = false;
    };

    IDevice*                m_device = nullptr;
    Config                  m_config;
    std::vector<ReadbackSlot> m_slots;
    u32                     m_nextSlot = 0;
    u64                     m_currentFrame = 0;
    u32                     m_completedCount = 0;
    std::mutex              m_mutex;
};

} // namespace nge::rhi
