#include "engine/rhi/vulkan/vk_sparse_residency.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi::vulkan {

bool SparseResidencyManager::Init(IDevice* device, const SparseResidencyConfig& config) {
    m_device = device;
    m_config = config;
    m_physicalMemoryUsed = 0;
    m_currentFrame = 0;
    m_nextTextureId = 1;

    // Initialize physical memory pool as one large free block
    PhysicalBlock block;
    block.offset = 0;
    block.size = config.physicalMemoryBudget;
    block.free = true;
    m_physicalBlocks.push_back(block);

    NGE_LOG_INFO("Sparse residency manager initialized: budget={}MB, maxBinds={}, feedbackSize={}",
                 config.physicalMemoryBudget / (1024 * 1024),
                 config.maxPendingBinds, config.feedbackBufferSize);
    return true;
}

void SparseResidencyManager::Shutdown() {
    u32 leakedPages = 0;
    for (const auto& [id, page] : m_pages) {
        if (page.state == PageState::Resident) leakedPages++;
    }
    if (leakedPages > 0) {
        NGE_LOG_WARN("Sparse residency shutdown: {} pages still resident", leakedPages);
    }

    m_textures.clear();
    m_pages.clear();
    m_pendingBinds.clear();
    m_physicalBlocks.clear();
    m_physicalMemoryUsed = 0;
}

u32 SparseResidencyManager::RegisterTexture(const SparseTextureInfo& info) {
    std::lock_guard lock(m_mutex);

    u32 id = m_nextTextureId++;
    SparseTextureInfo texInfo = info;
    texInfo.textureId = id;
    texInfo.pagesX = (info.width + info.pageWidth - 1) / info.pageWidth;
    texInfo.pagesY = (info.height + info.pageHeight - 1) / info.pageHeight;

    m_textures[id] = texInfo;

    // TODO: Create VkImage with VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT
    // VkImageCreateInfo imageCI{};
    // imageCI.flags = VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT | VK_IMAGE_CREATE_SPARSE_BINDING_BIT;
    // ...
    // vkCreateImage(device, &imageCI, nullptr, &texInfo.handle);

    // Auto-make top mips resident (always needed for fallback)
    for (u32 mip = info.mipLevels; mip > 0 && mip > info.mipLevels - m_config.minResidentMip; --mip) {
        u32 mipIdx = mip - 1;
        u32 mipPagesX = std::max(1u, texInfo.pagesX >> mipIdx);
        u32 mipPagesY = std::max(1u, texInfo.pagesY >> mipIdx);
        for (u32 py = 0; py < mipPagesY; ++py) {
            for (u32 px = 0; px < mipPagesX; ++px) {
                SparsePageId pageId;
                pageId.textureId = id;
                pageId.mipLevel = static_cast<u16>(mipIdx);
                pageId.layerIndex = 0;
                pageId.pageX = px;
                pageId.pageY = py;
                RequestPage(pageId);
            }
        }
    }

    NGE_LOG_INFO("Sparse texture '{}' registered: {}x{}, {}mips, {}x{} pages",
                 info.debugName, info.width, info.height, info.mipLevels,
                 texInfo.pagesX, texInfo.pagesY);
    return id;
}

void SparseResidencyManager::UnregisterTexture(u32 textureId) {
    std::lock_guard lock(m_mutex);

    // Free all resident pages
    std::vector<SparsePageId> toRemove;
    for (const auto& [id, page] : m_pages) {
        if (id.textureId == textureId) {
            if (page.state == PageState::Resident) {
                FreePhysicalMemory(page.physicalOffset);
            }
            toRemove.push_back(id);
        }
    }
    for (const auto& id : toRemove) {
        m_pages.erase(id);
    }

    // Remove pending binds for this texture
    m_pendingBinds.erase(
        std::remove_if(m_pendingBinds.begin(), m_pendingBinds.end(),
            [textureId](const SparsePageId& id) { return id.textureId == textureId; }),
        m_pendingBinds.end());

    m_textures.erase(textureId);
}

void SparseResidencyManager::ProcessFeedback(const SparsePageId* requests, u32 requestCount, u64 frameNumber) {
    std::lock_guard lock(m_mutex);
    m_feedbackRequestsThisFrame = requestCount;

    for (u32 i = 0; i < requestCount && i < m_config.feedbackBufferSize; ++i) {
        const auto& req = requests[i];

        // Validate texture exists
        if (m_textures.find(req.textureId) == m_textures.end()) continue;

        auto it = m_pages.find(req);
        if (it != m_pages.end()) {
            // Update access tracking
            it->second.lastAccessFrame = frameNumber;
            it->second.accessCount++;
        } else {
            // New page request
            RequestPage(req);
        }
    }
}

