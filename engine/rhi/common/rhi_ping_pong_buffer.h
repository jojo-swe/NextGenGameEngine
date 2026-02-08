#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::rhi {

// ─── GPU Multi-Buffer Ping-Pong Manager ──────────────────────────────────
// Manages double/triple-buffered resources for GPU read-back-free streaming.
// While the GPU reads from buffer N, the CPU writes to buffer N+1.
// No GPU stalls, no readback — pure parallel execution.
//
// Use cases:
//   - Per-frame uniform buffers (view/proj matrices)
//   - GPU scene buffer (instance data streaming)
//   - Particle simulation (read previous, write current)
//   - Any producer-consumer GPU resource

enum class PingPongMode : u8 {
    Double = 2,
    Triple = 3,
};

struct PingPongBufferDesc {
    u64            sizeBytes;
    PingPongMode   mode = PingPongMode::Triple;
    bool           hostVisible = true;      // For CPU-writable buffers
    bool           storageBuffer = false;    // GPU read/write (compute)
    std::string    debugName;
};

struct PingPongTextureDesc {
    u32            width;
    u32            height;
    u32            format;
    PingPongMode   mode = PingPongMode::Double;
    std::string    debugName;
};

struct PingPongStats {
    u32 totalPingPongSets;
    u32 totalBuffers;
    u64 totalMemoryBytes;
    u32 currentFrameIndex;
};

class PingPongBufferManager {
public:
    bool Init(IDevice* device);
    void Shutdown();

    // Create a ping-pong buffer set
    u32 CreateBufferSet(const PingPongBufferDesc& desc);

    // Create a ping-pong texture set (for compute read/write patterns)
    u32 CreateTextureSet(const PingPongTextureDesc& desc);

    // Destroy a set
    void DestroySet(u32 setId);

    // Get the current write buffer (CPU writes here)
    BufferHandle GetWriteBuffer(u32 setId) const;

    // Get the current read buffer (GPU reads from here — previous frame's data)
    BufferHandle GetReadBuffer(u32 setId) const;

    // Get the write texture (GPU writes here)
    TextureHandle GetWriteTexture(u32 setId) const;

    // Get the read texture (GPU reads from here)
    TextureHandle GetReadTexture(u32 setId) const;

    // Get mapped pointer for the current write buffer (if host-visible)
    void* GetWritePtr(u32 setId) const;

    // Advance to next frame (rotates buffers)
    void Advance(u64 frameNumber);

    // Get the current frame's buffer index (for manual indexing)
    u32 GetCurrentIndex() const { return m_currentIndex; }
    u32 GetPreviousIndex(u32 setId) const;

    PingPongStats GetStats() const;

private:
    struct BufferSet {
        PingPongMode           mode;
        std::vector<BufferHandle>  buffers;
        std::vector<TextureHandle> textures;
        std::vector<void*>     mappedPtrs;
        u64                    sizeBytes;
        std::string            debugName;
        bool                   isTexture;
        bool                   alive = true;
    };

    IDevice* m_device = nullptr;
    std::vector<BufferSet> m_sets;
    u32 m_currentIndex = 0;
    u64 m_frameNumber = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
