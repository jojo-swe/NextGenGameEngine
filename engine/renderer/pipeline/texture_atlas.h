#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::renderer {

// ─── GPU Texture Atlas Manager ───────────────────────────────────────────
// Dynamic atlas packing for UI sprites, decals, lightmaps, and other
// small textures. Uses a shelf-based packing algorithm for fast online
// insertion. Supports multiple atlas pages when a single page fills up.
//
// Features:
//   - Shelf-based bin packing (skyline algorithm)
//   - Multiple atlas pages (auto-expand)
//   - GPU upload via staging buffer
//   - UV rect queries for atlas lookups
//   - Defragmentation (repack)

struct AtlasRect {
    u32 x, y, width, height;
    u32 pageIndex;
    f32 u0, v0, u1, v1;    // Normalized UV coordinates
    bool valid = false;
};

struct AtlasConfig {
    u32     pageWidth = 4096;
    u32     pageHeight = 4096;
    u32     maxPages = 4;
    u32     padding = 1;         // Pixel padding between entries
    rhi::Format format = rhi::Format::RGBA8_UNORM;
    const char* debugName = "TextureAtlas";
};

struct AtlasStats {
    u32 pageCount;
    u32 entryCount;
    u64 totalPixels;
    u64 usedPixels;
    f32 occupancy;
};

class TextureAtlas {
public:
    bool Init(rhi::IDevice* device, const AtlasConfig& config = {});
    void Shutdown();

    // Insert a texture into the atlas. Returns UV rect for sampling.
    AtlasRect Insert(u32 width, u32 height, const void* pixelData = nullptr);

    // Remove an entry (marks space as free, does not immediately reclaim)
    void Remove(const AtlasRect& rect);

    // Upload pending changes to GPU
    void Upload(rhi::ICommandList* cmd);

    // Repack all entries to minimize wasted space
    void Repack();

    // Query
    rhi::TextureHandle GetPageTexture(u32 pageIndex) const;
    u32 GetPageCount() const { return static_cast<u32>(m_pages.size()); }

    AtlasStats GetStats() const;
    const AtlasConfig& GetConfig() const { return m_config; }

private:
    struct Shelf {
        u32 y;          // Top edge of the shelf
        u32 height;     // Shelf height (tallest entry)
        u32 usedWidth;  // How much width is occupied
    };

    struct Page {
        rhi::TextureHandle texture;
        std::vector<Shelf> shelves;
        u32 nextShelfY = 0;  // Y position for next new shelf
    };

    AtlasRect InsertInPage(u32 pageIndex, u32 width, u32 height);
    u32 CreatePage();

    rhi::IDevice* m_device = nullptr;
    AtlasConfig m_config;
    std::vector<Page> m_pages;

    // Pending uploads (CPU pixel data → GPU atlas)
    struct PendingUpload {
        u32 pageIndex;
        u32 x, y, width, height;
        std::vector<u8> pixelData;
    };
    std::vector<PendingUpload> m_pendingUploads;

    u32 m_entryCount = 0;
    u64 m_usedPixels = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::renderer
