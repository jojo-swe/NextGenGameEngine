#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Mipmap Generation Manager ───────────────────────────────────────
// Manages compute-based mipmap generation for textures. Batches mip
// generation requests, tracks per-texture mip state, and supports
// multiple downscale filters.
//
// Use cases:
//   - Generate mip chains for imported textures
//   - Runtime mip regeneration for dynamic textures (render targets)
//   - Batch mip generation to minimize dispatch overhead
//   - Track which textures need mip updates
//   - Support multiple filter modes (box, Kaiser, Lanczos)

enum class MipFilter : u8 {
    Box,           // Simple 2x2 average (fastest)
    Kaiser,        // Kaiser-windowed sinc (high quality)
    Lanczos,       // Lanczos resampling
    Linear,        // Bilinear interpolation
};

enum class MipGenState : u8 {
    UpToDate,
    Pending,       // Mip generation requested
    InProgress,    // Currently being generated
    Failed,
};

struct MipGenTextureInfo {
    u32          textureId;
    u32          width;
    u32          height;
    u32          mipLevels;
    u32          arrayLayers;
    MipFilter    filter;
    MipGenState  state;
    u32          lastGenFrame;
    bool         isDynamic;       // Needs regeneration each frame
    std::string  debugName;
};

struct MipGenRequest {
    u32 textureId;
    u32 baseMip;         // Start generating from this mip level
    u32 mipCount;        // Number of mips to generate (0 = all remaining)
    u32 arrayLayer;      // UINT32_MAX = all layers
};

struct MipGenConfig {
    u32       maxTextures = 1024;
    u32       maxRequestsPerFrame = 256;
    MipFilter defaultFilter = MipFilter::Box;
    bool      enableBatching = true;
};

struct MipGenStats {
    u32 totalTextures;
    u32 texturesPending;
    u32 texturesUpToDate;
    u32 totalMipsGenerated;
    u32 requestsThisFrame;
    u32 batchesDispatched;
    u32 dynamicTextures;
    u32 failedGenerations;
};

class MipmapGenManager {
public:
    bool Init(const MipGenConfig& config = {});
    void Shutdown();

    // Register a texture for mip management
    u32 RegisterTexture(u32 width, u32 height, u32 mipLevels, u32 arrayLayers = 1,
                         MipFilter filter = MipFilter::Box, bool isDynamic = false,
                         const std::string& name = "");

    // Request mip generation for a texture
    bool RequestGeneration(u32 textureId, u32 baseMip = 0, u32 mipCount = 0,
                            u32 arrayLayer = UINT32_MAX);

    // Mark a texture as needing mip update (e.g., after render-to-texture)
    void Invalidate(u32 textureId);

    // Process pending requests. Returns list of requests to dispatch.
    std::vector<MipGenRequest> ProcessFrame(u32 currentFrame);

    // Mark generation as complete
    void MarkComplete(u32 textureId);

    // Mark generation as failed
    void MarkFailed(u32 textureId);

    // Get texture info
    const MipGenTextureInfo* GetTextureInfo(u32 textureId) const;

    // Get mip level count for a texture
    u32 GetMipLevelCount(u32 textureId) const;

    // Calculate expected mip count from dimensions
    static u32 CalculateMipCount(u32 width, u32 height);

    // Get all textures needing mip generation
    std::vector<u32> GetPendingTextures() const;

    // Set filter for a texture
    void SetFilter(u32 textureId, MipFilter filter);

    // Unregister
    void Unregister(u32 textureId);

    u32 GetTextureCount() const;

    void Reset();

    MipGenStats GetStats() const;

private:
    MipGenConfig m_config;
    std::unordered_map<u32, MipGenTextureInfo> m_textures;
    std::vector<MipGenRequest> m_pendingRequests;

    u32 m_nextId = 0;
    u32 m_totalMipsGenerated = 0;
    u32 m_requestsThisFrame = 0;
    u32 m_batchesDispatched = 0;
    u32 m_failedGenerations = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
