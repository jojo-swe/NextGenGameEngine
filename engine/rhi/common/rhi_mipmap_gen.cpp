#include "engine/rhi/common/rhi_mipmap_gen.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <cmath>

namespace nge::rhi {

bool MipmapGenManager::Init(const MipGenConfig& config) {
    m_config = config;
    m_nextId = 0;
    m_totalMipsGenerated = 0;
    m_requestsThisFrame = 0;
    m_batchesDispatched = 0;
    m_failedGenerations = 0;

    NGE_LOG_INFO("Mipmap generation manager initialized: maxTextures={}, maxRequests={}, filter={}, batching={}",
                 config.maxTextures, config.maxRequestsPerFrame,
                 static_cast<u8>(config.defaultFilter), config.enableBatching);
    return true;
}

void MipmapGenManager::Shutdown() {
    m_textures.clear();
    m_pendingRequests.clear();
}

u32 MipmapGenManager::RegisterTexture(u32 width, u32 height, u32 mipLevels, u32 arrayLayers,
                                         MipFilter filter, bool isDynamic,
                                         const std::string& name) {
    std::lock_guard lock(m_mutex);

    if (m_textures.size() >= m_config.maxTextures) {
        NGE_LOG_ERROR("Mipmap gen: max textures reached ({})", m_config.maxTextures);
        return UINT32_MAX;
    }

    u32 id = m_nextId++;

    MipGenTextureInfo info;
    info.textureId = id;
    info.width = width;
    info.height = height;
    info.mipLevels = mipLevels;
    info.arrayLayers = arrayLayers;
    info.filter = filter;
    info.state = MipGenState::Pending; // Needs initial generation
    info.lastGenFrame = 0;
    info.isDynamic = isDynamic;
    info.debugName = name;

    m_textures[id] = std::move(info);
    return id;
}

bool MipmapGenManager::RequestGeneration(u32 textureId, u32 baseMip, u32 mipCount,
                                            u32 arrayLayer) {
    std::lock_guard lock(m_mutex);

    auto it = m_textures.find(textureId);
    if (it == m_textures.end()) return false;

    if (m_pendingRequests.size() >= m_config.maxRequestsPerFrame) {
        NGE_LOG_WARN("Mipmap gen: max requests per frame reached ({})", m_config.maxRequestsPerFrame);
        return false;
    }

    auto& tex = it->second;
    tex.state = MipGenState::Pending;

    MipGenRequest req;
    req.textureId = textureId;
    req.baseMip = baseMip;
    req.mipCount = (mipCount == 0) ? (tex.mipLevels - baseMip - 1) : mipCount;
    req.arrayLayer = arrayLayer;

    m_pendingRequests.push_back(req);
    return true;
}

void MipmapGenManager::Invalidate(u32 textureId) {
    std::lock_guard lock(m_mutex);

    auto it = m_textures.find(textureId);
    if (it == m_textures.end()) return;

    it->second.state = MipGenState::Pending;
}

std::vector<MipGenRequest> MipmapGenManager::ProcessFrame(u32 currentFrame) {
    std::lock_guard lock(m_mutex);

    // Add dynamic textures that need regeneration
    for (auto& [id, tex] : m_textures) {
        if (tex.isDynamic && tex.state == MipGenState::UpToDate) {
            tex.state = MipGenState::Pending;

            MipGenRequest req;
            req.textureId = id;
            req.baseMip = 0;
            req.mipCount = tex.mipLevels - 1;
            req.arrayLayer = UINT32_MAX;
            m_pendingRequests.push_back(req);
        }
    }

    // Collect pending requests
    std::vector<MipGenRequest> dispatched;

    for (auto& req : m_pendingRequests) {
        auto it = m_textures.find(req.textureId);
        if (it == m_textures.end()) continue;

        it->second.state = MipGenState::InProgress;
        it->second.lastGenFrame = currentFrame;

        m_totalMipsGenerated += req.mipCount;
        dispatched.push_back(req);
    }

    m_requestsThisFrame = static_cast<u32>(dispatched.size());
    if (!dispatched.empty()) m_batchesDispatched++;

    m_pendingRequests.clear();
    return dispatched;
}

void MipmapGenManager::MarkComplete(u32 textureId) {
    std::lock_guard lock(m_mutex);

    auto it = m_textures.find(textureId);
    if (it == m_textures.end()) return;

    it->second.state = MipGenState::UpToDate;
}

void MipmapGenManager::MarkFailed(u32 textureId) {
    std::lock_guard lock(m_mutex);

    auto it = m_textures.find(textureId);
    if (it == m_textures.end()) return;

    it->second.state = MipGenState::Failed;
    m_failedGenerations++;
}

const MipGenTextureInfo* MipmapGenManager::GetTextureInfo(u32 textureId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_textures.find(textureId);
    if (it == m_textures.end()) return nullptr;
    return &it->second;
}

u32 MipmapGenManager::GetMipLevelCount(u32 textureId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_textures.find(textureId);
    if (it == m_textures.end()) return 0;
    return it->second.mipLevels;
}

u32 MipmapGenManager::CalculateMipCount(u32 width, u32 height) {
    u32 maxDim = std::max(width, height);
    if (maxDim == 0) return 1;
    return static_cast<u32>(std::floor(std::log2(static_cast<double>(maxDim)))) + 1;
}

std::vector<u32> MipmapGenManager::GetPendingTextures() const {
    std::lock_guard lock(m_mutex);

    std::vector<u32> pending;
    for (const auto& [id, tex] : m_textures) {
        if (tex.state == MipGenState::Pending) {
            pending.push_back(id);
        }
    }
    return pending;
}

void MipmapGenManager::SetFilter(u32 textureId, MipFilter filter) {
    std::lock_guard lock(m_mutex);

    auto it = m_textures.find(textureId);
    if (it == m_textures.end()) return;

    it->second.filter = filter;
}

void MipmapGenManager::Unregister(u32 textureId) {
    std::lock_guard lock(m_mutex);
    m_textures.erase(textureId);
}

u32 MipmapGenManager::GetTextureCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_textures.size());
}

void MipmapGenManager::Reset() {
    std::lock_guard lock(m_mutex);
    m_textures.clear();
    m_pendingRequests.clear();
    m_nextId = 0;
    m_totalMipsGenerated = 0;
    m_requestsThisFrame = 0;
    m_batchesDispatched = 0;
    m_failedGenerations = 0;
}

MipGenStats MipmapGenManager::GetStats() const {
    std::lock_guard lock(m_mutex);

    MipGenStats stats{};
    stats.totalTextures = static_cast<u32>(m_textures.size());

    u32 pending = 0, upToDate = 0, dynamic = 0;
    for (const auto& [id, tex] : m_textures) {
        if (tex.state == MipGenState::Pending) pending++;
        if (tex.state == MipGenState::UpToDate) upToDate++;
        if (tex.isDynamic) dynamic++;
    }

    stats.texturesPending = pending;
    stats.texturesUpToDate = upToDate;
    stats.totalMipsGenerated = m_totalMipsGenerated;
    stats.requestsThisFrame = m_requestsThisFrame;
    stats.batchesDispatched = m_batchesDispatched;
    stats.dynamicTextures = dynamic;
    stats.failedGenerations = m_failedGenerations;

    return stats;
}

} // namespace nge::rhi
