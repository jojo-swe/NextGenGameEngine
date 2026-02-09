#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Shader Variant Dispatch Table ───────────────────────────────────
// Maps shader permutation keys (combination of feature flags) to
// compiled PSO handles for fast runtime dispatch. Supports fallback
// chains when a specific variant isn't compiled yet.
//
// Use cases:
//   - Material system: select PSO based on active features (skinning, alpha, etc.)
//   - Uber-shader dispatch: map #define combinations to pipeline objects
//   - Hot-reload: invalidate and re-map variants after recompilation
//   - Fallback: degrade gracefully when a variant is missing

using VariantKey = u64; // Bitmask of enabled features

struct ShaderVariantEntry {
    VariantKey  key;
    u64         psoHandle;       // Pipeline state object handle
    std::string shaderName;
    std::string variantDesc;     // Human-readable feature list
    u32         hitCount;        // Runtime dispatch count
    bool        isReady;         // Compiled and available
};

struct VariantFallback {
    VariantKey from;             // Requested variant
    VariantKey to;               // Fallback variant to use
};

struct ShaderVariantDispatchConfig {
    u32  maxVariants = 4096;
    u32  maxFallbacks = 256;
    bool enableHitTracking = true;
    bool warnOnFallback = true;
};

struct ShaderVariantDispatchStats {
    u32 totalVariants;
    u32 readyVariants;
    u32 pendingVariants;
    u32 totalDispatches;
    u32 fallbackDispatches;
    u32 missedDispatches;      // No variant or fallback found
    u32 totalFallbackRules;
};

class ShaderVariantDispatchTable {
public:
    bool Init(const ShaderVariantDispatchConfig& config = {});
    void Shutdown();

    // Register a compiled variant
    void RegisterVariant(VariantKey key, u64 psoHandle,
                          const std::string& shaderName = "",
                          const std::string& variantDesc = "");

    // Mark a variant as pending (being compiled)
    void RegisterPending(VariantKey key, const std::string& shaderName = "");

    // Mark a pending variant as ready
    void MarkReady(VariantKey key, u64 psoHandle);

    // Remove a variant (e.g., after hot-reload invalidation)
    void RemoveVariant(VariantKey key);

    // Register a fallback rule
    void RegisterFallback(VariantKey from, VariantKey to);

    // Dispatch: get PSO handle for a variant key (with fallback chain)
    u64 Dispatch(VariantKey key);

    // Check if a variant is available (ready)
    bool HasVariant(VariantKey key) const;

    // Check if a variant is pending compilation
    bool IsPending(VariantKey key) const;

    // Get variant info
    const ShaderVariantEntry* GetVariant(VariantKey key) const;

    // Get the most-dispatched variants (hot variants)
    std::vector<VariantKey> GetHotVariants(u32 topN = 10) const;

    // Invalidate all variants for a shader (by name)
    u32 InvalidateShader(const std::string& shaderName);

    // Clear all
    void Clear();

    ShaderVariantDispatchStats GetStats() const;

private:
    VariantKey ResolveFallback(VariantKey key) const;

    ShaderVariantDispatchConfig m_config;
    std::unordered_map<VariantKey, ShaderVariantEntry> m_variants;
    std::vector<VariantFallback> m_fallbacks;

    u32 m_totalDispatches = 0;
    u32 m_fallbackDispatches = 0;
    u32 m_missedDispatches = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