void SparseResidencyManager::ExecuteBinds() {
    std::lock_guard lock(m_mutex);

    u32 bindCount = std::min(static_cast<u32>(m_pendingBinds.size()), m_config.maxPendingBinds);

    for (u32 i = 0; i < bindCount; ++i) {
        const auto& pageId = m_pendingBinds[i];
        auto it = m_pages.find(pageId);
        if (it == m_pages.end() || it->second.state != PageState::Pending) continue;

        auto texIt = m_textures.find(pageId.textureId);
        if (texIt == m_textures.end()) continue;

        // Allocate physical memory for this page
        u64 pageSize = static_cast<u64>(texIt->second.pageWidth) * texIt->second.pageHeight * 4; // Assume 4 BPP
        pageSize >>= (pageId.mipLevel * 2); // Smaller for higher mips
        pageSize = std::max(pageSize, static_cast<u64>(256)); // Minimum page size

        u64 physOffset = AllocatePhysicalMemory(pageSize);
        if (physOffset == UINT64_MAX) {
            // Out of physical memory — try eviction
            EvictLRU(m_config.evictionBatchSize);
            physOffset = AllocatePhysicalMemory(pageSize);
            if (physOffset == UINT64_MAX) {
                NGE_LOG_WARN("Sparse residency: failed to allocate physical memory for page");
                continue;
            }
        }

        it->second.physicalOffset = physOffset;
        it->second.state = PageState::Resident;

        // TODO: VkSparseImageMemoryBind bind{};
        // bind.subresource.mipLevel = pageId.mipLevel;
        // bind.subresource.arrayLayer = pageId.layerIndex;
        // bind.offset = {pageId.pageX * pageWidth, pageId.pageY * pageHeight, 0};
        // bind.extent = {pageWidth >> mip, pageHeight >> mip, 1};
        // bind.memory = physicalMemory;
        // bind.memoryOffset = physOffset;
        // VkSparseImageMemoryBindInfo bindInfo{};
        // bindInfo.image = texIt->second.handle;
        // bindInfo.bindCount = 1;
        // bindInfo.pBinds = &bind;
        // VkBindSparseInfo sparseInfo{};
        // sparseInfo.imageBindCount = 1;
        // sparseInfo.pImageBinds = &bindInfo;
        // vkQueueBindSparse(queue, 1, &sparseInfo, fence);
    }

    // Remove processed binds
    if (bindCount > 0) {
        m_pendingBinds.erase(m_pendingBinds.begin(), m_pendingBinds.begin() + bindCount);
    }
}

u32 SparseResidencyManager::EvictLRU(u32 maxEvictions) {
    std::lock_guard lock(m_mutex);

    if (maxEvictions == 0) maxEvictions = m_config.evictionBatchSize;

    // Collect resident pages sorted by last access frame (oldest first)
    std::vector<std::pair<SparsePageId, u64>> candidates;
    for (const auto& [id, page] : m_pages) {
        if (page.state == PageState::Resident) {
            // Don't evict top mips
            auto texIt = m_textures.find(id.textureId);
            if (texIt != m_textures.end() &&
                id.mipLevel >= texIt->second.mipLevels - m_config.minResidentMip) {
                continue;
            }
            candidates.push_back({id, page.lastAccessFrame});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });

    u32 evicted = 0;
    for (const auto& [pageId, frame] : candidates) {
        if (evicted >= maxEvictions) break;
        MakeNonResident(pageId);
        evicted++;
    }

    m_evictionsThisFrame += evicted;
    return evicted;
}

void SparseResidencyManager::RequestPage(const SparsePageId& pageId) {
    // Note: caller must hold m_mutex or this is called from a locked context
    auto it = m_pages.find(pageId);
    if (it != m_pages.end()) {
        if (it->second.state == PageState::Resident || it->second.state == PageState::Pending) {
            return; // Already resident or pending
        }
    }

    SparsePage page;
    page.id = pageId;
    page.state = PageState::Pending;
    page.physicalOffset = 0;
    page.lastAccessFrame = m_currentFrame;
    page.accessCount = 1;
    m_pages[pageId] = page;

    m_pendingBinds.push_back(pageId);
}

bool SparseResidencyManager::IsResident(const SparsePageId& pageId) const {
    std::lock_guard lock(m_mutex);
    auto it = m_pages.find(pageId);
    return it != m_pages.end() && it->second.state == PageState::Resident;
}

