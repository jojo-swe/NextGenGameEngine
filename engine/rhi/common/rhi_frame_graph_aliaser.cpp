#include "engine/rhi/common/rhi_frame_graph_aliaser.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool FrameGraphResourceAliaser::Init(const AliaserConfig& config) {
    m_config = config;
    m_resources.reserve(config.maxResources);
    m_dirty = true;

    NGE_LOG_INFO("Frame graph aliaser initialized: maxResources={}, aliasing={}, sameType={}, sameFormat={}",
                 config.maxResources, config.enableAliasing, config.requireSameType, config.requireSameFormat);
    return true;
}

void FrameGraphResourceAliaser::Shutdown() {
    m_resources.clear();
    m_interference.clear();
    m_resourceToGroup.clear();
    m_aliasGroups.clear();
}

u32 FrameGraphResourceAliaser::DeclareResource(ResourceType type, u64 sizeBytes,
                                                  u32 firstPass, u32 lastPass,
                                                  const std::string& name) {
    std::lock_guard lock(m_mutex);

    if (m_resources.size() >= m_config.maxResources) {
        NGE_LOG_ERROR("Frame graph aliaser: max resources reached ({})", m_config.maxResources);
        return UINT32_MAX;
    }

    FrameGraphResource res;
    res.resourceId = static_cast<u32>(m_resources.size());
    res.type = type;
    res.sizeBytes = sizeBytes;
    res.width = 0;
    res.height = 0;
    res.format = 0;
    res.sampleCount = 1;
    res.firstPassIndex = firstPass;
    res.lastPassIndex = lastPass;
    res.debugName = name;

    u32 id = res.resourceId;
    m_resources.push_back(std::move(res));
    m_dirty = true;

    return id;
}

u32 FrameGraphResourceAliaser::DeclareImageResource(ResourceType type, u32 width, u32 height,
                                                       u32 format, u32 sampleCount,
                                                       u32 firstPass, u32 lastPass,
                                                       const std::string& name) {
    std::lock_guard lock(m_mutex);

    if (m_resources.size() >= m_config.maxResources) {
        NGE_LOG_ERROR("Frame graph aliaser: max resources reached ({})", m_config.maxResources);
        return UINT32_MAX;
    }

    // Estimate size: width * height * 4 bytes per pixel * sampleCount
    u64 bpp = 4; // Assume RGBA8 as default
    u64 sizeBytes = static_cast<u64>(width) * height * bpp * sampleCount;

    FrameGraphResource res;
    res.resourceId = static_cast<u32>(m_resources.size());
    res.type = type;
    res.sizeBytes = sizeBytes;
    res.width = width;
    res.height = height;
    res.format = format;
    res.sampleCount = sampleCount;
    res.firstPassIndex = firstPass;
    res.lastPassIndex = lastPass;
    res.debugName = name;

    u32 id = res.resourceId;
    m_resources.push_back(std::move(res));
    m_dirty = true;

    return id;
}

bool FrameGraphResourceAliaser::Overlaps(u32 resourceA, u32 resourceB) const {
    std::lock_guard lock(m_mutex);

    if (resourceA >= m_resources.size() || resourceB >= m_resources.size()) return false;

    const auto& a = m_resources[resourceA];
    const auto& b = m_resources[resourceB];

    // Overlapping if their pass ranges intersect
    return !(a.lastPassIndex < b.firstPassIndex || b.lastPassIndex < a.firstPassIndex);
}

bool FrameGraphResourceAliaser::AreCompatible(u32 resourceA, u32 resourceB) const {
    std::lock_guard lock(m_mutex);

    if (resourceA >= m_resources.size() || resourceB >= m_resources.size()) return false;

    return CanAlias(m_resources[resourceA], m_resources[resourceB]);
}

std::vector<AliasGroup> FrameGraphResourceAliaser::ComputeAliasing() {
    std::lock_guard lock(m_mutex);

    m_aliasGroups.clear();
    m_resourceToGroup.assign(m_resources.size(), UINT32_MAX);

    if (!m_config.enableAliasing || m_resources.empty()) {
        // No aliasing: each resource gets its own group
        for (u32 i = 0; i < static_cast<u32>(m_resources.size()); ++i) {
            AliasGroup group;
            group.groupId = i;
            group.physicalSize = m_resources[i].sizeBytes;
            group.resourceIds.push_back(i);
            group.debugName = m_resources[i].debugName;
            m_aliasGroups.push_back(std::move(group));
            m_resourceToGroup[i] = i;
        }
        m_dirty = false;
        return m_aliasGroups;
    }

    BuildInterferenceGraph();
    m_aliasGroups = GraphColorAssign();
    m_dirty = false;

    return m_aliasGroups;
}

u32 FrameGraphResourceAliaser::GetAliasGroup(u32 resourceId) const {
    std::lock_guard lock(m_mutex);

    if (resourceId >= m_resourceToGroup.size()) return UINT32_MAX;
    return m_resourceToGroup[resourceId];
}

const FrameGraphResource* FrameGraphResourceAliaser::GetResource(u32 resourceId) const {
    std::lock_guard lock(m_mutex);

    if (resourceId >= m_resources.size()) return nullptr;
    return &m_resources[resourceId];
}

