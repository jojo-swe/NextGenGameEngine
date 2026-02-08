#include "engine/renderer/pipeline/texture_atlas.h"
#include "engine/core/logging/log.h"
#include <cstring>
#include <algorithm>

namespace nge::renderer {

bool TextureAtlas::Init(rhi::IDevice* device, const AtlasConfig& config) {
    m_device = device;
    m_config = config;
    m_entryCount = 0;
    m_usedPixels = 0;

    m_pages.reserve(config.maxPages);

    NGE_LOG_INFO("Texture atlas initialized: {}x{}, max {} pages, format {}",
                 config.pageWidth, config.pageHeight, config.maxPages,
                 static_cast<u32>(config.format));
    return true;
}

void TextureAtlas::Shutdown() {
    for (auto& page : m_pages) {
        if (page.texture.IsValid()) {
            m_device->DestroyTexture(page.texture);
        }
    }
    m_pages.clear();
    m_pendingUploads.clear();
    m_entryCount = 0;
    m_usedPixels = 0;
}

AtlasRect TextureAtlas::Insert(u32 width, u32 height, const void* pixelData) {
    std::lock_guard lock(m_mutex);

    u32 paddedW = width + m_config.padding * 2;
    u32 paddedH = height + m_config.padding * 2;

    // Try existing pages
    for (u32 i = 0; i < static_cast<u32>(m_pages.size()); ++i) {
        auto rect = InsertInPage(i, paddedW, paddedH);
        if (rect.valid) {
            // Adjust rect to exclude padding
            rect.x += m_config.padding;
            rect.y += m_config.padding;
            rect.width = width;
            rect.height = height;

            // Calculate UVs
            f32 invW = 1.0f / static_cast<f32>(m_config.pageWidth);
            f32 invH = 1.0f / static_cast<f32>(m_config.pageHeight);
            rect.u0 = static_cast<f32>(rect.x) * invW;
            rect.v0 = static_cast<f32>(rect.y) * invH;
            rect.u1 = static_cast<f32>(rect.x + width) * invW;
            rect.v1 = static_cast<f32>(rect.y + height) * invH;

            m_entryCount++;
            m_usedPixels += static_cast<u64>(width) * height;

            // Queue pixel data upload
            if (pixelData) {
                PendingUpload upload;
                upload.pageIndex = rect.pageIndex;
                upload.x = rect.x;
                upload.y = rect.y;
                upload.width = width;
                upload.height = height;
                u32 bpp = 4; // Assume 4 bytes per pixel for RGBA8
                u64 dataSize = static_cast<u64>(width) * height * bpp;
                upload.pixelData.resize(dataSize);
                std::memcpy(upload.pixelData.data(), pixelData, dataSize);
                m_pendingUploads.push_back(std::move(upload));
            }

            return rect;
        }
    }

    // Create new page
    if (m_pages.size() < m_config.maxPages) {
        u32 pageIdx = CreatePage();
        auto rect = InsertInPage(pageIdx, paddedW, paddedH);
        if (rect.valid) {
            rect.x += m_config.padding;
            rect.y += m_config.padding;
            rect.width = width;
            rect.height = height;

            f32 invW = 1.0f / static_cast<f32>(m_config.pageWidth);
            f32 invH = 1.0f / static_cast<f32>(m_config.pageHeight);
            rect.u0 = static_cast<f32>(rect.x) * invW;
            rect.v0 = static_cast<f32>(rect.y) * invH;
            rect.u1 = static_cast<f32>(rect.x + width) * invW;
            rect.v1 = static_cast<f32>(rect.y + height) * invH;

            m_entryCount++;
            m_usedPixels += static_cast<u64>(width) * height;

            if (pixelData) {
                PendingUpload upload;
                upload.pageIndex = rect.pageIndex;
                upload.x = rect.x;
                upload.y = rect.y;
                upload.width = width;
                upload.height = height;
                u32 bpp = 4;
                u64 dataSize = static_cast<u64>(width) * height * bpp;
                upload.pixelData.resize(dataSize);
                std::memcpy(upload.pixelData.data(), pixelData, dataSize);
                m_pendingUploads.push_back(std::move(upload));
            }

            return rect;
        }
    }

    NGE_LOG_WARN("Texture atlas full: cannot insert {}x{}", width, height);
    return {};
}

void TextureAtlas::Remove(const AtlasRect& rect) {
    std::lock_guard lock(m_mutex);
    if (!rect.valid) return;

    // Simple approach: decrement counts. Space is not reclaimed until Repack().
    if (m_entryCount > 0) m_entryCount--;
    m_usedPixels -= static_cast<u64>(rect.width) * rect.height;
}

void TextureAtlas::Upload(rhi::ICommandList* cmd) {
    std::lock_guard lock(m_mutex);

    for (const auto& upload : m_pendingUploads) {
        if (upload.pageIndex >= m_pages.size()) continue;

        // TODO: Copy pixel data to staging buffer, then CopyBufferToImage
        // cmd->CopyBufferToTexture(stagingBuffer, stagingOffset,
        //     m_pages[upload.pageIndex].texture,
        //     upload.x, upload.y, 0, upload.width, upload.height, 1, 0, 0);
        (void)cmd;
    }

    m_pendingUploads.clear();
}

void TextureAtlas::Repack() {
    std::lock_guard lock(m_mutex);
    // TODO: Collect all live entries, sort by height descending, re-insert
    // This would require tracking all live entries with their pixel data
    NGE_LOG_DEBUG("Texture atlas repack requested (not yet implemented)");
}

rhi::TextureHandle TextureAtlas::GetPageTexture(u32 pageIndex) const {
    std::lock_guard lock(m_mutex);
    if (pageIndex < m_pages.size()) {
        return m_pages[pageIndex].texture;
    }
    return {};
}

AtlasStats TextureAtlas::GetStats() const {
    std::lock_guard lock(m_mutex);
    AtlasStats stats{};
    stats.pageCount = static_cast<u32>(m_pages.size());
    stats.entryCount = m_entryCount;
    stats.totalPixels = static_cast<u64>(m_config.pageWidth) * m_config.pageHeight * stats.pageCount;
    stats.usedPixels = m_usedPixels;
    stats.occupancy = stats.totalPixels > 0
        ? static_cast<f32>(stats.usedPixels) / static_cast<f32>(stats.totalPixels)
        : 0.0f;
    return stats;
}

AtlasRect TextureAtlas::InsertInPage(u32 pageIndex, u32 width, u32 height) {
    auto& page = m_pages[pageIndex];

    // Shelf-based packing: try to fit on an existing shelf
    for (auto& shelf : page.shelves) {
        if (height <= shelf.height && shelf.usedWidth + width <= m_config.pageWidth) {
            AtlasRect rect;
            rect.x = shelf.usedWidth;
            rect.y = shelf.y;
            rect.width = width;
            rect.height = height;
            rect.pageIndex = pageIndex;
            rect.valid = true;

            shelf.usedWidth += width;
            return rect;
        }
    }

    // Create a new shelf if there's vertical space
    if (page.nextShelfY + height <= m_config.pageHeight) {
        Shelf shelf;
        shelf.y = page.nextShelfY;
        shelf.height = height;
        shelf.usedWidth = width;

        AtlasRect rect;
        rect.x = 0;
        rect.y = shelf.y;
        rect.width = width;
        rect.height = height;
        rect.pageIndex = pageIndex;
        rect.valid = true;

        page.nextShelfY += height;
        page.shelves.push_back(shelf);
        return rect;
    }

    return {}; // No space on this page
}

u32 TextureAtlas::CreatePage() {
    rhi::TextureDesc desc;
    desc.width = m_config.pageWidth;
    desc.height = m_config.pageHeight;
    desc.format = m_config.format;
    desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
    desc.debugName = std::string(m_config.debugName ? m_config.debugName : "Atlas") +
                     "_Page" + std::to_string(m_pages.size());

    Page page;
    page.texture = m_device->CreateTexture(desc);
    page.nextShelfY = 0;

    u32 idx = static_cast<u32>(m_pages.size());
    m_pages.push_back(std::move(page));

    NGE_LOG_DEBUG("Texture atlas: created page {} ({}x{})",
                  idx, m_config.pageWidth, m_config.pageHeight);
    return idx;
}

} // namespace nge::renderer
