#include "engine/rhi/common/rhi_shader_variant_dispatch.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool ShaderVariantDispatchTable::Init(const ShaderVariantDispatchConfig& config) {
    m_config = config;
    m_variants.reserve(config.maxVariants);
    m_fallbacks.reserve(config.maxFallbacks);
    m_totalDispatches = 0;
    m_fallbackDispatches = 0;
    m_missedDispatches = 0;

    NGE_LOG_INFO("Shader variant dispatch table initialized: maxVariants={}, maxFallbacks={}, hitTracking={}",
                 config.maxVariants, config.maxFallbacks, config.enableHitTracking);
    return true;
}

void ShaderVariantDispatchTable::Shutdown() {
    m_variants.clear();
    m_fallbacks.clear();
}

void ShaderVariantDispatchTable::RegisterVariant(VariantKey key, u64 psoHandle,
                                                   const std::string& shaderName,
                                                   const std::string& variantDesc) {
    std::lock_guard lock(m_mutex);

    if (m_variants.size() >= m_config.maxVariants) {
        NGE_LOG_WARN("Shader variant dispatch: max variants reached ({})", m_config.maxVariants);
        return;
    }

    ShaderVariantEntry entry;
    entry.key = key;
    entry.psoHandle = psoHandle;
    entry.shaderName = shaderName;
    entry.variantDesc = variantDesc;
    entry.hitCount = 0;
    entry.isReady = true;

    m_variants[key] = std::move(entry);
}

void ShaderVariantDispatchTable::RegisterPending(VariantKey key, const std::string& shaderName) {
    std::lock_guard lock(m_mutex);

    if (m_variants.size() >= m_config.maxVariants) return;

    ShaderVariantEntry entry;
    entry.key = key;
    entry.psoHandle = 0;
    entry.shaderName = shaderName;
    entry.hitCount = 0;
    entry.isReady = false;

    m_variants[key] = std::move(entry);
}

void ShaderVariantDispatchTable::MarkReady(VariantKey key, u64 psoHandle) {
    std::lock_guard lock(m_mutex);

    auto it = m_variants.find(key);
    if (it != m_variants.end()) {
        it->second.psoHandle = psoHandle;
        it->second.isReady = true;
    }
}

void ShaderVariantDispatchTable::RemoveVariant(VariantKey key) {
    std::lock_guard lock(m_mutex);
    m_variants.erase(key);
}

void ShaderVariantDispatchTable::RegisterFallback(VariantKey from, VariantKey to) {
    std::lock_guard lock(m_mutex);

    if (m_fallbacks.size() >= m_config.maxFallbacks) {
        NGE_LOG_WARN("Shader variant dispatch: max fallback rules reached ({})", m_config.maxFallbacks);
        return;
    }

    m_fallbacks.push_back({from, to});
}

u64 ShaderVariantDispatchTable::Dispatch(VariantKey key) {
    std::lock_guard lock(m_mutex);
    m_totalDispatches++;

    // Direct lookup
    auto it = m_variants.find(key);
    if (it != m_variants.end() && it->second.isReady) {
        if (m_config.enableHitTracking) it->second.hitCount++;
        return it->second.psoHandle;
    }

    // Try fallback chain
    VariantKey fallbackKey = ResolveFallback(key);
    if (fallbackKey != key) {
        auto fbIt = m_variants.find(fallbackKey);
        if (fbIt != m_variants.end() && fbIt->second.isReady) {
            m_fallbackDispatches++;
            if (m_config.enableHitTracking) fbIt->second.hitCount++;

            if (m_config.warnOnFallback) {
                NGE_LOG_DEBUG("Shader variant fallback: 0x{:X} -> 0x{:X}", key, fallbackKey);
            }

            return fbIt->second.psoHandle;
        }
    }

    // No variant found
    m_missedDispatches++;
    NGE_LOG_WARN("Shader variant miss: key=0x{:X}, no variant or fallback available", key);
    return 0;
}

bool ShaderVariantDispatchTable::HasVariant(VariantKey key) const {
    std::lock_guard lock(m_mutex);
    auto it = m_variants.find(key);
    return it != m_variants.end() && it->second.isReady;
}

bool ShaderVariantDispatchTable::IsPending(VariantKey key) const {
    std::lock_guard lock(m_mutex);
    auto it = m_variants.find(key);
    return it != m_variants.end() && !it->second.isReady;
}

const ShaderVariantEntry* ShaderVariantDispatchTable::GetVariant(VariantKey key) const {
    std::lock_guard lock(m_mutex);
    auto it = m_variants.find(key);
    if (it != m_variants.end()) return &it->second;
    return nullptr;
}

std::vector<VariantKey> ShaderVariantDispatchTable::GetHotVariants(u32 topN) const {
    std::lock_guard lock(m_mutex);

    std::vector<std::pair<VariantKey, u32>> sorted;
    sorted.reserve(m_variants.size());
    for (const auto& [key, entry] : m_variants) {
        sorted.push_back({key, entry.hitCount});
    }

    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    std::vector<VariantKey> result;
    u32 count = std::min(topN, static_cast<u32>(sorted.size()));
    for (u32 i = 0; i < count; ++i) {
        result.push_back(sorted[i].first);
    }

    return result;
}

u32 ShaderVariantDispatchTable::InvalidateShader(const std::string& shaderName) {
    std::lock_guard lock(m_mutex);

    u32 removed = 0;
    auto it = m_variants.begin();
    while (it != m_variants.end()) {
        if (it->second.shaderName == shaderName) {
            it = m_variants.erase(it);
            removed++;
        } else {
            ++it;
        }
    }

    if (removed > 0) {
        NGE_LOG_INFO("Invalidated {} variants for shader '{}'", removed, shaderName);
    }

    return removed;
}

void ShaderVariantDispatchTable::Clear() {
    std::lock_guard lock(m_mutex);
    m_variants.clear();
    m_fallbacks.clear();
    m_totalDispatches = 0;
    m_fallbackDispatches = 0;
    m_missedDispatches = 0;
}

ShaderVariantDispatchStats ShaderVariantDispatchTable::GetStats() const {
    std::lock_guard lock(m_mutex);
    ShaderVariantDispatchStats stats{};
    stats.totalVariants = static_cast<u32>(m_variants.size());

    for (const auto& [key, entry] : m_variants) {
        if (entry.isReady) stats.readyVariants++;
        else stats.pendingVariants++;
    }

    stats.totalDispatches = m_totalDispatches;
    stats.fallbackDispatches = m_fallbackDispatches;
    stats.missedDispatches = m_missedDispatches;
    stats.totalFallbackRules = static_cast<u32>(m_fallbacks.size());

    return stats;
}

VariantKey ShaderVariantDispatchTable::ResolveFallback(VariantKey key) const {
    // Walk fallback chain (max depth 8 to prevent infinite loops)
    VariantKey current = key;
    for (u32 depth = 0; depth < 8; ++depth) {
        bool found = false;
        for (const auto& fb : m_fallbacks) {
            if (fb.from == current) {
                current = fb.to;
                found = true;

                // Check if this fallback target is ready
                auto it = m_variants.find(current);
                if (it != m_variants.end() && it->second.isReady) {
                    return current;
                }

                break;
            }
        }
        if (!found) break;
    }

    return current; // May still be the original key if no fallback found
}

} // namespace nge::rhi
