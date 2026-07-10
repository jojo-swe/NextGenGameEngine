#include "engine/rhi/common/rhi_pipeline_layout_checker.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool PipelineLayoutChecker::Init(const PipelineLayoutCheckerConfig& config) {
    m_config = config;
    m_layouts.reserve(config.maxLayouts);
    m_totalChecks = 0;
    m_compatiblePairs = 0;
    m_incompatiblePairs = 0;
    m_totalIncompats = 0;

    NGE_LOG_INFO("Pipeline layout checker initialized: maxLayouts={}, checkGaps={}, checkPartial={}",
                 config.maxLayouts, config.checkPushConstantGaps, config.checkPartialSetCompat);
    return true;
}

void PipelineLayoutChecker::Shutdown() {
    m_layouts.clear();
}

void PipelineLayoutChecker::RegisterLayout(const PipelineLayoutDesc& layout) {
    std::lock_guard lock(m_mutex);

    if (m_layouts.size() >= m_config.maxLayouts) {
        NGE_LOG_WARN("Pipeline layout checker: max layouts reached ({})", m_config.maxLayouts);
        return;
    }

    m_layouts[layout.layoutId] = layout;
}

void PipelineLayoutChecker::RemoveLayout(u64 layoutId) {
    std::lock_guard lock(m_mutex);
    m_layouts.erase(layoutId);
}

std::vector<LayoutIncompatibility> PipelineLayoutChecker::CheckCompatibility(u64 layoutA, u64 layoutB) const {
    std::lock_guard lock(m_mutex);
    m_totalChecks++;

    std::vector<LayoutIncompatibility> issues;

    auto itA = m_layouts.find(layoutA);
    auto itB = m_layouts.find(layoutB);
    if (itA == m_layouts.end() || itB == m_layouts.end()) {
        LayoutIncompatibility issue;
        issue.type = IncompatibilityType::MissingSet;
        issue.setIndex = 0;
        issue.bindingIndex = 0;
        issue.description = "One or both layouts not registered";
        issues.push_back(std::move(issue));
        m_incompatiblePairs++;
        m_totalIncompats++;
        return issues;
    }

    const auto& a = itA->second;
    const auto& b = itB->second;

    // Check descriptor set layouts
    u32 maxSets = static_cast<u32>(std::max(a.setLayouts.size(), b.setLayouts.size()));
    for (u32 s = 0; s < maxSets; ++s) {
        bool aHasSet = s < a.setLayouts.size();
        bool bHasSet = s < b.setLayouts.size();

        if (aHasSet && !bHasSet) {
            LayoutIncompatibility issue;
            issue.type = IncompatibilityType::MissingSet;
            issue.setIndex = s;
            issue.bindingIndex = 0;
            issue.description = "Layout '" + b.debugName + "' missing set " + std::to_string(s) +
                                 " present in '" + a.debugName + "'";
            issues.push_back(std::move(issue));
        } else if (!aHasSet && bHasSet) {
            LayoutIncompatibility issue;
            issue.type = IncompatibilityType::MissingSet;
            issue.setIndex = s;
            issue.bindingIndex = 0;
            issue.description = "Layout '" + a.debugName + "' missing set " + std::to_string(s) +
                                 " present in '" + b.debugName + "'";
            issues.push_back(std::move(issue));
        } else if (aHasSet && bHasSet) {
            auto setIssues = CheckSetCompat(a.setLayouts[s], b.setLayouts[s]);
            issues.insert(issues.end(), setIssues.begin(), setIssues.end());
        }
    }

    // Check push constants
    auto pcIssues = CheckPushConstantCompat(layoutA, layoutB);
    issues.insert(issues.end(), pcIssues.begin(), pcIssues.end());

    if (issues.empty()) {
        m_compatiblePairs++;
    } else {
        m_incompatiblePairs++;
        m_totalIncompats += static_cast<u32>(issues.size());
    }

    return issues;
}

bool PipelineLayoutChecker::AreSetsPrefixCompatible(u64 layoutA, u64 layoutB, u32 upToSet) const {
    std::lock_guard lock(m_mutex);

    auto itA = m_layouts.find(layoutA);
    auto itB = m_layouts.find(layoutB);
    if (itA == m_layouts.end() || itB == m_layouts.end()) return false;

    const auto& a = itA->second;
    const auto& b = itB->second;

    for (u32 s = 0; s <= upToSet; ++s) {
        if (s >= a.setLayouts.size() || s >= b.setLayouts.size()) return false;

        auto issues = CheckSetCompat(a.setLayouts[s], b.setLayouts[s]);
        if (!issues.empty()) return false;
    }

    return true;
}

