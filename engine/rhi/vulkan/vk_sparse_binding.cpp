#include "engine/rhi/vulkan/vk_sparse_binding.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool SparseBindingManager::Init(IDevice* device, const SparseBindingConfig& config) {
    m_device = device;
    m_config = config;
    m_poolCapacity = config.physicalPagePoolSize;
    m_poolUsed = 0;

    // TODO: Allocate physical page pool
    // VkMemoryAllocateInfo allocInfo{};
    // allocInfo.allocationSize = config.physicalPagePoolSize;
    // allocInfo.memoryTypeIndex = FindMemoryType(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    // vkAllocateMemory(device, &allocInfo, nullptr, &m_poolHandle);
    m_poolHandle = 1; // Stub

    m_images.reserve(config.maxSparseImages);
    m_pendingBinds.reserve(config.maxPendingBinds);

    NGE_LOG_INFO("Sparse binding manager initialized: {} MB page pool, max {} images",
                 config.physicalPagePoolSize / (1024 * 1024), config.maxSparseImages);
    return true;
}

void SparseBindingManager::Shutdown() {
    // TODO: vkFreeMemory(device, m_poolHandle, nullptr);
    m_images.clear();
    m_pendingBinds.clear();
    m_freePages.clear();
}

u32 SparseBindingManager::RegisterSparseImage(TextureHandle texture, const SparseImageInfo& info) {
    std::lock_guard lock(m_mutex);

    ImageState state;
    state.info = info;
    state.info.texture = texture;

    u32 index = static_cast<u32>(m_images.size());
    m_images.push_back(std::move(state));

    NGE_LOG_DEBUG("Registered sparse image {}: {}x{} tiles, {} mips, {} bytes/tile",
                  index, info.tilesX, info.tilesY, info.mipLevels, info.tileSizeBytes);
    return index;
}

bool SparseBindingManager::BindTile(u32 imageIndex, const SparseTile& tile) {
    std::lock_guard lock(m_mutex);
    if (imageIndex >= m_images.size()) return false;

    auto& state = m_images[imageIndex];
    if (state.boundTiles.count(tile)) return true; // Already bound

    // Allocate physical page
    PhysicalPage page = AllocatePhysicalPage(state.info.tileSizeBytes);
    if (!page.bound) {
        NGE_LOG_WARN("Sparse binding: failed to allocate physical page");
        return false;
    }

    state.boundTiles[tile] = page;

    PendingBind pending;
    pending.imageIndex = imageIndex;
    pending.tile = tile;
    pending.bind = true;
    m_pendingBinds.push_back(pending);

    return true;
}

bool SparseBindingManager::UnbindTile(u32 imageIndex, const SparseTile& tile) {
    std::lock_guard lock(m_mutex);
    if (imageIndex >= m_images.size()) return false;

    auto& state = m_images[imageIndex];
    auto it = state.boundTiles.find(tile);
    if (it == state.boundTiles.end()) return false;

    FreePhysicalPage(it->second);
    state.boundTiles.erase(it);

    PendingBind pending;
    pending.imageIndex = imageIndex;
    pending.tile = tile;
    pending.bind = false;
    m_pendingBinds.push_back(pending);

    return true;
}

bool SparseBindingManager::BindMipTail(u32 imageIndex) {
    std::lock_guard lock(m_mutex);
    if (imageIndex >= m_images.size()) return false;

    auto& state = m_images[imageIndex];
    if (state.mipTailBound) return true;

    // TODO: Bind the mip tail opaque memory
    // VkSparseMemoryBind mipTailBind{};
    // mipTailBind.resourceOffset = state.info.mipTailOffset;
    // mipTailBind.size = mipTailSize;
    // mipTailBind.memory = m_poolHandle;
    // mipTailBind.memoryOffset = AllocatePhysicalPage(mipTailSize).offset;

    state.mipTailBound = true;
    return true;
}

void SparseBindingManager::Flush() {
    std::lock_guard lock(m_mutex);
    if (m_pendingBinds.empty()) return;

    // TODO: Submit sparse binding operations
    // VkBindSparseInfo bindInfo{};
    // bindInfo.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
    //
    // For each pending bind:
    //   VkSparseImageMemoryBind imageBind{};
    //   imageBind.subresource.mipLevel = tile.mipLevel;
    //   imageBind.subresource.arrayLayer = tile.arrayLayer;
    //   imageBind.offset = { tile.x * tileW, tile.y * tileH, tile.z * tileD };
    //   imageBind.extent = { tileW, tileH, tileD };
    //   imageBind.memory = page.memoryHandle;
    //   imageBind.memoryOffset = page.offset;
    //
    // vkQueueBindSparse(sparseQueue, 1, &bindInfo, fence);

    NGE_LOG_DEBUG("Sparse binding: flushed {} operations", m_pendingBinds.size());
    m_pendingBinds.clear();
}

bool SparseBindingManager::IsTileResident(u32 imageIndex, const SparseTile& tile) const {
    std::lock_guard lock(m_mutex);
    if (imageIndex >= m_images.size()) return false;
    return m_images[imageIndex].boundTiles.count(tile) > 0;
}

const SparseImageInfo& SparseBindingManager::GetImageInfo(u32 imageIndex) const {
    static SparseImageInfo empty{};
    std::lock_guard lock(m_mutex);
    if (imageIndex >= m_images.size()) return empty;
    return m_images[imageIndex].info;
}

SparseBindingStats SparseBindingManager::GetStats() const {
    std::lock_guard lock(m_mutex);
    SparseBindingStats stats{};

    for (const auto& img : m_images) {
        u32 totalTilesForImage = img.info.tilesX * img.info.tilesY * img.info.mipLevels * img.info.arrayLayers;
        stats.totalTiles += totalTilesForImage;
        stats.residentTiles += static_cast<u32>(img.boundTiles.size());
    }

    stats.pendingBinds = 0;
    stats.pendingUnbinds = 0;
    for (const auto& p : m_pendingBinds) {
        if (p.bind) stats.pendingBinds++;
        else stats.pendingUnbinds++;
    }

    stats.physicalMemoryUsed = m_poolUsed;
    stats.physicalMemoryBudget = m_poolCapacity;
    stats.residencyPercent = stats.totalTiles > 0
        ? static_cast<f32>(stats.residentTiles) * 100.0f / static_cast<f32>(stats.totalTiles)
        : 0.0f;

    return stats;
}

PhysicalPage SparseBindingManager::AllocatePhysicalPage(u64 size) {
    PhysicalPage page;

    // Try free list first
    if (!m_freePages.empty()) {
        page.offset = m_freePages.back();
        m_freePages.pop_back();
        page.memoryHandle = m_poolHandle;
        page.size = size;
        page.bound = true;
        return page;
    }

    // Allocate from pool
    if (m_poolUsed + size > m_poolCapacity) {
        return page; // Out of memory
    }

    page.memoryHandle = m_poolHandle;
    page.offset = m_poolUsed;
    page.size = size;
    page.bound = true;
    m_poolUsed += size;
    return page;
}

void SparseBindingManager::FreePhysicalPage(const PhysicalPage& page) {
    m_freePages.push_back(page.offset);
    // Note: m_poolUsed isn't decremented — use free list for recycling
}

} // namespace nge::rhi
