#include "engine/rhi/common/rhi_permutation_key_builder.h"
#include "engine/core/logging/log.h"
#include <sstream>

namespace nge::rhi {

bool PermutationKeyBuilder::Init(const PermutationKeyConfig& config) {
    m_config = config;
    m_totalKeysBuilt = 0;

    NGE_LOG_INFO("Permutation key builder initialized: maxFeatures={}, 64bit={}, validate={}",
                 config.maxFeatures, config.use64Bit, config.validateCombinations);
    return true;
}

void PermutationKeyBuilder::Shutdown() {
    m_features.clear();
    m_nameToIndex.clear();
    m_invalidCombos.clear();
    m_keyUsage.clear();
}

bool PermutationKeyBuilder::RegisterFeature(const std::string& name, bool defaultEnabled) {
    std::lock_guard lock(m_mutex);

    if (m_nameToIndex.find(name) != m_nameToIndex.end()) {
        NGE_LOG_WARN("Permutation key: feature '{}' already registered", name);
        return false;
    }

    u32 maxBits = m_config.use64Bit ? 64 : 32;
    if (m_features.size() >= m_config.maxFeatures || m_features.size() >= maxBits) {
        NGE_LOG_ERROR("Permutation key: max features reached ({})", m_config.maxFeatures);
        return false;
    }

    PermutationFeature feature;
    feature.name = name;
    feature.bitIndex = static_cast<u32>(m_features.size());
    feature.defaultEnabled = defaultEnabled;

    m_nameToIndex[name] = feature.bitIndex;
    m_features.push_back(std::move(feature));
    return true;
}

u64 PermutationKeyBuilder::BuildKey(const std::vector<std::string>& enabledFeatures) const {
    std::lock_guard lock(m_mutex);

    u64 key = 0;

    for (const auto& name : enabledFeatures) {
        auto it = m_nameToIndex.find(name);
        if (it != m_nameToIndex.end()) {
            key |= (1ULL << it->second);
        }
    }

    m_totalKeysBuilt++;
    m_keyUsage[key]++;

    return key;
}

u64 PermutationKeyBuilder::BuildDefaultKey() const {
    std::lock_guard lock(m_mutex);

    u64 key = 0;

    for (const auto& feature : m_features) {
        if (feature.defaultEnabled) {
            key |= (1ULL << feature.bitIndex);
        }
    }

    m_totalKeysBuilt++;
    m_keyUsage[key]++;

    return key;
}

u64 PermutationKeyBuilder::SetFeature(u64 key, const std::string& featureName) const {
    std::lock_guard lock(m_mutex);

    auto it = m_nameToIndex.find(featureName);
    if (it == m_nameToIndex.end()) return key;

    return key | (1ULL << it->second);
}

u64 PermutationKeyBuilder::ClearFeature(u64 key, const std::string& featureName) const {
    std::lock_guard lock(m_mutex);

    auto it = m_nameToIndex.find(featureName);
    if (it == m_nameToIndex.end()) return key;

    return key & ~(1ULL << it->second);
}

bool PermutationKeyBuilder::HasFeature(u64 key, const std::string& featureName) const {
    std::lock_guard lock(m_mutex);

    auto it = m_nameToIndex.find(featureName);
    if (it == m_nameToIndex.end()) return false;

    return (key & (1ULL << it->second)) != 0;
}

void PermutationKeyBuilder::RegisterInvalidCombination(const std::string& featureA, const std::string& featureB) {
    std::lock_guard lock(m_mutex);

    auto itA = m_nameToIndex.find(featureA);
    auto itB = m_nameToIndex.find(featureB);

    if (itA == m_nameToIndex.end() || itB == m_nameToIndex.end()) return;

    InvalidCombo combo;
    combo.bitA = itA->second;
    combo.bitB = itB->second;
    m_invalidCombos.push_back(combo);
}

bool PermutationKeyBuilder::IsValidKey(u64 key) const {
    std::lock_guard lock(m_mutex);

    if (!m_config.validateCombinations) return true;

    for (const auto& combo : m_invalidCombos) {
        bool hasA = (key & (1ULL << combo.bitA)) != 0;
        bool hasB = (key & (1ULL << combo.bitB)) != 0;

        if (hasA && hasB) return false;
    }

    return true;
}

std::string PermutationKeyBuilder::DescribeKey(u64 key) const {
    std::lock_guard lock(m_mutex);

    std::ostringstream oss;
    oss << "0x" << std::hex << key << " [";

    bool first = true;
    for (const auto& feature : m_features) {
        if (key & (1ULL << feature.bitIndex)) {
            if (!first) oss << " | ";
            oss << feature.name;
            first = false;
        }
    }

    oss << "]";
    return oss.str();
}

std::vector<u64> PermutationKeyBuilder::EnumerateValidPermutations() const {
    std::lock_guard lock(m_mutex);

    std::vector<u64> valid;
    u64 totalCombinations = 1ULL << m_features.size();

    for (u64 key = 0; key < totalCombinations; ++key) {
        bool isValid = true;

        if (m_config.validateCombinations) {
            for (const auto& combo : m_invalidCombos) {
                bool hasA = (key & (1ULL << combo.bitA)) != 0;
                bool hasB = (key & (1ULL << combo.bitB)) != 0;
                if (hasA && hasB) {
                    isValid = false;
                    break;
                }
            }
        }

        if (isValid) {
            valid.push_back(key);
        }
    }

    return valid;
}

const PermutationFeature* PermutationKeyBuilder::GetFeature(const std::string& name) const {
    std::lock_guard lock(m_mutex);

    auto it = m_nameToIndex.find(name);
    if (it == m_nameToIndex.end()) return nullptr;

    return &m_features[it->second];
}

u32 PermutationKeyBuilder::GetBitIndex(const std::string& name) const {
    std::lock_guard lock(m_mutex);

    auto it = m_nameToIndex.find(name);
    if (it == m_nameToIndex.end()) return UINT32_MAX;

    return it->second;
}

u32 PermutationKeyBuilder::GetFeatureCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_features.size());
}

void PermutationKeyBuilder::Reset() {
    std::lock_guard lock(m_mutex);
    m_features.clear();
    m_nameToIndex.clear();
    m_invalidCombos.clear();
    m_keyUsage.clear();
    m_totalKeysBuilt = 0;
}

PermutationKeyStats PermutationKeyBuilder::GetStats() const {
    std::lock_guard lock(m_mutex);

    PermutationKeyStats stats{};
    stats.totalFeatures = static_cast<u32>(m_features.size());
    stats.totalKeysBuilt = m_totalKeysBuilt;
    stats.uniqueKeys = static_cast<u32>(m_keyUsage.size());

    // Count invalid combinations
    stats.invalidCombinations = static_cast<u32>(m_invalidCombos.size());

    // Count valid permutations
    u64 totalCombinations = 1ULL << m_features.size();
    u32 validCount = 0;
    for (u64 key = 0; key < totalCombinations; ++key) {
        bool isValid = true;
        for (const auto& combo : m_invalidCombos) {
            if ((key & (1ULL << combo.bitA)) && (key & (1ULL << combo.bitB))) {
                isValid = false;
                break;
            }
        }
        if (isValid) validCount++;
    }
    stats.warmupPermutations = validCount;

    return stats;
}

} // namespace nge::rhi
