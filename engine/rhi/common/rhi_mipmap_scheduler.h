#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Mipmap Generation Scheduler ─────────────────────────────────────
// Schedules and batches mipmap chain generation on async compute queues.
// Format-aware dispatch: selects correct downsampling shader per format
// (sRGB, HDR, depth, BC compressed, etc.).
//
// Use cases:
//   - Texture streaming: generate mips after upload
//   - Runtime render target mip generation (Hi-Z, bloom)
//   - Batch multiple textures into single dispatch for throughput
//   - Priority-based scheduling (visible textures first)

enum class MipFormat : u8 {
    RGBA8_UNORM,
    RGBA8_SRGB,
    RGBA16_FLOAT,
    RGBA32_FLOAT,
    R32_FLOAT,       // Depth / single-channel
    R16_FLOAT,
    RG16_FLOAT,
    BC1,             // Compressed (requires decompression step)
    BC3,
    BC5,
    BC7,
};

enum class MipFilter : u8 {
    Box,             // Simple 2x2 average
    Kaiser,          // Kaiser-windowed sinc (higher quality)
    Lanczos,         // Lanczos-3
    Min,             // Min filter (for Hi-Z)
    Max,             // Max filter (for max-Z)
};

enum class MipPriority : u8 {
    Immediate,       // Generate this frame
    High,            // Within 1-2 frames
    Normal,          // Queued, batched
    Low,             // Background, stream-in
};

struct MipGenRequest {
    u64         textureHandle;
    std::string debugName;
    u32         width;
    u32         height;
    u32         mipLevels;       // 0 = compute full chain
    u32         arrayLayers;     // 1 for 2D, >1 for array/cubemap
    MipFormat   format;
    MipFilter   filter;
    MipPriority priority;
    bool        useAsyncCompute;
};

struct MipGenJob {
    u64         textureHandle;
    u32         currentMip;      // Next mip level to generate
    u32         totalMips;
    u32         width;
    u32         height;
    u32         arrayLayers;
    MipFormat   format;
    MipFilter   filter;
    bool        completed;
    bool        dispatched;
};

struct MipSchedulerConfig {
    u32  maxPendingJobs = 256;
    u32  batchSize = 16;          // Max textures per dispatch batch
    u32  maxMipsPerFrame = 64;    // Max mip levels generated per frame
    bool preferAsyncCompute = true;
    bool enableSRGBCorrection = true;
};

struct MipSchedulerStats {
    u32 totalJobsSubmitted;
    u32 totalJobsCompleted;
    u32 pendingJobs;
    u32 mipsGeneratedThisFrame;
    u32 totalMipsGenerated;
    u32 asyncDispatches;
    u32 graphicsDispatches;
    u32 batchesDispatched;
};

class MipmapScheduler {
public:
    bool Init(const MipSchedulerConfig& config = {});
    void Shutdown();

    // Submit a mip generation request
    u64 Submit(const MipGenRequest& request);

    // Cancel a pending request
    bool Cancel(u64 textureHandle);

    // Process pending jobs (call once per frame)
    // Returns number of mip levels generated
    u32 ProcessFrame();

    // Mark a job as completed (after GPU fence signals)
    void MarkCompleted(u64 textureHandle);

    // Check if a texture has completed mip generation
    bool IsComplete(u64 textureHandle) const;

    // Get progress (0.0 - 1.0) for a texture
    f32 GetProgress(u64 textureHandle) const;

    // Get pending job count
    u32 GetPendingCount() const;

    // Compute required mip levels for a given resolution
    static u32 ComputeMipLevels(u32 width, u32 height);

    // Get dispatch group size for a mip level
    static void ComputeDispatchSize(u32 width, u32 height, u32 mipLevel,
                                     u32& groupsX, u32& groupsY);

    void Reset();

    MipSchedulerStats GetStats() const;

private:
    void SortByPriority();
    u32  DispatchBatch(std::vector<MipGenJob*>& batch);

    MipSchedulerConfig m_config;
    std::vector<MipGenJob> m_jobs;
    std::unordered_map<u64, u32> m_jobIndex; // textureHandle -> index

    u32 m_totalSubmitted = 0;
    u32 m_totalCompleted = 0;
    u32 m_totalMips = 0;
    u32 m_frameMips = 0;
    u32 m_asyncDispatches = 0;
    u32 m_graphicsDispatches = 0;
    u32 m_batchesDispatched = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
