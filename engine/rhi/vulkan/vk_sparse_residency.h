#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <mutex>

namespace nge::rhi::vulkan {

// ─── Vulkan Sparse Residency Manager ─────────────────────────────────────
// Manages sparse (partially resident) textures via VK_EXT_sparse_residency.
// Provides GPU feedback-driven page table management for virtual texturing.
//
// Architecture:
//   1. GPU writes requested page IDs to a feedback buffer during rendering
//   2. CPU reads back feedback, determines which pages to make resident
//   3. Sparse bind operations map physical memory to virtual pages
//   4. LRU eviction when physical memory budget is exceeded
//
// Used by:
//   - Virtual texture streaming system
//   - Megatexture / terrain splatting
//   - Large environment textures

struct SparsePageId {
    u32 textureId;
    u16 mipLevel;
    u16 layerIndex;
    u32 pageX;
    u32 pageY;

    bool operator==(const SparsePageId& other) const {
        return textureId == other.textureId && mipLevel == other.mipLevel &&
               layerIndex == other.layerIndex && pageX == other.pageX && pageY == other.pageY;
    }
};

struct SparsePageIdHash {
    size_t operator()(const SparsePageId& id) const {
        size_t h = std::hash<u32>()(id.textureId);
        h ^= std::hash<u16>()(id.mipLevel) << 1;
        h ^= std::hash<u16>()(id.layerIndex) << 2;
        h ^= std::hash<u32>()(id.pageX) << 3;
        h ^= std::hash<u32>()(id.pageY) << 4;
        return h;
    }
};

enum class PageState : u8 {
    NonResident,
    Pending,       // Bind requested, not yet committed
    Resident,
    Evicting,
};

struct SparsePage {
    SparsePageId id;
    PageState    state;
    u64          physicalOffset;  // Offset in physical memory pool
    u64          lastAccessFrame;
    u32          accessCount;
};

struct SparseTextureInfo {
    u32         textureId;
    u64         handle;          // VkImage
    u32         width;
    u32         height;
    u32         mipLevels;
    u32         layers;
    u32         format;
    u32         pageWidth;       // Sparse block extent X (e.g., 128)
    u32         pageHeight;      // Sparse block extent Y (e.g., 128)
    u32         pagesX;          // Total pages in X
    u32         pagesY;          // Total pages in Y
    std::string debugName;
};

struct SparseResidencyConfig {
    u64 physicalMemoryBudget = 512 * 1024 * 1024; // 512 MB
    u32 maxPendingBinds = 256;                      // Per frame
    u32 feedbackBufferSize = 65536;                 // Max page requests per frame
    u32 evictionBatchSize = 64;                     // Pages to evict per frame
    u32 minResidentMip = 4;                         // Always keep top N mips resident
};

struct SparseResidencyStats {
    u32 totalTextures;
    u32 totalPages;
    u32 residentPages;
    u32 pendingBinds;
    u32 evictionsThisFrame;
    u64 physicalMemoryUsed;
    u64 physicalMemoryBudget;
    f32 residencyPercent;
    u32 feedbackRequestsThisFrame;
};

class SparseResidencyManager {
public:
    bool Init(IDevice* device, const SparseResidencyConfig& config = {});
    void Shutdown();

    // Register a sparse texture
    u32 RegisterTexture(const SparseTextureInfo& info);

    // Unregister and release all pages
    void UnregisterTexture(u32 textureId);

    // Process GPU feedback buffer (called after readback)
    void ProcessFeedback(const SparsePageId* requests, u32 requestCount, u64 frameNumber);

    // Execute pending sparse bind operations
    void ExecuteBinds();

    // Evict least-recently-used pages to stay within budget
    u32 EvictLRU(u32 maxEvictions = 0);

    // Force a specific page to be resident
    void RequestPage(const SparsePageId& pageId);

    // Check if a page is resident
    bool IsResident(const SparsePageId& pageId) const;

    // Get page state
    PageState GetPageState(const SparsePageId& pageId) const;

    // Get all resident pages for a texture
    std::vector<SparsePageId> GetResidentPages(u32 textureId) const;

    // Get texture info
    const SparseTextureInfo* GetTextureInfo(u32 textureId) const;

    // Per-frame update
    void BeginFrame(u64 frameNumber);

    SparseResidencyStats GetStats() const;

private:
    struct PhysicalBlock {
        u64  offset;
        u64  size;
        bool free;
    };

    u64 AllocatePhysicalMemory(u64 size);
    void FreePhysicalMemory(u64 offset);
    void MakeResident(const SparsePageId& pageId);
    void MakeNonResident(const SparsePageId& pageId);

    IDevice* m_device = nullptr;
    SparseResidencyConfig m_config;

    std::unordered_map<u32, SparseTextureInfo> m_textures;
    std::unordered_map<SparsePageId, SparsePage, SparsePageIdHash> m_pages;
    std::vector<SparsePageId> m_pendingBinds;
    std::vector<PhysicalBlock> m_physicalBlocks;

    u64 m_physicalMemoryUsed = 0;
    u64 m_currentFrame = 0;
    u32 m_evictionsThisFrame = 0;
    u32 m_feedbackRequestsThisFrame = 0;
    u32 m_nextTextureId = 1;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi::vulkan