u32 FrameGraphResourceAliaser::GetResourceCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_resources.size());
}

void FrameGraphResourceAliaser::Clear() {
    std::lock_guard lock(m_mutex);
    m_resources.clear();
    m_interference.clear();
    m_resourceToGroup.clear();
    m_aliasGroups.clear();
    m_dirty = true;
}

void FrameGraphResourceAliaser::Reset() {
    Clear();
}

AliaserStats FrameGraphResourceAliaser::GetStats() const {
    std::lock_guard lock(m_mutex);

    AliaserStats stats{};
    stats.totalResources = static_cast<u32>(m_resources.size());
    stats.totalAliasGroups = static_cast<u32>(m_aliasGroups.size());

    u64 logicalSize = 0;
    u64 physicalSize = 0;

    for (const auto& res : m_resources) {
        logicalSize += res.sizeBytes;
    }

    for (const auto& group : m_aliasGroups) {
        physicalSize += group.physicalSize;
    }

    stats.totalLogicalSize = logicalSize;
    stats.totalPhysicalSize = physicalSize;
    stats.memorySaved = logicalSize > physicalSize ? logicalSize - physicalSize : 0;
    stats.savingsRatio = logicalSize > 0 ? static_cast<float>(stats.memorySaved) / static_cast<float>(logicalSize) : 0.0f;

    // Compute max overlap: max resources alive at any single pass
    u32 maxPass = 0;
    for (const auto& res : m_resources) {
        if (res.lastPassIndex > maxPass) maxPass = res.lastPassIndex;
    }

    u32 maxOverlap = 0;
    for (u32 p = 0; p <= maxPass; ++p) {
        u32 alive = 0;
        for (const auto& res : m_resources) {
            if (p >= res.firstPassIndex && p <= res.lastPassIndex) alive++;
        }
        if (alive > maxOverlap) maxOverlap = alive;
    }
    stats.maxOverlap = maxOverlap;

    return stats;
}

bool FrameGraphResourceAliaser::CanAlias(const FrameGraphResource& a, const FrameGraphResource& b) const {
    if (m_config.requireSameType && a.type != b.type) return false;
    if (m_config.requireSameFormat && a.format != b.format) return false;
    if (m_config.requireSameSize && (a.width != b.width || a.height != b.height)) return false;
    if (a.sizeBytes < m_config.minResourceSize || b.sizeBytes < m_config.minResourceSize) return false;
    return true;
}

void FrameGraphResourceAliaser::BuildInterferenceGraph() {
    u32 n = static_cast<u32>(m_resources.size());
    m_interference.assign(n, {});

    for (u32 i = 0; i < n; ++i) {
        for (u32 j = i + 1; j < n; ++j) {
            const auto& a = m_resources[i];
            const auto& b = m_resources[j];

            // Two resources interfere if their lifetimes overlap
            bool overlaps = !(a.lastPassIndex < b.firstPassIndex || b.lastPassIndex < a.firstPassIndex);

            if (overlaps) {
                m_interference[i].push_back(j);
                m_interference[j].push_back(i);
            }
        }
    }
}

std::vector<AliasGroup> FrameGraphResourceAliaser::GraphColorAssign() {
    u32 n = static_cast<u32>(m_resources.size());
    std::vector<AliasGroup> groups;

    // Greedy graph coloring: assign each resource to the first compatible group
    // where no interfering resource is already assigned
    m_resourceToGroup.assign(n, UINT32_MAX);

    // Process resources sorted by size descending (larger resources first for better packing)
    std::vector<u32> order(n);
    for (u32 i = 0; i < n; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](u32 a, u32 b) {
        return m_resources[a].sizeBytes > m_resources[b].sizeBytes;
    });

    for (u32 idx : order) {
        const auto& res = m_resources[idx];

        // Try to fit into an existing group
        bool assigned = false;
        for (u32 g = 0; g < static_cast<u32>(groups.size()); ++g) {
            // Check: no resource in this group interferes with idx
            bool conflict = false;
            for (u32 existing : groups[g].resourceIds) {
                // Check interference
                for (u32 neighbor : m_interference[idx]) {
                    if (neighbor == existing) {
                        conflict = true;
                        break;
                    }
                }
                if (conflict) break;

                // Check compatibility
                if (!CanAlias(res, m_resources[existing])) {
                    conflict = true;
                    break;
                }
            }

            if (!conflict) {
                groups[g].resourceIds.push_back(idx);
                // Physical size = max of all resources in the group
                if (res.sizeBytes > groups[g].physicalSize) {
                    groups[g].physicalSize = res.sizeBytes;
                }
                m_resourceToGroup[idx] = g;
                assigned = true;
                break;
            }
        }

        if (!assigned) {
            // Create new group
            AliasGroup group;
            group.groupId = static_cast<u32>(groups.size());
            group.physicalSize = res.sizeBytes;
            group.resourceIds.push_back(idx);
            group.debugName = res.debugName;
            m_resourceToGroup[idx] = group.groupId;
            groups.push_back(std::move(group));
        }
    }

    return groups;
}

} // namespace nge::rhi
