#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Sampler Feedback Manager ────────────────────────────────────────
// Tracks which texture pages/mips are actually sampled by the GPU,
// driving the virtual texture streaming system. Uses a GPU feedback
// buffer that shaders write to, then reads back results to determine
// residency requests.
//
// Flow:
//   1. Shaders write (textureID, mipLevel, pageXY) to feedback buffer
//   2. GPU compacts feedback via compute pass (dedup + sort)
//   3. CPU reads back compacted feedback
//   4. Streaming system uses feedback to prioritize page loads

struct FeedbackEntry {
    u32 textureId;
    u16 mipLevel;
    u16 pageX;
    u16 pageY;
    u16 priority;     // Based on screen coverage
};

struct ResidencyRequest {
    u32 textureId;
    u8  mipLevel;
    u16 pageX;
    u16 pageY;
    f32 screenCoverage;
    u32 frameRequested;
};

struct SamplerFeedbackConfig {
    u32 feedbackBufferSize = 65536;  // Max entries per frame
    u32 framesInFlight = 3;
    u32 maxTexturesTracked = 4096;
    u32 pageSize = 64;               // Virtual texture page size in texels
    f32 mipBias = 0.0f;              // Bias for mip level selection
};

struct SamplerFeedbackStats {
    u32 feedbackEntriesThisFrame;
    u32 uniqueRequests;
    u32 totalTexturesRequested;
    u32 totalPagesRequested;
    u32 compactionReductions;        // Entries removed by dedup
};

class SamplerFeedbackManager {
public:
    bool Init(IDevice* device, const SamplerFeedbackConfig& config = {});
    void Shutdown();

    // Get the feedback buffer handle for shader binding
    BufferHandle GetFeedbackBuffer() const { return m_feedbackBuffer; }

    // Get the atomic counter buffer for shader binding
    BufferHandle GetCounterBuffer() const { return m_counterBuffer; }

    // Clear feedback buffer at frame start
    void BeginFrame(ICommandList* cmd, u64 frameNumber);

    // Dispatch compaction compute pass after rendering
    void CompactFeedback(ICommandList* cmd);

    // Read back compacted results (call after GPU completes)
    void ReadbackResults();

    // Get residency requests for the streaming system
    std::vector<ResidencyRequest> GetResidencyRequests() const;

    // Get requests for a specific texture
    std::vector<ResidencyRequest> GetRequestsForTexture(u32 textureId) const;

    // Check if a specific page was requested this frame
    bool WasPageRequested(u32 textureId, u8 mipLevel, u16 pageX, u16 pageY) const;

    // Set mip bias (affects which mip levels are requested)
    void SetMipBias(f32 bias) { m_config.mipBias = bias; }

    SamplerFeedbackStats GetStats() const;

private:
    struct FrameData {
        BufferHandle readbackBuffer;
        u32          entryCount;
        bool         readbackReady;
    };

    IDevice* m_device = nullptr;
    SamplerFeedbackConfig m_config;

    BufferHandle m_feedbackBuffer;    // GPU-writable feedback buffer
    BufferHandle m_counterBuffer;     // Atomic counter for feedback writes
    BufferHandle m_compactedBuffer;   // After dedup/sort

    std::vector<FrameData> m_frames;
    u32 m_currentFrame = 0;
    u64 m_frameNumber = 0;

    std::vector<ResidencyRequest> m_currentRequests;
    std::unordered_map<u64, ResidencyRequest> m_requestMap; // hash -> request

    SamplerFeedbackStats m_stats{};
    mutable std::mutex m_mutex;

    u64 HashRequest(u32 textureId, u8 mipLevel, u16 pageX, u16 pageY) const;
};

} // namespace nge::rhi
