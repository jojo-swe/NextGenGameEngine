#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <mutex>

namespace nge::rhi {

// ─── Staging Buffer Manager ──────────────────────────────────────────────
// Batches CPU → GPU uploads into staging buffers, then copies them
// in a single command list submission. Avoids per-resource staging overhead.
//
// Usage:
//   staging.BeginFrame();
//   staging.StageBuffer(dstBuffer, 0, data, size);
//   staging.StageTexture(dstTex, 0, 0, width, height, pixels, pixelSize);
//   staging.Flush(cmd); // Records all copy commands

struct StagingBufferCopy {
    BufferHandle  dstBuffer;
    u32           dstOffset;
    u32           srcOffset; // Offset within staging buffer
    u32           size;
};

struct StagingTextureCopy {
    TextureHandle dstTexture;
    u32           mipLevel;
    u32           arrayLayer;
    u32           width;
    u32           height;
    u32           srcOffset; // Offset within staging buffer
    u32           size;
};

class StagingManager {
public:
    struct Config {
        u32 initialSize = 16 * 1024 * 1024; // 16 MB initial staging buffer
        u32 growFactor = 2;                   // Double when full
        u32 maxSize = 256 * 1024 * 1024;     // 256 MB max
    };

    // No default argument: Config's default member initializers cannot be
    // used in a default argument while the enclosing class is incomplete.
    bool Init(IDevice* device, const Config& config);
    bool Init(IDevice* device) { return Init(device, Config{}); }
    void Shutdown();

    // Call at frame start
    void BeginFrame();

    // Stage data for buffer upload
    bool StageBuffer(BufferHandle dst, u32 dstOffset, const void* data, u32 size);

    // Stage data for texture upload
    bool StageTexture(TextureHandle dst, u32 mipLevel, u32 arrayLayer,
                       u32 width, u32 height, const void* data, u32 size);

    // Record all staged copy commands into the command list
    void Flush(ICommandList* cmd);

    // Stats
    u32 GetPendingBufferCopies() const { return static_cast<u32>(m_bufferCopies.size()); }
    u32 GetPendingTextureCopies() const { return static_cast<u32>(m_textureCopies.size()); }
    u32 GetStagingUsed() const { return m_currentOffset; }
    u32 GetStagingCapacity() const { return m_currentSize; }

private:
    bool EnsureCapacity(u32 additionalBytes);
    void GrowStagingBuffer(u32 newSize);

    IDevice* m_device = nullptr;
    Config m_config;

    BufferHandle m_stagingBuffer;
    void* m_mappedPtr = nullptr;
    u32 m_currentSize = 0;
    u32 m_currentOffset = 0;

    std::vector<StagingBufferCopy> m_bufferCopies;
    std::vector<StagingTextureCopy> m_textureCopies;

    std::mutex m_mutex;
};

} // namespace nge::rhi
