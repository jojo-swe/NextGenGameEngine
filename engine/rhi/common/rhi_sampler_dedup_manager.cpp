#include "engine/rhi/common/rhi_sampler_dedup_manager.h"
#include "engine/core/logging/log.h"
#include <cstring>

namespace nge::rhi {

// ─── SamplerDesc Implementation ─────────────────────────────────────────

u64 SamplerDesc::ComputeHash() const {
    // FNV-1a hash over sampler state
    u64 hash = 14695981039346656037ULL;
    auto hashByte = [&](u8 b) {
        hash ^= b;
        hash *= 1099511628211ULL;
    };
    auto hashU8 = [&](u8 v) { hashByte(v); };
    auto hashFloat = [&](float f) {
        u32 bits;
        std::memcpy(&bits, &f, sizeof(bits));
        hashByte(static_cast<u8>(bits));
        hashByte(static_cast<u8>(bits >> 8));
        hashByte(static_cast<u8>(bits >> 16));
        hashByte(static_cast<u8>(bits >> 24));
    };

    hashU8(static_cast<u8>(magFilter));
    hashU8(static_cast<u8>(minFilter));
    hashU8(static_cast<u8>(mipMode));
    hashU8(static_cast<u8>(addressU));
    hashU8(static_cast<u8>(addressV));
    hashU8(static_cast<u8>(addressW));
    hashFloat(mipLodBias);
    hashU8(anisotropyEnable ? 1 : 0);
    hashFloat(maxAnisotropy);
    hashU8(static_cast<u8>(compareOp));
    hashFloat(minLod);
    hashFloat(maxLod);
    hashU8(static_cast<u8>(borderColor));
    hashU8(unnormalizedCoords ? 1 : 0);

    return hash;
}

// ─── SamplerDedupManager Implementation ─────────────────────────────────

bool SamplerDedupManager::Init(const SamplerDedupConfig& config) {
    m_config = config;
    m_nextHandle = 1;
    m_totalAcquires = 0;
    m_deduplicated = 0;
    m_totalReleases = 0;
    m_totalDestroyed = 0;
    m_peakSamplers = 0;

    NGE_LOG_INFO("Sampler dedup manager initialized: maxSamplers={}, refCount={}, canonicalize={}",
                 config.maxSamplers, config.enableRefCounting, config.canonicalize);
    return true;
}

void SamplerDedupManager::Shutdown() {
    if (!m_samplersByDesc.empty()) {
        NGE_LOG_WARN("Sampler dedup manager: {} samplers still alive at shutdown",
                     m_samplersByDesc.size());
    }
    // TODO: vkDestroySampler for all remaining samplers
    m_samplersByDesc.clear();
    m_handleToDesc.clear();
}

u64 SamplerDedupManager::Acquire(const SamplerDesc& desc) {
    std::lock_guard lock(m_mutex);
    m_totalAcquires++;

    SamplerDesc canonical = m_config.canonicalize ? Canonicalize(desc) : desc;

    auto it = m_samplersByDesc.find(canonical);
    if (it != m_samplersByDesc.end()) {
        // Existing sampler - increment ref count
        if (m_config.enableRefCounting) {
            it->second.refCount++;
        }
        m_deduplicated++;
        return it->second.samplerHandle;
    }

    // Check capacity
    if (m_samplersByDesc.size() >= m_config.maxSamplers) {
        NGE_LOG_ERROR("Sampler dedup manager: max samplers reached ({})", m_config.maxSamplers);
        return 0;
    }

    // Create new sampler
    u64 handle = CreateSamplerHandle(canonical);

    SamplerEntry entry;
    entry.desc = canonical;
    entry.samplerHandle = handle;
    entry.refCount = 1;
    entry.bindlessIndex = static_cast<u32>(m_samplersByDesc.size());

    m_samplersByDesc[canonical] = entry;
    m_handleToDesc[handle] = canonical;

    u32 count = static_cast<u32>(m_samplersByDesc.size());
    if (count > m_peakSamplers) m_peakSamplers = count;

    return handle;
}

void SamplerDedupManager::Release(u64 samplerHandle) {
    std::lock_guard lock(m_mutex);
    m_totalReleases++;

    auto handleIt = m_handleToDesc.find(samplerHandle);
    if (handleIt == m_handleToDesc.end()) return;

    auto descIt = m_samplersByDesc.find(handleIt->second);
    if (descIt == m_samplersByDesc.end()) return;

    if (m_config.enableRefCounting) {
        if (descIt->second.refCount > 0) {
            descIt->second.refCount--;
        }

        if (descIt->second.refCount == 0) {
            // TODO: vkDestroySampler(descIt->second.samplerHandle)
            m_samplersByDesc.erase(descIt);
            m_handleToDesc.erase(handleIt);
            m_totalDestroyed++;
        }
    }
}

u32 SamplerDedupManager::GetRefCount(u64 samplerHandle) const {
    std::lock_guard lock(m_mutex);

    auto handleIt = m_handleToDesc.find(samplerHandle);
    if (handleIt == m_handleToDesc.end()) return 0;

    auto descIt = m_samplersByDesc.find(handleIt->second);
    if (descIt == m_samplersByDesc.end()) return 0;

    return descIt->second.refCount;
}

bool SamplerDedupManager::Exists(const SamplerDesc& desc) const {
    std::lock_guard lock(m_mutex);
    SamplerDesc canonical = m_config.canonicalize ? Canonicalize(desc) : desc;
    return m_samplersByDesc.find(canonical) != m_samplersByDesc.end();
}

const SamplerEntry* SamplerDedupManager::GetEntry(u64 samplerHandle) const {
    std::lock_guard lock(m_mutex);

    auto handleIt = m_handleToDesc.find(samplerHandle);
    if (handleIt == m_handleToDesc.end()) return nullptr;

    auto descIt = m_samplersByDesc.find(handleIt->second);
    if (descIt == m_samplersByDesc.end()) return nullptr;

    return &descIt->second;
}

u32 SamplerDedupManager::GetCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_samplersByDesc.size());
}