std::vector<LayoutIncompatibility> PipelineLayoutChecker::CheckPushConstantCompat(u64 layoutA, u64 layoutB) const {
    // Note: caller must already hold m_mutex if called from CheckCompatibility
    std::vector<LayoutIncompatibility> issues;

    auto itA = m_layouts.find(layoutA);
    auto itB = m_layouts.find(layoutB);
    if (itA == m_layouts.end() || itB == m_layouts.end()) return issues;

    const auto& pcA = itA->second.pushConstants;
    const auto& pcB = itB->second.pushConstants;

    // Check for overlapping ranges with different stage flags
    for (const auto& ra : pcA) {
        for (const auto& rb : pcB) {
            u32 aEnd = ra.offset + ra.size;
            u32 bEnd = rb.offset + rb.size;

            bool overlaps = ra.offset < bEnd && rb.offset < aEnd;
            if (overlaps) {
                if (ra.offset != rb.offset || ra.size != rb.size ||
                    static_cast<u32>(ra.stageFlags) != static_cast<u32>(rb.stageFlags)) {
                    LayoutIncompatibility issue;
                    issue.type = IncompatibilityType::PushConstantOverlap;
                    issue.setIndex = 0;
                    issue.bindingIndex = 0;
                    issue.description = "Push constant overlap: [" + std::to_string(ra.offset) + "," +
                                         std::to_string(aEnd) + ") vs [" + std::to_string(rb.offset) + "," +
                                         std::to_string(bEnd) + ")";
                    issues.push_back(std::move(issue));
                }
            }
        }
    }

    // Check for gaps in push constant ranges (within a single layout)
    if (m_config.checkPushConstantGaps) {
        auto checkGaps = [&](const std::vector<PushConstantRange>& ranges, const std::string& name) {
            if (ranges.size() < 2) return;

            // Sort by offset
            std::vector<PushConstantRange> sorted = ranges;
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto& a, const auto& b) { return a.offset < b.offset; });

            for (size_t i = 1; i < sorted.size(); ++i) {
                u32 prevEnd = sorted[i - 1].offset + sorted[i - 1].size;
                if (sorted[i].offset > prevEnd) {
                    LayoutIncompatibility issue;
                    issue.type = IncompatibilityType::PushConstantGap;
                    issue.setIndex = 0;
                    issue.bindingIndex = 0;
                    issue.description = "Push constant gap in '" + name + "': bytes [" +
                                         std::to_string(prevEnd) + "," + std::to_string(sorted[i].offset) + ")";
                    issues.push_back(std::move(issue));
                }
            }
        };

        checkGaps(pcA, itA->second.debugName);
        checkGaps(pcB, itB->second.debugName);
    }

    return issues;
}

const PipelineLayoutDesc* PipelineLayoutChecker::GetLayout(u64 layoutId) const {
    std::lock_guard lock(m_mutex);
    auto it = m_layouts.find(layoutId);
    if (it != m_layouts.end()) return &it->second;
    return nullptr;
}

void PipelineLayoutChecker::Reset() {
    std::lock_guard lock(m_mutex);
    m_layouts.clear();
    m_totalChecks = 0;
    m_compatiblePairs = 0;
    m_incompatiblePairs = 0;
    m_totalIncompats = 0;
}

PipelineLayoutCheckerStats PipelineLayoutChecker::GetStats() const {
    std::lock_guard lock(m_mutex);
    PipelineLayoutCheckerStats stats{};
    stats.totalLayouts = static_cast<u32>(m_layouts.size());
    stats.totalChecks = m_totalChecks;
    stats.compatiblePairs = m_compatiblePairs;
    stats.incompatiblePairs = m_incompatiblePairs;
    stats.totalIncompatibilities = m_totalIncompats;
    return stats;
}

std::vector<LayoutIncompatibility> PipelineLayoutChecker::CheckSetCompat(
    const PipelineSetLayoutDesc& a, const PipelineSetLayoutDesc& b) const {

    std::vector<LayoutIncompatibility> issues;
    u32 setIdx = a.setIndex;

    if (a.bindings.size() != b.bindings.size()) {
        LayoutIncompatibility issue;
        issue.type = IncompatibilityType::BindingCountMismatch;
        issue.setIndex = setIdx;
        issue.bindingIndex = 0;
        issue.description = "Set " + std::to_string(setIdx) + ": binding count " +
                             std::to_string(a.bindings.size()) + " vs " + std::to_string(b.bindings.size());
        issues.push_back(std::move(issue));
    }

    // Build map for B's bindings by binding index
    std::unordered_map<u32, const LayoutBinding*> bMap;
    for (const auto& bind : b.bindings) {
        bMap[bind.binding] = &bind;
    }

    // Compare each binding in A against B
    for (const auto& bindA : a.bindings) {
        auto it = bMap.find(bindA.binding);
        if (it == bMap.end()) {
            LayoutIncompatibility issue;
            issue.type = IncompatibilityType::SetLayoutMismatch;
            issue.setIndex = setIdx;
            issue.bindingIndex = bindA.binding;
            issue.description = "Set " + std::to_string(setIdx) + ": binding " +
                                 std::to_string(bindA.binding) + " missing in second layout";
            issues.push_back(std::move(issue));
            continue;
        }

        const auto& bindB = *it->second;

        if (bindA.type != bindB.type) {
            LayoutIncompatibility issue;
            issue.type = IncompatibilityType::BindingTypeMismatch;
            issue.setIndex = setIdx;
            issue.bindingIndex = bindA.binding;
            issue.description = "Set " + std::to_string(setIdx) + ", binding " +
                                 std::to_string(bindA.binding) + ": type mismatch";
            issues.push_back(std::move(issue));
        }

        if (static_cast<u32>(bindA.stageFlags) != static_cast<u32>(bindB.stageFlags)) {
            LayoutIncompatibility issue;
            issue.type = IncompatibilityType::BindingStageMismatch;
            issue.setIndex = setIdx;
            issue.bindingIndex = bindA.binding;
            issue.description = "Set " + std::to_string(setIdx) + ", binding " +
                                 std::to_string(bindA.binding) + ": stage flags mismatch";
            issues.push_back(std::move(issue));
        }

        if (bindA.count != bindB.count) {
            LayoutIncompatibility issue;
            issue.type = IncompatibilityType::BindingArraySizeMismatch;
            issue.setIndex = setIdx;
            issue.bindingIndex = bindA.binding;
            issue.description = "Set " + std::to_string(setIdx) + ", binding " +
                                 std::to_string(bindA.binding) + ": array size " +
                                 std::to_string(bindA.count) + " vs " + std::to_string(bindB.count);
            issues.push_back(std::move(issue));
        }
    }

    return issues;
}

} // namespace nge::rhi
