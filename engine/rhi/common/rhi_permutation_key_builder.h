#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Shader Permutation Key Builder ──────────────────────────────────
// Builds compact bitfield keys from named shader feature flags for fast
// PSO lookup. Maps feature names to bit positions, constructs keys from
// active feature sets, and validates against known permutations.
//
// Use cases:
//   - Shader variant selection (e.g., HAS_NORMAL_MAP | USE_SKINNING)
//   - PSO cache key construction from material/render state
//   - Feature flag validation (detect invalid combinations)
//   - Enumerate all valid permutations for warm-up
//   - Debug: human-readable key description

struct PermutationFeature {
    std::string name;
    u32         bitIndex;
    bool        defaultEnabled;
};

struct PermutationKeyConfig {
    u32  maxFeatures = 32;       // Max 32 for u32 key, 64 for u64
    bool use64Bit = false;
    bool validateCombinations = true;
};

struct PermutationKeyStats {
    u32 totalFeatures;
    u32 totalKeysBuilt;
    u32 uniqueKeys;
    u32 invalidCombinations;
    u32 warmupPermutations;
};

class PermutationKeyBuilder {
public:
    bool Init(const PermutationKeyConfig& config = {});
    void Shutdown();

    // Register a feature flag
    bool RegisterFeature(const std::string& name, bool defaultEnabled = false);

    // Build a key from a list of enabled feature names
    u64 BuildKey(const std::vector<std::string>& enabledFeatures) const;

    // Build a key with all defaults
    u64 BuildDefaultKey() const;

    // Set/clear a single feature in an existing key
    u64 SetFeature(u64 key, const std::string& featureName) const;
    u64 ClearFeature(u64 key, const std::string& featureName) const;

    // Check if a feature is set in a key
    bool HasFeature(u64 key, const std::string& featureName) const;

    // Register an invalid combination (these features cannot coexist)
    void RegisterInvalidCombination(const std::string& featureA, const std::string& featureB);

    // Validate a key against registered invalid combinations
    bool IsValidKey(u64 key) const;

    // Get human-readable description of a key
    std::string DescribeKey(u64 key) const;

    // Get all valid permutation keys (for warm-up)
    std::vector<u64> EnumerateValidPermutations() const;

    // Get feature info
    const PermutationFeature* GetFeature(const std::string& name) const;

    // Get bit index for a feature
    u32 GetBitIndex(const std::string& name) const;

    u32 GetFeatureCount() const;

    void Reset();

    PermutationKeyStats GetStats() const;

private:
    PermutationKeyConfig m_config;

    std::vector<PermutationFeature> m_features;
    std::unordered_map<std::string, u32> m_nameToIndex; // name -> index in m_features

    struct InvalidCombo {
        u32 bitA;
        u32 bitB;
    };
    std::vector<InvalidCombo> m_invalidCombos;

    mutable u32 m_totalKeysBuilt = 0;
    mutable std::unordered_map<u64, u32> m_keyUsage; // key -> usage count

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
