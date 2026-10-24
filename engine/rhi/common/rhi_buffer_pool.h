#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <mutex>

namespace nge::rhi {

// ─── GPU Buffer Pool ─────────────────────────────────────────────────────
// Per-frame transient buffer allocator for GPU uploads.
// Uses a ring buffer strategy with frame-indexed regions.
// Avoids per-draw buffer creation/destruction overhead.
//
// Usage:
//   pool.BeginFrame(frameIndex);
//   auto alloc = pool.Allocate(sizeof(MyConstants));
//   memcpy(alloc.mappedPtr, &data, sizeof(data));
//   cmd->BindConstantBuffer(alloc.buffer, alloc.offset);
//   pool.EndFrame();

struct BufferPoolAllocation {
    BufferHandle buffer;
    u32          offset;
    u32          size;
    void*        mappedPtr; // CPU-visible mapped pointer for writing
};

class BufferPool {
public:
    struct Config {
        u32         blockSize = 4 * 1024 * 1024; // 4 MB per block
        u32         alignment = 256;               // Min allocation alignment
        BufferUsage usage = BufferUsage::Uniform | BufferUsage::TransferSrc;
        MemoryUsage memoryUsage = MemoryUsage::CPU_To_GPU;
        u32         maxFramesInFlight = 3;
    };

    // No default argument: Config's default member initializers cannot be
    // used in a default argument while the enclosing class is incomplete.
    bool Init(IDevice* device, const Config& config);
    bool Init(IDevice* device) { return Init(device, Config{}); }
    void Shutdown();

    // Call at frame boundaries
    void BeginFrame(u64 frameIndex);
    void EndFrame();

    // Allocate transient memory from the pool (thread-safe)
    BufferPoolAllocation Allocate(u32 size);

    // Reset the pool (free all blocks)
    void Reset();

    // Stats
    u32 GetBlockCount() const { return static_cast<u32>(m_blocks.size()); }
    u32 GetTotalAllocated() const { return m_totalAllocated; }
    u32 GetTotalCapacity() const { return static_cast<u32>(m_blocks.size()) * m_config.blockSize; }
    f32 GetUtilization() const;

private:
    struct Block {
        BufferHandle buffer;
        void*        mappedBase = nullptr;
        u32          currentOffset = 0;
        u64          lastUsedFrame = 0;
    };

    Block& GetOrCreateBlock();
    u32 AlignUp(u32 value, u32 alignment) const {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    IDevice*           m_device = nullptr;
    Config             m_config;
    std::vector<Block> m_blocks;
    u32                m_currentBlock = 0;
    u64                m_currentFrame = 0;
    u32                m_totalAllocated = 0;
    std::mutex         m_mutex;
};

} // namespace nge::rhi
