#include "engine/renderer/streaming/texture_streamer.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::renderer {

bool TextureStreamer::Init(rhi::IDevice* device, u32 pagePoolSize, u32 pageSize) {
    m_device = device;
    m_poolCapacity = pagePoolSize;
    m_pageSize = pageSize;
    m_poolSlotUsed.resize(pagePoolSize, false);

    // Page atlas: 2D texture large enough to hold all pages in a grid
    // Layout: sqrt(poolSize) × sqrt(poolSize) pages
    u32 gridDim = static_cast<u32>(std::ceil(std::sqrt(static_cast<f64>(pagePoolSize))));
    u32 atlasSize = gridDim * pageSize;

    {
        rhi::TextureDesc desc;
        desc.width = atlasSize;
        desc.height = atlasSize;
        desc.format = rhi::Format::RGBA8_UNORM; // Default; actual format per-texture
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
        desc.debugName = "VT_PageAtlas";
        m_pageAtlas = device->CreateTexture(desc);
    }

    // Indirection texture: maps virtual page → physical page in atlas
    // Size depends on max virtual texture resolution / page size
    {
        rhi::TextureDesc desc;
        desc.width = 256;  // Supports up to 256×256 pages = 32K×32K at 128px pages
        desc.height = 256;
        desc.format = rhi::Format::RGBA8_UNORM; // RG = atlas XY, B = mip, A = valid
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage;
        desc.debugName = "VT_Indirection";
        m_indirectionTex = device->CreateTexture(desc);
    }

    // Feedback buffer for GPU readback
    {
        rhi::BufferDesc desc;
        desc.size = sizeof(FeedbackEntry) * 4096; // Support up to 4K feedback entries
        desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferSrc;
        desc.memoryUsage = rhi::MemoryUsage::GPU_To_CPU;
        desc.debugName = "VT_FeedbackBuffer";
        m_feedbackBuffer = device->CreateBuffer(desc);
    }

    NGE_LOG_INFO("Texture streamer initialized: {} page pool ({}px pages), {}×{} atlas",
                 pagePoolSize, pageSize, atlasSize, atlasSize);
    return true;
}

void TextureStreamer::Shutdown() {
    if (!m_device) return;

    if (m_pageAtlas.IsValid()) { m_device->DestroyTexture(m_pageAtlas); m_pageAtlas = {}; }
    if (m_indirectionTex.IsValid()) { m_device->DestroyTexture(m_indirectionTex); m_indirectionTex = {}; }
    if (m_feedbackBuffer.IsValid()) { m_device->DestroyBuffer(m_feedbackBuffer); m_feedbackBuffer = {}; }

    m_textures.clear();
    m_loadedPages.clear();
    m_poolSlotUsed.clear();
}

StreamingTextureId TextureStreamer::RegisterTexture(const StreamingTextureDesc& desc) {
    StreamingTextureId id = m_nextId++;
    StreamingTexture tex;
    tex.desc = desc;
    tex.id = id;
    m_textures[id] = std::move(tex);

    NGE_LOG_INFO("Registered streaming texture '{}' ({}x{}, {} mips, id={})",
                 desc.path, desc.fullWidth, desc.fullHeight, desc.mipCount, id);
    return id;
}

void TextureStreamer::UnregisterTexture(StreamingTextureId id) {
    // Remove all loaded pages for this texture
    std::vector<PageAddress> toRemove;
    for (const auto& [addr, entry] : m_loadedPages) {
        if (addr.textureId == id) {
            toRemove.push_back(addr);
        }
    }
    for (const auto& addr : toRemove) {
        auto it = m_loadedPages.find(addr);
        if (it != m_loadedPages.end()) {
            m_poolSlotUsed[it->second.poolIndex] = false;
            m_loadedPages.erase(it);
            m_loadedPageCount--;
        }
    }

    m_textures.erase(id);
}

void TextureStreamer::Update(u64 frameIndex) {
    std::lock_guard lock(m_mutex);
    m_currentFrame = frameIndex;

    // Process load queue (limited per frame to avoid stalls)
    u32 loadsThisFrame = 0;
    while (!m_loadQueue.empty() && loadsThisFrame < MAX_LOADS_PER_FRAME) {
        auto request = m_loadQueue.top();
        m_loadQueue.pop();

        // Skip if already loaded
        if (m_loadedPages.count(request.addr)) continue;

        LoadPage(request.addr);
        loadsThisFrame++;
    }
}

