#include "engine/rhi/common/rhi_desc_set_layout_optimizer.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool DescriptorSetLayoutOptimizer::Init(const LayoutOptimizerConfig& config) {
    m_config = config;
    m_layoutsMerged = 0;
    m_bindingsRemoved = 0;
    m_totalBinds = 0;
    m_rebindsAvoided = 0;

    NGE_LOG_INFO("Descriptor set layout optimizer initialized: maxLayouts={}, merge={}, compact={}, sortFreq={}",
                 config.maxLayouts, config.enableMerging, config.enableCompaction, config.sortByFrequency);
    return true;
}

void DescriptorSetLayoutOptimizer::Shutdown() {
    m_declaredBindings.clear();
    m_layouts.clear();
}

void DescriptorSetLayoutOptimizer::DeclareBinding(u32 setIndex, u32 binding, DescriptorType type,
                                                     u32 count, u32 stageFlags,
                                                     UpdateFrequency frequency,
                                                     const std::string& name) {
    std::lock_guard lock(m_mutex);

    DescriptorBinding desc;
    desc.binding = binding;
    desc.type = type;
    desc.count = count;
    desc.stageFlags = stageFlags;
    desc.frequency = frequency;
    desc.debugName = name;

    m_declaredBindings[setIndex].push_back(std::move(desc));
}

std::vector<DescriptorSetLayout> DescriptorSetLayoutOptimizer::BuildOptimizedLayouts() {
    std::lock_guard lock(m_mutex);

    m_layouts.clear();

    if (m_config.sortByFrequency) {
        // Reorganize bindings into sets by update frequency
        std::unordered_map<u32, std::vector<DescriptorBinding>> frequencySets;

        for (auto& [setIdx, bindings] : m_declaredBindings) {
            for (auto& b : bindings) {
                u32 targetSet = static_cast<u32>(b.frequency);
                frequencySets[targetSet].push_back(b);
            }
        }

        for (auto& [setIdx, bindings] : frequencySets) {
            if (m_config.enableCompaction) {
                u32 origSize = static_cast<u32>(bindings.size());
                CompactBindings(bindings);
                m_bindingsRemoved += origSize - static_cast<u32>(bindings.size());
            }

            // Re-number bindings sequentially
            for (u32 i = 0; i < static_cast<u32>(bindings.size()); ++i) {
                bindings[i].binding = i;
            }

            DescriptorSetLayout layout;
            layout.layoutId = static_cast<u32>(m_layouts.size());
            layout.setIndex = setIdx;
            layout.bindings = bindings;
            layout.layoutHash = ComputeLayoutHash(bindings);
            layout.refCount = 1;
            layout.debugName = "Set" + std::to_string(setIdx);

            m_layouts.push_back(std::move(layout));
        }
    } else {
        // Keep original set assignments
        for (auto& [setIdx, bindings] : m_declaredBindings) {
            if (m_config.enableCompaction) {
                u32 origSize = static_cast<u32>(bindings.size());
                CompactBindings(bindings);
                m_bindingsRemoved += origSize - static_cast<u32>(bindings.size());
            }

            DescriptorSetLayout layout;
            layout.layoutId = static_cast<u32>(m_layouts.size());
            layout.setIndex = setIdx;
            layout.bindings = bindings;
            layout.layoutHash = ComputeLayoutHash(bindings);
            layout.refCount = 1;
            layout.debugName = "Set" + std::to_string(setIdx);

            m_layouts.push_back(std::move(layout));
        }
    }

    // Merge compatible layouts
    if (m_config.enableMerging) {
        std::vector<DescriptorSetLayout> merged;
        std::vector<bool> consumed(m_layouts.size(), false);

        for (u32 i = 0; i < static_cast<u32>(m_layouts.size()); ++i) {
            if (consumed[i]) continue;

            DescriptorSetLayout combined = m_layouts[i];

            for (u32 j = i + 1; j < static_cast<u32>(m_layouts.size()); ++j) {
                if (consumed[j]) continue;

                if (m_layouts[i].layoutHash == m_layouts[j].layoutHash &&
                    m_layouts[i].setIndex != m_layouts[j].setIndex) {
                    // Same layout used in different sets -> merge ref count
                    combined.refCount++;
                    consumed[j] = true;
                    m_layoutsMerged++;
                }
            }

            combined.layoutId = static_cast<u32>(merged.size());
            merged.push_back(std::move(combined));
        }

        m_layouts = std::move(merged);
    }

    // Sort by set index
    std::sort(m_layouts.begin(), m_layouts.end(),
              [](const DescriptorSetLayout& a, const DescriptorSetLayout& b) {
                  return a.setIndex < b.setIndex;
              });

    return m_layouts;
}

