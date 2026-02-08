#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>

namespace nge::renderer {

// ─── Virtual Shadow Maps ─────────────────────────────────────────────────
// Clipmap-based virtual shadow map per directional light.
//   - 16K×16K virtual shadow map divided into 128×128 pages
//   - Only allocate pages visible to camera
//   - Cache pages across frames, invalidate on geometry change
//   - Point/spot: cubemap virtual shadow maps

struct ShadowPage {
    u32   x, y;                // Page coordinates in virtual shadow map
    u32   clipmapLevel;        // Which clipmap level
    bool  allocated;
    bool  dirty;               // Needs re-render
    u64   lastUsedFrame;
};

struct VirtualShadowMapConfig {
    u32 virtualResolution    = 16384; // 16K virtual resolution
    u32 pageSize             = 128;   // 128×128 pixels per page
    u32 physicalTilePoolSize = 4096;  // Max physical tiles in pool
    u32 clipmapLevels        = 16;    // Number of clipmap levels
    u32 maxShadowMaps        = 64;    // Max simultaneous shadow maps (lights)
};

class VirtualShadowMapSystem {
public:
    bool Init(rhi::IDevice* device, const VirtualShadowMapConfig& config = {});
    void Shutdown();

    // Per-frame: determine which pages are needed, allocate, render dirty pages
    void Update(rhi::ICommandList* cmd,
                const math::Vec3& cameraPos,
                const math::Vec3& sunDirection,
                const math::Mat4& viewProj,
                u32 screenWidth, u32 screenHeight);

    // Render shadow casters into dirty pages
    void RenderPages(rhi::ICommandList* cmd);

    // GPU resources for shader sampling
    rhi::TextureHandle GetShadowAtlas() const { return m_shadowAtlas; }
    rhi::BufferHandle  GetPageTable() const { return m_pageTableBuffer; }

    // Stats
    u32 GetAllocatedPages() const { return m_allocatedPageCount; }
    u32 GetDirtyPages() const { return m_dirtyPageCount; }

private:
    // Determine visible pages based on camera frustum
    void MarkVisiblePages(const math::Vec3& cameraPos,
                           const math::Mat4& viewProj,
                           u32 screenWidth, u32 screenHeight);

    // Allocate physical tiles for needed virtual pages (LRU eviction)
    void AllocatePhysicalTiles();

    // Build shadow projection matrices for each clipmap level
    math::Mat4 ComputeClipmapProjection(u32 level,
                                          const math::Vec3& cameraPos,
                                          const math::Vec3& sunDirection);

    rhi::IDevice* m_device = nullptr;
    VirtualShadowMapConfig m_config;

    // Physical shadow atlas texture
    rhi::TextureHandle m_shadowAtlas;      // D32_FLOAT, tiled

    // Page table: maps virtual pages → physical tile coordinates
    rhi::BufferHandle  m_pageTableBuffer;  // GPU buffer for shader access

    // CPU-side page tracking
    std::vector<ShadowPage> m_pages;
    std::vector<u32>        m_physicalTilePool; // Free list of physical tiles

    u32 m_allocatedPageCount = 0;
    u32 m_dirtyPageCount     = 0;
    u64 m_currentFrame       = 0;

    // Per-clipmap level projection matrices
    std::vector<math::Mat4> m_clipmapProjections;

    // Pipeline for rendering shadow casters
    rhi::PipelineHandle m_shadowRasterPipeline;
};

} // namespace nge::renderer
