#include "engine/renderer/shadows/virtual_shadow_map.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"
#include <algorithm>

namespace nge::renderer {

bool VirtualShadowMapSystem::Init(rhi::IDevice* device, const VirtualShadowMapConfig& config) {
    m_device = device;
    m_config = config;

    // Create physical shadow atlas
    // Each physical tile is config.pageSize × config.pageSize
    // Arrange tiles in a square atlas texture
    u32 tilesPerRow = static_cast<u32>(math::Ceil(math::Sqrt(static_cast<f32>(config.physicalTilePoolSize))));
    u32 atlasRes = tilesPerRow * config.pageSize;

    rhi::TextureDesc atlasDesc{};
    atlasDesc.width   = atlasRes;
    atlasDesc.height  = atlasRes;
    atlasDesc.format  = rhi::Format::D32_FLOAT;
    atlasDesc.usage   = rhi::TextureUsage::DepthStencil | rhi::TextureUsage::ShaderRead;
    atlasDesc.debugName = "VSM_ShadowAtlas";
    m_shadowAtlas = device->CreateTexture(atlasDesc);

    // Page table buffer: one entry per virtual page (level × pagesX × pagesY)
    u32 pagesPerDim = config.virtualResolution / config.pageSize;
    u32 totalVirtualPages = 0;
    for (u32 level = 0; level < config.clipmapLevels; ++level) {
        u32 levelPages = pagesPerDim >> level;
        if (levelPages < 1) levelPages = 1;
        totalVirtualPages += levelPages * levelPages;
    }

    rhi::BufferDesc pageTableDesc;
    pageTableDesc.size        = sizeof(u32) * 2 * totalVirtualPages; // (physX, physY) per page
    pageTableDesc.usage       = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;
    pageTableDesc.memoryUsage = rhi::MemoryUsage::GPU_Only;
    pageTableDesc.debugName   = "VSM_PageTable";
    m_pageTableBuffer = device->CreateBuffer(pageTableDesc);

    // Initialize physical tile free list
    m_physicalTilePool.resize(config.physicalTilePoolSize);
    for (u32 i = 0; i < config.physicalTilePoolSize; ++i) {
        m_physicalTilePool[i] = i;
    }

    m_clipmapProjections.resize(config.clipmapLevels);

    NGE_LOG_INFO("Virtual shadow maps initialized: {}x{} atlas, {} physical tiles, {} clipmap levels",
                 atlasRes, atlasRes, config.physicalTilePoolSize, config.clipmapLevels);
    return true;
}

void VirtualShadowMapSystem::Shutdown() {
    if (!m_device) return;

    if (m_shadowAtlas.IsValid()) m_device->DestroyTexture(m_shadowAtlas);
    if (m_pageTableBuffer.IsValid()) m_device->DestroyBuffer(m_pageTableBuffer);

    m_pages.clear();
    m_physicalTilePool.clear();
}

void VirtualShadowMapSystem::Update(rhi::ICommandList* cmd,
                                      const math::Vec3& cameraPos,
                                      const math::Vec3& sunDirection,
                                      const math::Mat4& viewProj,
                                      u32 screenWidth, u32 screenHeight) {
    m_currentFrame++;

    // Compute clipmap projection matrices
    for (u32 level = 0; level < m_config.clipmapLevels; ++level) {
        m_clipmapProjections[level] = ComputeClipmapProjection(level, cameraPos, sunDirection);
    }

    // Determine which virtual pages are needed
    MarkVisiblePages(cameraPos, viewProj, screenWidth, screenHeight);

    // Allocate physical tiles for needed pages
    AllocatePhysicalTiles();

    // Render dirty pages
    RenderPages(cmd);

    // Upload page table to GPU
    // TODO: Upload m_pageTableBuffer with virtual→physical mapping
}

void VirtualShadowMapSystem::RenderPages(rhi::ICommandList* cmd) {
    if (m_dirtyPageCount == 0) return;

    cmd->BeginDebugLabel("VSM Render Pages", 0.3f, 0.3f, 0.3f);

    // For each dirty page, set viewport to its physical tile location
    // and render shadow casters using the appropriate clipmap projection
    for (auto& page : m_pages) {
        if (!page.dirty || !page.allocated) continue;

        // Compute physical tile location in atlas
        // TODO: Look up physical tile coordinates from allocation
        // Set viewport, scissor, bind shadow pipeline, draw

        page.dirty = false;
    }

    m_dirtyPageCount = 0;
    cmd->EndDebugLabel();
}

void VirtualShadowMapSystem::MarkVisiblePages(const math::Vec3& /*cameraPos*/,
                                                const math::Mat4& /*viewProj*/,
                                                u32 /*screenWidth*/, u32 /*screenHeight*/) {
    // Determine which shadow pages are visible from the camera's perspective
    // A page is needed if:
    //   1. It covers geometry visible to the camera
    //   2. It's within the appropriate clipmap level based on distance

    // For each clipmap level:
    //   - Compute world-space bounds of each page
    //   - Test against camera frustum
    //   - Mark needed pages

    // TODO: GPU-driven page marking via compute shader reading visibility buffer
    // For now, mark center pages of each level as needed (stub)

    m_dirtyPageCount = 0;
    u32 pagesPerDim = m_config.virtualResolution / m_config.pageSize;

    for (u32 level = 0; level < m_config.clipmapLevels; ++level) {
        u32 levelPages = pagesPerDim >> level;
        if (levelPages < 1) levelPages = 1;

        // Mark center 4×4 pages as needed
        u32 center = levelPages / 2;
        u32 halfExtent = math::Min(2u, center);

        for (u32 y = center - halfExtent; y < center + halfExtent && y < levelPages; ++y) {
            for (u32 x = center - halfExtent; x < center + halfExtent && x < levelPages; ++x) {
                ShadowPage page;
                page.x = x;
                page.y = y;
                page.clipmapLevel = level;
                page.allocated = false;
                page.dirty = true;
                page.lastUsedFrame = m_currentFrame;
                m_pages.push_back(page);
                m_dirtyPageCount++;
            }
        }
    }
}

void VirtualShadowMapSystem::AllocatePhysicalTiles() {
    m_allocatedPageCount = 0;

    for (auto& page : m_pages) {
        if (!page.dirty) continue;

        if (!m_physicalTilePool.empty()) {
            // Allocate from free list
            u32 tile = m_physicalTilePool.back();
            m_physicalTilePool.pop_back();
            page.allocated = true;
            m_allocatedPageCount++;
            (void)tile; // TODO: store mapping
        } else {
            // Evict oldest page (LRU)
            // Find oldest allocated page
            auto oldest = std::min_element(m_pages.begin(), m_pages.end(),
                [](const ShadowPage& a, const ShadowPage& b) {
                    if (!a.allocated) return false;
                    if (!b.allocated) return true;
                    return a.lastUsedFrame < b.lastUsedFrame;
                });

            if (oldest != m_pages.end() && oldest->allocated &&
                oldest->lastUsedFrame < m_currentFrame - 2) {
                oldest->allocated = false;
                page.allocated = true;
                m_allocatedPageCount++;
            }
        }
    }
}

math::Mat4 VirtualShadowMapSystem::ComputeClipmapProjection(u32 level,
                                                               const math::Vec3& cameraPos,
                                                               const math::Vec3& sunDirection) {
    // Each clipmap level covers a larger area around the camera
    // Level 0: covers ±10m, Level 1: ±20m, etc. (doubling each level)
    f32 halfExtent = 10.0f * static_cast<f32>(1u << level);

    // Light-space: look from sun direction at camera position
    math::Vec3 lightUp = math::Abs(sunDirection.y) > 0.99f
        ? math::Vec3(0, 0, 1)
        : math::Vec3(0, 1, 0);

    math::Vec3 lightRight = lightUp.Cross(sunDirection).Normalized();
    lightUp = sunDirection.Cross(lightRight).Normalized();

    // Snap to texel grid to prevent shadow shimmering
    f32 texelSize = (halfExtent * 2.0f) / static_cast<f32>(m_config.virtualResolution >> level);

    math::Vec3 snappedCenter = cameraPos;
    f32 dotRight = snappedCenter.Dot(lightRight);
    f32 dotUp = snappedCenter.Dot(lightUp);
    dotRight = math::Floor(dotRight / texelSize) * texelSize;
    dotUp = math::Floor(dotUp / texelSize) * texelSize;
    snappedCenter = lightRight * dotRight + lightUp * dotUp +
                    sunDirection * snappedCenter.Dot(sunDirection);

    // Orthographic projection
    return math::Mat4::Ortho(
        -halfExtent, halfExtent,
        -halfExtent, halfExtent,
        -halfExtent * 4.0f, halfExtent * 4.0f
    );
}

} // namespace nge::renderer
