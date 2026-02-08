#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_format_utils.h"
#include <vector>
#include <algorithm>

namespace nge::rhi {

// ─── Resource Aliasing Optimizer ─────────────────────────────────────────
// Graph-coloring based optimizer for transient resource memory assignment.
// Analyzes resource lifetimes from the render graph and produces an
// optimal aliasing plan that minimizes peak GPU memory usage.
//
// Algorithm:
//   1. Build an interference graph (resources with overlapping lifetimes)
//   2. Apply graph coloring to assign resources to memory "bins"
//   3. Resources sharing a bin are aliased to the same heap region
//   4. Report memory savings vs. naive allocation

struct ResourceLifetime {
    u32  resourceId;
    u32  firstPass;
    u32  lastPass;
    u64  sizeBytes;
    u64  alignment;
    bool isTexture;
};

struct AliasingBin {
    u32              binId;
    u64              binSize;      // Size of the largest resource in this bin
    u64              alignment;    // Strictest alignment
    std::vector<u32> resourceIds;  // Resources aliased to this bin
};

struct AliasingPlan {
    std::vector<AliasingBin> bins;
    u64 totalWithoutAliasing;   // Sum of all resource sizes
    u64 totalWithAliasing;      // Sum of bin sizes (peak memory)
    u64 memorySaved;
    f32 reductionPercent;       // (saved / total) * 100
};

class AliasingOptimizer {
public:
    void Reset();

    // Add a resource with its lifetime
    void AddResource(const ResourceLifetime& resource);

    // Compute optimal aliasing plan
    AliasingPlan Optimize() const;

    // Query: can two resources be aliased?
    bool CanAlias(u32 resourceA, u32 resourceB) const;

    u32 GetResourceCount() const { return static_cast<u32>(m_resources.size()); }

private:
    // Build adjacency list (interference graph)
    std::vector<std::vector<u32>> BuildInterferenceGraph() const;

    // Graph coloring (greedy with largest-first ordering)
    std::vector<u32> ColorGraph(const std::vector<std::vector<u32>>& adj) const;

    std::vector<ResourceLifetime> m_resources;
};

// ─── Inline Implementation ──────────────────────────────────────────────

inline void AliasingOptimizer::Reset() {
    m_resources.clear();
}

inline void AliasingOptimizer::AddResource(const ResourceLifetime& resource) {
    m_resources.push_back(resource);
}

inline bool AliasingOptimizer::CanAlias(u32 resourceA, u32 resourceB) const {
    if (resourceA >= m_resources.size() || resourceB >= m_resources.size()) return false;
    const auto& a = m_resources[resourceA];
    const auto& b = m_resources[resourceB];
    return a.lastPass < b.firstPass || b.lastPass < a.firstPass;
}

inline std::vector<std::vector<u32>> AliasingOptimizer::BuildInterferenceGraph() const {
    u32 n = static_cast<u32>(m_resources.size());
    std::vector<std::vector<u32>> adj(n);

    for (u32 i = 0; i < n; ++i) {
        for (u32 j = i + 1; j < n; ++j) {
            // Overlapping lifetimes = interference edge
            if (!CanAlias(i, j)) {
                adj[i].push_back(j);
                adj[j].push_back(i);
            }
        }
    }
    return adj;
}

inline std::vector<u32> AliasingOptimizer::ColorGraph(
    const std::vector<std::vector<u32>>& adj) const {

    u32 n = static_cast<u32>(m_resources.size());
    std::vector<u32> colors(n, UINT32_MAX);

    // Order by size (largest first) for better bin packing
    std::vector<u32> order(n);
    for (u32 i = 0; i < n; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [this](u32 a, u32 b) {
        return m_resources[a].sizeBytes > m_resources[b].sizeBytes;
    });

    for (u32 idx : order) {
        // Find colors used by neighbors
        std::vector<bool> usedColors;
        for (u32 neighbor : adj[idx]) {
            if (colors[neighbor] != UINT32_MAX) {
                if (colors[neighbor] >= usedColors.size()) {
                    usedColors.resize(colors[neighbor] + 1, false);
                }
                usedColors[colors[neighbor]] = true;
            }
        }

        // Assign the smallest unused color
        u32 color = 0;
        while (color < usedColors.size() && usedColors[color]) {
            color++;
        }
        colors[idx] = color;
    }

    return colors;
}

inline AliasingPlan AliasingOptimizer::Optimize() const {
    AliasingPlan plan;
    plan.totalWithoutAliasing = 0;
    plan.totalWithAliasing = 0;

    if (m_resources.empty()) {
        plan.memorySaved = 0;
        plan.reductionPercent = 0;
        return plan;
    }

    // Sum total without aliasing
    for (const auto& res : m_resources) {
        plan.totalWithoutAliasing += res.sizeBytes;
    }

    // Build interference graph and color it
    auto adj = BuildInterferenceGraph();
    auto colors = ColorGraph(adj);

    // Determine number of bins
    u32 numBins = 0;
    for (u32 c : colors) {
        numBins = std::max(numBins, c + 1);
    }

    // Build bins
    plan.bins.resize(numBins);
    for (u32 i = 0; i < numBins; ++i) {
        plan.bins[i].binId = i;
        plan.bins[i].binSize = 0;
        plan.bins[i].alignment = 1;
    }

    for (u32 i = 0; i < static_cast<u32>(m_resources.size()); ++i) {
        u32 bin = colors[i];
        plan.bins[bin].resourceIds.push_back(m_resources[i].resourceId);
        plan.bins[bin].binSize = std::max(plan.bins[bin].binSize, m_resources[i].sizeBytes);
        plan.bins[bin].alignment = std::max(plan.bins[bin].alignment, m_resources[i].alignment);
    }

    // Calculate total with aliasing
    for (const auto& bin : plan.bins) {
        plan.totalWithAliasing += bin.binSize;
    }

    plan.memorySaved = plan.totalWithoutAliasing - plan.totalWithAliasing;
    plan.reductionPercent = plan.totalWithoutAliasing > 0
        ? static_cast<f32>(plan.memorySaved) * 100.0f / static_cast<f32>(plan.totalWithoutAliasing)
        : 0.0f;

    return plan;
}

} // namespace nge::rhi
