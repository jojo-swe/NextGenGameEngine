#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Texture Streaming Priority Manager ──────────────────────────────
// Manages texture streaming priority based on screen coverage, distance,
// mip bias, and material importance. Produces ordered load/evict lists
// for the async texture streaming system.
//
// Use cases:
//   - Prioritize texture loads by screen-space importance
//   - Track per-texture mip residency
//   - Budget-aware streaming (total VRAM limit)
//   - Distance-based LOD bias for texture quality
//   - Material importance weighting (hero assets vs background)

struct StreamingTextureInfo {
    u32         textureId;
    u32         totalMipLevels;
    u32         residentMipLevel;   // Highest quality resident mip (0 = full res)
    u32         requestedMipLevel;  // Desired mip based on screen coverage
    u64         perMipSize;         // Approximate bytes per mip
    float       screenCoverage;     // Current screen-space coverage (0..1)
    float       distanceToCamera;
    float       importance;         // Material importance weight (default 1.0)
    u32         lastUsedFrame;
    std::string debugName;
};

enum class StreamAction : u8 {
    None,
    LoadHigherMip,    // Stream in a more detailed mip
    EvictToLowerMip,  // Drop to a lower quality mip
};

struct StreamCommand {
    u32          textureId;
    StreamAction action;
    u32          targetMipLevel;
    float        priority;
};

struct TextureStreamConfig {
    u32  maxTextures = 8192;
    u64  vramBudget = 512 * 1024 * 1024ULL;  // 512 MB default
    u32  maxLoadsPerFrame = 16;
    u32  maxEvictsPerFrame = 8;
    float distanceWeight = 1.0f;
    float coverageWeight = 2.0f;
    float importanceWeight = 1.5f;
    u32  evictionFrameThreshold = 60;  // Frames unused before eviction candidate
};

struct TextureStreamStats {
    u32 totalTextures;
    u64 totalVRAMUsed;
    u64 vramBudget;
    float budgetUtilization;
    u32 texturesAtFullRes;
    u32 texturesStreaming;
    u32 evictionCandidates;
    u32 totalLoadsIssued;
    u32 totalEvictsIssued;
};

class TextureStreamPriorityManager {
public:
    bool Init(const TextureStreamConfig& config = {});
    void Shutdown();

    // Register a texture for streaming
    u32 RegisterTexture(u32 totalMips, u64 perMipSize, float importance = 1.0f,
                         const std::string& name = "");

    // Update per-frame usage data
    void UpdateUsage(u32 textureId, float screenCoverage, float distance, u32 currentFrame);

    // Set the resident mip level (after load completes)
    void SetResidentMip(u32 textureId, u32 mipLevel);

    // Process frame: compute priorities and return stream commands
    std::vector<StreamCommand> ProcessFrame(u32 currentFrame);

    // Get texture info
    const StreamingTextureInfo* GetTextureInfo(u32 textureId) const;

    // Get current VRAM usage estimate
    u64 GetEstimatedVRAMUsage() const;

    // Unregister a texture
    void Unregister(u32 textureId);

    u32 GetTextureCount() const;

    void Reset();

    TextureStreamStats GetStats() const;

private:
    float ComputePriority(const StreamingTextureInfo& info) const;
    u32 ComputeDesiredMip(const StreamingTextureInfo& info) const;
    u64 EstimateVRAM() const;

    TextureStreamConfig m_config;
    std::unordered_map<u32, StreamingTextureInfo> m_textures;

    u32 m_nextId = 0;
    u32 m_totalLoads = 0;
    u32 m_totalEvicts = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