PageState SparseResidencyManager::GetPageState(const SparsePageId& pageId) const {
    std::lock_guard lock(m_mutex);
    auto it = m_pages.find(pageId);
    if (it != m_pages.end()) return it->second.state;
    return PageState::NonResident;
}

std::vector<SparsePageId> SparseResidencyManager::GetResidentPages(u32 textureId) const {
    std::lock_guard lock(m_mutex);
    std::vector<SparsePageId> result;
    for (const auto& [id, page] : m_pages) {
        if (id.textureId == textureId && page.state == PageState::Resident) {
            result.push_back(id);
        }
    }
    return result;
}

const SparseTextureInfo* SparseResidencyManager::GetTextureInfo(u32 textureId) const {
    std::lock_guard lock(m_mutex);
    auto it = m_textures.find(textureId);
    if (it != m_textures.end()) return &it->second;
    return nullptr;
}

void SparseResidencyManager::BeginFrame(u64 frameNumber) {
    std::lock_guard lock(m_mutex);
    m_currentFrame = frameNumber;
    m_evictionsThisFrame = 0;
    m_feedbackRequestsThisFrame = 0;
}

SparseResidencyStats SparseResidencyManager::GetStats() const {
    std::lock_guard lock(m_mutex);
    SparseResidencyStats stats{};
    stats.totalTextures = static_cast<u32>(m_textures.size());
    stats.totalPages = static_cast<u32>(m_pages.size());
    stats.pendingBinds = static_cast<u32>(m_pendingBinds.size());
    stats.evictionsThisFrame = m_evictionsThisFrame;
    stats.physicalMemoryUsed = m_physicalMemoryUsed;
    stats.physicalMemoryBudget = m_config.physicalMemoryBudget;
    stats.feedbackRequestsThisFrame = m_feedbackRequestsThisFrame;

    u32 resident = 0;
    for (const auto& [id, page] : m_pages) {
        if (page.state == PageState::Resident) resident++;
    }
    stats.residentPages = resident;
    stats.residencyPercent = stats.totalPages > 0 ?
        static_cast<f32>(resident) / static_cast<f32>(stats.totalPages) * 100.0f : 0.0f;

    return stats;
}

u64 SparseResidencyManager::AllocatePhysicalMemory(u64 size) {
    // Simple first-fit allocator
    for (auto& block : m_physicalBlocks) {
        if (block.free && block.size >= size) {
            u64 offset = block.offset;

            if (block.size > size) {
                // Split block
                PhysicalBlock remainder;
                remainder.offset = block.offset + size;
                remainder.size = block.size - size;
                remainder.free = true;

                block.size = size;
                block.free = false;

                // Insert after current block
                auto it = std::find_if(m_physicalBlocks.begin(), m_physicalBlocks.end(),
                    [&block](const PhysicalBlock& b) { return b.offset == block.offset; });
                m_physicalBlocks.insert(it + 1, remainder);
            } else {
                block.free = false;
            }

            m_physicalMemoryUsed += size;
            return offset;
        }
    }
    return UINT64_MAX;
}

void SparseResidencyManager::FreePhysicalMemory(u64 offset) {
    for (auto& block : m_physicalBlocks) {
        if (block.offset == offset && !block.free) {
            block.free = true;
            m_physicalMemoryUsed -= block.size;

            // Coalesce with adjacent free blocks
            for (size_t i = 0; i + 1 < m_physicalBlocks.size(); ) {
                if (m_physicalBlocks[i].free && m_physicalBlocks[i + 1].free) {
                    m_physicalBlocks[i].size += m_physicalBlocks[i + 1].size;
                    m_physicalBlocks.erase(m_physicalBlocks.begin() + i + 1);
                } else {
                    ++i;
                }
            }
            return;
        }
    }
}

void SparseResidencyManager::MakeResident(const SparsePageId& pageId) {
    auto it = m_pages.find(pageId);
    if (it == m_pages.end()) return;
    // Handled by ExecuteBinds
}

void SparseResidencyManager::MakeNonResident(const SparsePageId& pageId) {
    auto it = m_pages.find(pageId);
    if (it == m_pages.end() || it->second.state != PageState::Resident) return;

    FreePhysicalMemory(it->second.physicalOffset);
    it->second.state = PageState::NonResident;
    it->second.physicalOffset = 0;

    // TODO: VkSparseImageMemoryBind unbind with null memory
}

} // namespace nge::rhi::vulkan
