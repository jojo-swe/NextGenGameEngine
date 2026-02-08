#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <mutex>

namespace nge::renderer {

// ─── Texture Streaming System ────────────────────────────────────────────
// Manages virtual texture pages and mip-level streaming based on GPU feedback.
// Uses a feedback buffer where shaders write the required mip level per tile.
//
// Pipeline:
//   1. GPU writes mip requests to feedback buffer during rendering
//   2. CPU reads back feedback buffer (1-2 frame latency)
//   3. Compute priority: requested mip vs loaded mip
//   4. Stream pages from disk into a page pool (LRU eviction)
//   5. Update indirection texture for virtual texturing

using StreamingTextureId = u32;
inline constexpr StreamingTextureId INVALID_STREAMING_TEX = UINT32_MAX;

// ─── Streaming Texture Descriptor ────────────────────────────────────────

struct StreamingTextureDesc {
    std::string path;           // Source file path
    u32         fullWidth;      // Full resolution width
    u32         fullHeight;     // Full resolution height
    u32         mipCount;       // Total mip levels
    rhi::Format format;
    u32         pageSize = 128; // Virtual page size in texels
};

// ─── Page Address ────────────────────────────────────────────────────────

struct PageAddress {
    StreamingTextureId textureId;
    u32 mipLevel;
    u32 pageX;
    u32 pageY;

    bool operator==(const PageAddress& o) const {
        return textureId == o.textureId && mipLevel == o.mipLevel &&
               pageX == o.pageX && pageY == o.pageY;
    }
};

struct PageAddressHash {
    usize operator()(const PageAddress& p) const {
        usize h = std::hash<u32>{}(p.textureId);
        h ^= std::hash<u32>{}(p.mipLevel) << 8;
        h ^= std::hash<u32>{}(p.pageX) << 16;
        h ^= std::hash<u32>{}(p.pageY) << 24;
        return h;
    }
};

// ─── Page Pool Entry ─────────────────────────────────────────────────────

struct PagePoolEntry {
    PageAddress address;
    u32         poolIndex;    // Physical tile index in the page pool
    u64         lastUsedFrame;
    bool        loaded;
};

// ─── Feedback Entry ──────────────────────────────────────────────────────
// Written by shaders: which texture/mip was needed at each screen tile

struct FeedbackEntry {
    u32 textureId;
    u32 requestedMip;
};

// ─── Texture Streamer ────────────────────────────────────────────────────

class TextureStreamer {
public:
    bool Init(rhi::IDevice* device, u32 pagePoolSize = 1024, u32 pageSize = 128);
    void Shutdown();

    // Register a streaming texture
    StreamingTextureId RegisterTexture(const StreamingTextureDesc& desc);
    void UnregisterTexture(StreamingTextureId id);

    // Per-frame update
    void Update(u64 frameIndex);

    // Read GPU feedback buffer and process requests
    void ProcessFeedback(const FeedbackEntry* feedbackData, u32 feedbackCount);

    // Get indirection texture (shader reads this to map virtual → physical)
    rhi::TextureHandle GetIndirectionTexture() const { return m_indirectionTex; }

    // Get physical page atlas
    rhi::TextureHandle GetPageAtlas() const { return m_pageAtlas; }

    // Stats
    u32 GetLoadedPageCount() const { return m_loadedPageCount; }
    u32 GetPendingPageCount() const { return static_cast<u32>(m_loadQueue.size()); }
    u32 GetPoolCapacity() const { return m_poolCapacity; }
    f32 GetPoolOccupancy() const { return static_cast<f32>(m_loadedPageCount) / static_cast<f32>(m_poolCapacity); }

private:
    struct StreamingTexture {
        StreamingTextureDesc desc;
        StreamingTextureId   id;
    };

    void LoadPage(const PageAddress& addr);
    void EvictLRU();
    u32  AllocatePoolSlot();
    void UpdateIndirection(const PageAddress& addr, u32 poolIndex);

    rhi::IDevice* m_device = nullptr;

    // Textures
    std::unordered_map<StreamingTextureId, StreamingTexture> m_textures;
    StreamingTextureId m_nextId = 0;

    // Page pool
    u32 m_poolCapacity = 0;
    u32 m_pageSize = 128;
    u32 m_loadedPageCount = 0;
    std::unordered_map<PageAddress, PagePoolEntry, PageAddressHash> m_loadedPages;
    std::vector<bool> m_poolSlotUsed;

    // Load queue (priority: lower mip = higher priority)
    struct LoadRequest {
        PageAddress addr;
        u32         priority; // Lower = more urgent

        bool operator>(const LoadRequest& o) const { return priority > o.priority; }
    };
    std::priority_queue<LoadRequest, std::vector<LoadRequest>, std::greater<LoadRequest>> m_loadQueue;

    // GPU resources
    rhi::TextureHandle m_pageAtlas;       // Physical page pool (2D array texture)
    rhi::TextureHandle m_indirectionTex;  // Virtual → physical mapping
    rhi::BufferHandle  m_feedbackBuffer;  // GPU feedback readback

    u64 m_currentFrame = 0;
    std::mutex m_mutex;

    static constexpr u32 MAX_LOADS_PER_FRAME = 8;
};

} // namespace nge::renderer