void TextureStreamer::ProcessFeedback(const FeedbackEntry* feedbackData, u32 feedbackCount) {
    std::lock_guard lock(m_mutex);

    for (u32 i = 0; i < feedbackCount; ++i) {
        const auto& entry = feedbackData[i];
        if (entry.textureId == INVALID_STREAMING_TEX) continue;

        auto texIt = m_textures.find(entry.textureId);
        if (texIt == m_textures.end()) continue;

        const auto& texDesc = texIt->second.desc;
        u32 mip = std::min(entry.requestedMip, texDesc.mipCount - 1);

        // Calculate page coordinates at this mip level
        u32 mipWidth = std::max(texDesc.fullWidth >> mip, 1u);
        u32 mipHeight = std::max(texDesc.fullHeight >> mip, 1u);
        u32 pagesX = (mipWidth + texDesc.pageSize - 1) / texDesc.pageSize;
        u32 pagesY = (mipHeight + texDesc.pageSize - 1) / texDesc.pageSize;

        // Request all pages at this mip level (simplified — real impl would
        // only request pages visible in the feedback tile)
        for (u32 py = 0; py < pagesY; ++py) {
            for (u32 px = 0; px < pagesX; ++px) {
                PageAddress addr{entry.textureId, mip, px, py};

                // Update last-used frame if already loaded
                auto it = m_loadedPages.find(addr);
                if (it != m_loadedPages.end()) {
                    it->second.lastUsedFrame = m_currentFrame;
                    continue;
                }

                // Queue for loading
                LoadRequest req;
                req.addr = addr;
                req.priority = mip; // Lower mip = higher priority
                m_loadQueue.push(req);
            }
        }
    }
}

void TextureStreamer::LoadPage(const PageAddress& addr) {
    // Ensure pool has space
    u32 slot = AllocatePoolSlot();
    if (slot == UINT32_MAX) {
        EvictLRU();
        slot = AllocatePoolSlot();
        if (slot == UINT32_MAX) {
            NGE_LOG_WARN("Texture streamer: page pool exhausted, cannot load page");
            return;
        }
    }

    // TODO: Read page data from disk (mip level, page coords)
    // TODO: Upload to page atlas at the allocated slot
    // TODO: Update indirection texture

    PagePoolEntry entry;
    entry.address = addr;
    entry.poolIndex = slot;
    entry.lastUsedFrame = m_currentFrame;
    entry.loaded = true;

    m_loadedPages[addr] = entry;
    m_loadedPageCount++;

    UpdateIndirection(addr, slot);
}

void TextureStreamer::EvictLRU() {
    // Find the least recently used page
    u64 oldestFrame = UINT64_MAX;
    PageAddress oldestAddr{};
    bool found = false;

    for (const auto& [addr, entry] : m_loadedPages) {
        if (entry.lastUsedFrame < oldestFrame) {
            oldestFrame = entry.lastUsedFrame;
            oldestAddr = addr;
            found = true;
        }
    }

    if (!found) return;

    auto it = m_loadedPages.find(oldestAddr);
    if (it != m_loadedPages.end()) {
        m_poolSlotUsed[it->second.poolIndex] = false;
        m_loadedPages.erase(it);
        m_loadedPageCount--;
    }
}

u32 TextureStreamer::AllocatePoolSlot() {
    for (u32 i = 0; i < m_poolCapacity; ++i) {
        if (!m_poolSlotUsed[i]) {
            m_poolSlotUsed[i] = true;
            return i;
        }
    }
    return UINT32_MAX;
}

void TextureStreamer::UpdateIndirection(const PageAddress& addr, u32 poolIndex) {
    // Compute physical coordinates in the page atlas
    u32 gridDim = static_cast<u32>(std::ceil(std::sqrt(static_cast<f64>(m_poolCapacity))));
    u32 atlasX = poolIndex % gridDim;
    u32 atlasY = poolIndex / gridDim;

    // TODO: Write to indirection texture at (addr.pageX, addr.pageY) for addr.mipLevel
    // Value: (atlasX, atlasY, mipLevel, 1=valid)
    (void)atlasX;
    (void)atlasY;
    (void)addr;
}

} // namespace nge::renderer
