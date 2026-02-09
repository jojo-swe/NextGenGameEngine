#include "engine/rhi/common/rhi_descriptor_pool_analyzer.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <cmath>

namespace nge::rhi {

bool DescriptorPoolAnalyzer::Init(const DescriptorPoolAnalyzerConfig& config) {
    m_config = config;
    m_pools.reserve(config.maxTrackedPools);

    NGE_LOG_INFO("Descriptor pool analyzer initialized: maxPools={}, underutilThresh={:.0f}%, fragThresh={:.0f}%",
                 config.maxTrackedPools, config.underutilizationThreshold * 100.0f,
                 config.fragmentationThreshold * 100.0f);
    return true;
}

void DescriptorPoolAnalyzer::Shutdown() {
    m_pools.clear();
}

void DescriptorPoolAnalyzer::RegisterPool(u32 poolId, u32 maxSets,
                                            const std::unordered_map<DescriptorCategory, u32>& maxPerType,
                                            const std::string& debugName) {
    std::lock_guard lock(m_mutex);

    if (m_pools.size() >= m_config.maxTrackedPools) {
        NGE_LOG_WARN("Descriptor pool analyzer: max tracked pools reached ({})", m_config.maxTrackedPools);
        return;
    }

    PoolAllocation pool;
    pool.poolId = poolId;
    pool.setCount = 0;
    pool.maxSets = maxSets;
    pool.maxPerType = maxPerType;
    pool.debugName = debugName;

    m_pools[poolId] = std::move(pool);
}

void DescriptorPoolAnalyzer::UpdatePoolUsage(u32 poolId, u32 currentSets,
                                               const std::unordered_map<DescriptorCategory, u32>& usedPerType) {
    std::lock_guard lock(m_mutex);

    auto it = m_pools.find(poolId);
    if (it == m_pools.end()) return;

    it->second.setCount = currentSets;
    it->second.usedPerType = usedPerType;
}

void DescriptorPoolAnalyzer::RemovePool(u32 poolId) {
    std::lock_guard lock(m_mutex);
    m_pools.erase(poolId);
}

std::vector<FragmentationReport> DescriptorPoolAnalyzer::Analyze() const {
    std::lock_guard lock(m_mutex);
    std::vector<FragmentationReport> reports;
    reports.reserve(m_pools.size());

    for (const auto& [id, pool] : m_pools) {
        reports.push_back(AnalyzePoolInternal(pool));
    }

    // Sort by utilization (worst first)
    std::sort(reports.begin(), reports.end(),
              [](const FragmentationReport& a, const FragmentationReport& b) {
                  return a.utilizationPercent < b.utilizationPercent;
              });

    return reports;
}

FragmentationReport DescriptorPoolAnalyzer::AnalyzePool(u32 poolId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_pools.find(poolId);
    if (it == m_pools.end()) {
        FragmentationReport empty{};
        empty.poolId = poolId;
        empty.recommendation = "Pool not found";
        return empty;
    }

    return AnalyzePoolInternal(it->second);
}

u32 DescriptorPoolAnalyzer::GetMostFragmentedPool() const {
    std::lock_guard lock(m_mutex);

    u32 worstId = 0;
    f32 worstBalance = 1.0f;

    for (const auto& [id, pool] : m_pools) {
        f32 balance = ComputeTypeBalance(pool);
        if (balance < worstBalance) {
            worstBalance = balance;
            worstId = id;
        }
    }

    return worstId;
}

u32 DescriptorPoolAnalyzer::GetMostUnderutilizedPool() const {
    std::lock_guard lock(m_mutex);

    u32 worstId = 0;
    f32 lowestUtil = 1.0f;

    for (const auto& [id, pool] : m_pools) {
        f32 util = pool.maxSets > 0 ? static_cast<f32>(pool.setCount) / static_cast<f32>(pool.maxSets) : 0.0f;
        if (util < lowestUtil) {
            lowestUtil = util;
            worstId = id;
        }
    }

    return worstId;
}

u32 DescriptorPoolAnalyzer::GetTotalWaste() const {
    std::lock_guard lock(m_mutex);

    u32 totalWaste = 0;
    for (const auto& [id, pool] : m_pools) {
        for (const auto& [type, maxCount] : pool.maxPerType) {
            u32 used = 0;
            auto usedIt = pool.usedPerType.find(type);
            if (usedIt != pool.usedPerType.end()) used = usedIt->second;
            totalWaste += (maxCount - std::min(used, maxCount));
        }
    }

    return totalWaste;
}

void DescriptorPoolAnalyzer::Reset() {
    std::lock_guard lock(m_mutex);
    m_pools.clear();
}

DescriptorPoolAnalyzerStats DescriptorPoolAnalyzer::GetStats() const {
    std::lock_guard lock(m_mutex);
    DescriptorPoolAnalyzerStats stats{};
    stats.totalPools = static_cast<u32>(m_pools.size());

    u32 totalUsedDesc = 0;
    u32 totalMaxDesc = 0;

    for (const auto& [id, pool] : m_pools) {
        stats.totalSetsAllocated += pool.setCount;
        stats.totalSetsMax += pool.maxSets;

        for (const auto& [type, maxCount] : pool.maxPerType) {
            totalMaxDesc += maxCount;
            auto usedIt = pool.usedPerType.find(type);
            if (usedIt != pool.usedPerType.end()) {
                totalUsedDesc += usedIt->second;
            }
        }

        f32 util = pool.maxSets > 0 ? static_cast<f32>(pool.setCount) / static_cast<f32>(pool.maxSets) : 0.0f;
        if (util < m_config.underutilizationThreshold) stats.underutilizedPools++;

        f32 balance = ComputeTypeBalance(pool);
        if (balance < m_config.fragmentationThreshold) stats.fragmentedPools++;
    }

    stats.totalDescriptorsUsed = totalUsedDesc;
    stats.totalDescriptorsMax = totalMaxDesc;
    stats.totalWastedDescriptors = totalMaxDesc - totalUsedDesc;
    stats.overallUtilization = totalMaxDesc > 0
        ? static_cast<f32>(totalUsedDesc) / static_cast<f32>(totalMaxDesc) * 100.0f : 0.0f;

    return stats;
}

FragmentationReport DescriptorPoolAnalyzer::AnalyzePoolInternal(const PoolAllocation& pool) const {
    FragmentationReport report;
    report.poolId = pool.poolId;

    // Set utilization
    report.utilizationPercent = pool.maxSets > 0
        ? static_cast<f32>(pool.setCount) / static_cast<f32>(pool.maxSets) * 100.0f : 0.0f;

    // Type balance
    report.typeBalanceScore = ComputeTypeBalance(pool);

    // Wasted descriptors
    report.wastedDescriptors = 0;
    for (const auto& [type, maxCount] : pool.maxPerType) {
        u32 used = 0;
        auto usedIt = pool.usedPerType.find(type);
        if (usedIt != pool.usedPerType.end()) used = usedIt->second;
        report.wastedDescriptors += (maxCount - std::min(used, maxCount));
    }

    report.wastedSets = pool.maxSets - std::min(pool.setCount, pool.maxSets);

    report.isUnderutilized = (report.utilizationPercent / 100.0f) < m_config.underutilizationThreshold;
    report.isOverfragmented = report.typeBalanceScore < m_config.fragmentationThreshold;

    // Generate recommendation
    if (m_config.enableRecommendations) {
        if (report.isUnderutilized && report.wastedSets > pool.maxSets / 2) {
            report.recommendation = "Pool '" + pool.debugName + "' is severely underutilized (" +
                                     std::to_string(static_cast<int>(report.utilizationPercent)) +
                                     "%). Consider reducing maxSets from " +
                                     std::to_string(pool.maxSets) + " to " +
                                     std::to_string(std::max(pool.setCount * 2, 1u)) + ".";
        } else if (report.isOverfragmented) {
            report.recommendation = "Pool '" + pool.debugName +
                                     "' has imbalanced descriptor type allocation. "
                                     "Rebalance type counts or split into specialized pools.";
        } else if (report.wastedDescriptors > 0 && report.utilizationPercent > 90.0f) {
            report.recommendation = "Pool '" + pool.debugName +
                                     "' is well-utilized but has " +
                                     std::to_string(report.wastedDescriptors) +
                                     " wasted descriptors from type imbalance.";
        } else {
            report.recommendation = "Pool '" + pool.debugName + "' is healthy.";
        }
    }

    return report;
}

f32 DescriptorPoolAnalyzer::ComputeTypeBalance(const PoolAllocation& pool) const {
    if (pool.maxPerType.empty()) return 1.0f;

    f32 totalRatio = 0.0f;
    u32 typeCount = 0;

    for (const auto& [type, maxCount] : pool.maxPerType) {
        if (maxCount == 0) continue;

        u32 used = 0;
        auto usedIt = pool.usedPerType.find(type);
        if (usedIt != pool.usedPerType.end()) used = usedIt->second;

        totalRatio += static_cast<f32>(used) / static_cast<f32>(maxCount);
        typeCount++;
    }

    if (typeCount == 0) return 1.0f;

    f32 avgRatio = totalRatio / static_cast<f32>(typeCount);

    // Compute variance of ratios (lower = more balanced)
    f32 variance = 0.0f;
    for (const auto& [type, maxCount] : pool.maxPerType) {
        if (maxCount == 0) continue;

        u32 used = 0;
        auto usedIt = pool.usedPerType.find(type);
        if (usedIt != pool.usedPerType.end()) used = usedIt->second;

        f32 ratio = static_cast<f32>(used) / static_cast<f32>(maxCount);
        f32 diff = ratio - avgRatio;
        variance += diff * diff;
    }
    variance /= static_cast<f32>(typeCount);

    // Balance score: 1.0 = perfectly balanced, 0.0 = completely imbalanced
    return std::max(0.0f, 1.0f - std::sqrt(variance) * 2.0f);
}

} // namespace nge::rhi
