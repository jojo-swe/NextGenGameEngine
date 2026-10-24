#include "engine/rhi/common/rhi_texture_stream_priority.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <cmath>

namespace nge::rhi {

bool TextureStreamPriorityManager::Init(const TextureStreamConfig& config) {
    m_config = config;
    m_nextId = 0;
    m_totalLoads = 0;
    m_totalEvicts = 0;

    NGE_LOG_INFO("Texture stream priority manager initialized: maxTextures={}, budget={} MB, maxLoads={}/frame",
                 config.maxTextures, config.vramBudget / (1024 * 1024), config.maxLoadsPerFrame);
    return true;
}

void TextureStreamPriorityManager::Shutdown() {
    m_textures.clear();
}

u32 TextureStreamPriorityManager::RegisterTexture(u32 totalMips, u64 perMipSize, float importance,
                                                     const std::string& name) {
    std::lock_guard lock(m_mutex);

    if (m_textures.size() >= m_config.maxTextures) {
        NGE_LOG_WARN("Texture stream: max textures reached ({})", m_config.maxTextures);
        return UINT32_MAX;
    }

    u32 id = m_nextId++;

    StreamingTextureInfo info;
    info.textureId = id;
    info.totalMipLevels = totalMips;
    info.residentMipLevel = totalMips - 1; // Start at lowest quality
    info.requestedMipLevel = totalMips - 1;
    info.perMipSize = perMipSize;
    info.screenCoverage = 0.0f;
    info.distanceToCamera = 1000.0f;
    info.importance = importance;
    info.lastUsedFrame = 0;
    info.debugName = name;

    m_textures[id] = std::move(info);
    return id;
}

void TextureStreamPriorityManager::UpdateUsage(u32 textureId, float screenCoverage,
                                                  float distance, u32 currentFrame) {
    std::lock_guard lock(m_mutex);

    auto it = m_textures.find(textureId);
    if (it == m_textures.end()) return;

    it->second.screenCoverage = screenCoverage;
    it->second.distanceToCamera = distance;
    it->second.lastUsedFrame = currentFrame;
    it->second.requestedMipLevel = ComputeDesiredMip(it->second);
}

void TextureStreamPriorityManager::SetResidentMip(u32 textureId, u32 mipLevel) {
    std::lock_guard lock(m_mutex);

    auto it = m_textures.find(textureId);
    if (it == m_textures.end()) return;

    it->second.residentMipLevel = std::min(mipLevel, it->second.totalMipLevels - 1);
}

std::vector<StreamCommand> TextureStreamPriorityManager::ProcessFrame(u32 currentFrame) {
    std::lock_guard lock(m_mutex);

    std::vector<StreamCommand> commands;

    // Collect load candidates (want higher quality)
    std::vector<std::pair<float, u32>> loadCandidates;
    // Collect eviction candidates (unused or over budget)
    std::vector<std::pair<float, u32>> evictCandidates;

    for (auto& [id, info] : m_textures) {
        if (info.requestedMipLevel < info.residentMipLevel) {
            // Want higher quality mip
            float priority = ComputePriority(info);
            loadCandidates.push_back({priority, id});
        }

        u32 framesSinceUse = currentFrame - info.lastUsedFrame;
        if (framesSinceUse > m_config.evictionFrameThreshold && info.residentMipLevel < info.totalMipLevels - 1) {
            float evictPriority = static_cast<float>(framesSinceUse);
            evictCandidates.push_back({evictPriority, id});
        }
    }

    // Sort loads by priority descending
    std::sort(loadCandidates.begin(), loadCandidates.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    // Sort evictions by staleness descending
    std::sort(evictCandidates.begin(), evictCandidates.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    // Check if we need to evict first (over budget)
    u64 currentVRAM = EstimateVRAM();
    u32 evictsThisFrame = 0;

    if (currentVRAM > m_config.vramBudget) {
        for (const auto& [priority, id] : evictCandidates) {
            if (evictsThisFrame >= m_config.maxEvictsPerFrame) break;
            if (currentVRAM <= m_config.vramBudget) break;

            auto& info = m_textures[id];
            u32 targetMip = std::min(info.residentMipLevel + 1, info.totalMipLevels - 1);

            StreamCommand cmd;
            cmd.textureId = id;
            cmd.action = StreamAction::EvictToLowerMip;
            cmd.targetMipLevel = targetMip;
            cmd.priority = priority;
            commands.push_back(cmd);

            currentVRAM -= info.perMipSize; // Approximate
            evictsThisFrame++;
            m_totalEvicts++;
        }
    }

    // Issue loads (within budget and per-frame limit)
    u32 loadsThisFrame = 0;
    for (const auto& [priority, id] : loadCandidates) {
        if (loadsThisFrame >= m_config.maxLoadsPerFrame) break;

        auto& info = m_textures[id];
        u32 targetMip = info.residentMipLevel > 0 ? info.residentMipLevel - 1 : 0;

        // Check budget
        if (currentVRAM + info.perMipSize > m_config.vramBudget) continue;

        StreamCommand cmd;
        cmd.textureId = id;
        cmd.action = StreamAction::LoadHigherMip;
        cmd.targetMipLevel = targetMip;
        cmd.priority = priority;
        commands.push_back(cmd);

        currentVRAM += info.perMipSize;
        loadsThisFrame++;
        m_totalLoads++;
    }

    return commands;
}

const StreamingTextureInfo* TextureStreamPriorityManager::GetTextureInfo(u32 textureId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_textures.find(textureId);
    if (it == m_textures.end()) return nullptr;
    return &it->second;
}

u64 TextureStreamPriorityManager::GetEstimatedVRAMUsage() const {
    std::lock_guard lock(m_mutex);
    return EstimateVRAM();
}

void TextureStreamPriorityManager::Unregister(u32 textureId) {
    std::lock_guard lock(m_mutex);
    m_textures.erase(textureId);
}

u32 TextureStreamPriorityManager::GetTextureCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_textures.size());
}

void TextureStreamPriorityManager::Reset() {
    std::lock_guard lock(m_mutex);
    m_textures.clear();
    m_nextId = 0;
    m_totalLoads = 0;
    m_totalEvicts = 0;
}

TextureStreamStats TextureStreamPriorityManager::GetStats() const {
    std::lock_guard lock(m_mutex);

    TextureStreamStats stats{};
    stats.totalTextures = static_cast<u32>(m_textures.size());
    stats.vramBudget = m_config.vramBudget;
    stats.totalVRAMUsed = EstimateVRAM();
    stats.budgetUtilization = m_config.vramBudget > 0
        ? static_cast<float>(stats.totalVRAMUsed) / static_cast<float>(m_config.vramBudget)
        : 0.0f;

    u32 fullRes = 0, streaming = 0;
    for (const auto& [id, info] : m_textures) {
        if (info.residentMipLevel == 0) fullRes++;
        if (info.requestedMipLevel < info.residentMipLevel) streaming++;
    }

    stats.texturesAtFullRes = fullRes;
    stats.texturesStreaming = streaming;
    stats.totalLoadsIssued = m_totalLoads;
    stats.totalEvictsIssued = m_totalEvicts;

    return stats;
}

float TextureStreamPriorityManager::ComputePriority(const StreamingTextureInfo& info) const {
    float coverageFactor = info.screenCoverage * m_config.coverageWeight;
    float distFactor = 1.0f / (1.0f + info.distanceToCamera * 0.01f) * m_config.distanceWeight;
    float importanceFactor = info.importance * m_config.importanceWeight;

    // Mip urgency: bigger gap between requested and resident = higher priority
    float mipGap = static_cast<float>(info.residentMipLevel) - static_cast<float>(info.requestedMipLevel);
    float mipFactor = mipGap * 0.5f;

    return coverageFactor + distFactor + importanceFactor + mipFactor;
}

u32 TextureStreamPriorityManager::ComputeDesiredMip(const StreamingTextureInfo& info) const {
    // Desired mip based on screen coverage
    // More coverage -> want mip 0 (full res)
    // Less coverage -> can use lower mips
    if (info.screenCoverage <= 0.0f) return info.totalMipLevels - 1;

    float mipFloat = -std::log2(std::max(info.screenCoverage, 0.001f));
    u32 desiredMip = static_cast<u32>(std::max(0.0f, mipFloat));
    return std::min(desiredMip, info.totalMipLevels - 1);
}

u64 TextureStreamPriorityManager::EstimateVRAM() const {
    u64 total = 0;
    for (const auto& [id, info] : m_textures) {
        // Resident mips: from residentMipLevel to totalMipLevels-1
        // Each lower mip is roughly 1/4 the size, but we simplify
        u32 residentCount = info.totalMipLevels - info.residentMipLevel;
        total += info.perMipSize * residentCount;
    }
    return total;
}

} // namespace nge::rhi