const DescriptorSetLayout* DescriptorSetLayoutOptimizer::GetLayout(u32 setIndex) const {
    std::lock_guard lock(m_mutex);

    for (const auto& layout : m_layouts) {
        if (layout.setIndex == setIndex) return &layout;
    }
    return nullptr;
}

bool DescriptorSetLayoutOptimizer::AreCompatible(u32 layoutA, u32 layoutB) const {
    std::lock_guard lock(m_mutex);

    const DescriptorSetLayout* a = nullptr;
    const DescriptorSetLayout* b = nullptr;

    for (const auto& layout : m_layouts) {
        if (layout.layoutId == layoutA) a = &layout;
        if (layout.layoutId == layoutB) b = &layout;
    }

    if (!a || !b) return false;
    return a->layoutHash == b->layoutHash;
}

void DescriptorSetLayoutOptimizer::RecordBind([[maybe_unused]] u32 setIndex) {
    std::lock_guard lock(m_mutex);
    m_totalBinds++;
}

u32 DescriptorSetLayoutOptimizer::GetBindingCount(u32 setIndex) const {
    std::lock_guard lock(m_mutex);

    for (const auto& layout : m_layouts) {
        if (layout.setIndex == setIndex) return static_cast<u32>(layout.bindings.size());
    }

    // Check declared bindings before build
    auto it = m_declaredBindings.find(setIndex);
    if (it != m_declaredBindings.end()) return static_cast<u32>(it->second.size());

    return 0;
}

u32 DescriptorSetLayoutOptimizer::GetLayoutCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_layouts.size());
}

void DescriptorSetLayoutOptimizer::Clear() {
    std::lock_guard lock(m_mutex);
    m_declaredBindings.clear();
    m_layouts.clear();
}

void DescriptorSetLayoutOptimizer::Reset() {
    std::lock_guard lock(m_mutex);
    m_declaredBindings.clear();
    m_layouts.clear();
    m_layoutsMerged = 0;
    m_bindingsRemoved = 0;
    m_totalBinds = 0;
    m_rebindsAvoided = 0;
}

LayoutOptimizerStats DescriptorSetLayoutOptimizer::GetStats() const {
    std::lock_guard lock(m_mutex);

    LayoutOptimizerStats stats{};
    stats.totalLayouts = static_cast<u32>(m_layouts.size());

    u32 totalBindings = 0;
    std::unordered_map<u64, u32> hashCounts;

    for (const auto& layout : m_layouts) {
        totalBindings += static_cast<u32>(layout.bindings.size());
        hashCounts[layout.layoutHash]++;
    }

    stats.totalBindings = totalBindings;
    stats.layoutsMerged = m_layoutsMerged;
    stats.bindingsRemoved = m_bindingsRemoved;
    stats.uniqueLayoutHashes = static_cast<u32>(hashCounts.size());
    stats.rebindsAvoided = m_rebindsAvoided;

    return stats;
}

u64 DescriptorSetLayoutOptimizer::ComputeLayoutHash(const std::vector<DescriptorBinding>& bindings) const {
    // FNV-1a hash over binding metadata
    u64 hash = 14695981039346656037ULL;
    auto hashByte = [&](u8 b) {
        hash ^= b;
        hash *= 1099511628211ULL;
    };

    for (const auto& b : bindings) {
        hashByte(static_cast<u8>(b.binding));
        hashByte(static_cast<u8>(b.binding >> 8));
        hashByte(static_cast<u8>(b.type));
        hashByte(static_cast<u8>(b.count));
        hashByte(static_cast<u8>(b.count >> 8));
        hashByte(static_cast<u8>(b.stageFlags));
        hashByte(static_cast<u8>(b.stageFlags >> 8));
        hashByte(static_cast<u8>(b.frequency));
    }

    return hash;
}

void DescriptorSetLayoutOptimizer::SortBindingsByFrequency(std::vector<DescriptorBinding>& bindings) const {
    std::sort(bindings.begin(), bindings.end(),
              [](const DescriptorBinding& a, const DescriptorBinding& b) {
                  return static_cast<u8>(a.frequency) < static_cast<u8>(b.frequency);
              });
}

void DescriptorSetLayoutOptimizer::CompactBindings(std::vector<DescriptorBinding>& bindings) const {
    // Remove duplicate bindings (same binding index in same set)
    std::vector<DescriptorBinding> unique;
    std::unordered_map<u32, bool> seen;

    for (auto& b : bindings) {
        if (!seen[b.binding]) {
            unique.push_back(std::move(b));
            seen[b.binding] = true;
        }
    }

    bindings = std::move(unique);
}

} // namespace nge::rhi
