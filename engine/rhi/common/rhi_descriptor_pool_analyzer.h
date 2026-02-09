#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Descriptor Pool Fragmentation Analyzer ──────────────────────────
// Tracks descriptor pool allocations to detect fragmentation, waste,
// and sub-optimal pool sizing. Provides actionable recommendations.
//
// Use cases:
//   - Detect under-utilized pools (allocated but mostly empty)
//   - Detect over-fragmented pools (many small free gaps)
//   - Identify descriptor type imbalances
//   - Guide pool size tuning and defragmentation
//   - CI budget regression testing

enum class DescriptorCategory : u8 {
    Sampler,
    CombinedImageSampler,
    SampledImage,
    StorageImage,
    UniformBuffer,
    StorageBuffer,
    UniformBufferDynamic,
    StorageBufferDynamic,
    InputAttachment,
    AccelerationStructure,
    Count,
};

struct PoolAllocation {
    u32         poolId;
    u32         setCount;           // Descriptor sets allocated
    u32         maxSets;            // Max sets this pool supports
    std::unordered_map<DescriptorCategory, u32> usedPerType;
    std::unordered_map<DescriptorCategory, u32> maxPerType;
    std::string debugName;
};

struct FragmentationReport {
    u32   poolId;
    f32   utilizationPercent;       // Sets used / max sets
    f32   typeBalanceScore;         // 0=imbalanced, 1=perfectly balanced
    u32   wastedDescriptors;        // Total unused descriptors
    u32   wastedSets;               // Unused set slots
    bool  isUnderutilized;          // < 25% utilized
    bool  isOverfragmented;         // Many small allocations with gaps
    std::string recommendation;
};

struct DescriptorPoolAnalyzerConfig {
    f32  underutilizationThreshold = 0.25f;  // Below this = warning
    f32  fragmentationThreshold = 0.5f;       // Above this = fragmented
    u32  maxTrackedPools = 256;
    bool enableRecommendations = true;
};

struct DescriptorPoolAnalyzerStats {
    u32 totalPools;
    u32 totalSetsAllocated;
    u32 totalSetsMax;
    u32 totalDescriptorsUsed;
    u32 totalDescriptorsMax;
    u32 underutilizedPools;
    u32 fragmentedPools;
    f32 overallUtilization;
    u32 totalWastedDescriptors;
};

class DescriptorPoolAnalyzer {
public:
    bool Init(const DescriptorPoolAnalyzerConfig& config = {});
    void Shutdown();

    // Register a pool with its capacity
    void RegisterPool(u32 poolId, u32 maxSets,
                      const std::unordered_map<DescriptorCategory, u32>& maxPerType,
                      const std::string& debugName = "");

    // Update a pool's current usage
    void UpdatePoolUsage(u32 poolId, u32 currentSets,
                          const std::unordered_map<DescriptorCategory, u32>& usedPerType);

    // Remove a pool from tracking
    void RemovePool(u32 poolId);

    // Run analysis on all pools
    std::vector<FragmentationReport> Analyze() const;

    // Get report for a specific pool
    FragmentationReport AnalyzePool(u32 poolId) const;

    // Get the most fragmented pool
    u32 GetMostFragmentedPool() const;

    // Get the most underutilized pool
    u32 GetMostUnderutilizedPool() const;

    // Get total wasted descriptors across all pools
    u32 GetTotalWaste() const;

    // Clear all tracking
    void Reset();

    DescriptorPoolAnalyzerStats GetStats() const;

private:
    FragmentationReport AnalyzePoolInternal(const PoolAllocation& pool) const;
    f32 ComputeTypeBalance(const PoolAllocation& pool) const;

    DescriptorPoolAnalyzerConfig m_config;
    std::unordered_map<u32, PoolAllocation> m_pools;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