u32 SamplerDedupManager::PurgeUnreferenced() {
    std::lock_guard lock(m_mutex);

    u32 purged = 0;
    auto it = m_samplersByDesc.begin();
    while (it != m_samplersByDesc.end()) {
        if (it->second.refCount == 0) {
            m_handleToDesc.erase(it->second.samplerHandle);
            it = m_samplersByDesc.erase(it);
            m_totalDestroyed++;
            purged++;
        } else {
            ++it;
        }
    }

    return purged;
}

void SamplerDedupManager::Reset() {
    std::lock_guard lock(m_mutex);
    m_samplersByDesc.clear();
    m_handleToDesc.clear();
    m_nextHandle = 1;
    m_totalAcquires = 0;
    m_deduplicated = 0;
    m_totalReleases = 0;
    m_totalDestroyed = 0;
    m_peakSamplers = 0;
}

SamplerDedupStats SamplerDedupManager::GetStats() const {
    std::lock_guard lock(m_mutex);
    SamplerDedupStats stats{};
    stats.totalSamplers = static_cast<u32>(m_samplersByDesc.size());
    stats.totalAcquires = m_totalAcquires;
    stats.deduplicated = m_deduplicated;
    stats.totalReleases = m_totalReleases;
    stats.totalDestroyed = m_totalDestroyed;
    stats.peakSamplers = m_peakSamplers;
    return stats;
}

SamplerDesc SamplerDedupManager::Canonicalize(const SamplerDesc& desc) const {
    SamplerDesc canonical = desc;
    canonical.debugName.clear(); // Debug name doesn't affect sampler state

    // If anisotropy disabled, clamp maxAnisotropy to 1.0
    if (!canonical.anisotropyEnable) {
        canonical.maxAnisotropy = 1.0f;
    }

    // If compare disabled, normalize compareOp
    if (canonical.compareOp == SamplerCompareOp::Disabled) {
        // Already disabled, nothing to normalize
    }

    // If unnormalized coords, certain states are forced
    if (canonical.unnormalizedCoords) {
        canonical.mipMode = SamplerMipMode::Nearest;
        canonical.minLod = 0.0f;
        canonical.maxLod = 0.0f;
    }

    return canonical;
}

u64 SamplerDedupManager::CreateSamplerHandle([[maybe_unused]] const SamplerDesc& desc) {
    // TODO: vkCreateSampler with the provided desc
    // For now return a unique placeholder handle
    return m_nextHandle++;
}

} // namespace nge::rhi
