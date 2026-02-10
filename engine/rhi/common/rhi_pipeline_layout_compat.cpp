#include "engine/rhi/common/rhi_pipeline_layout_compat.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool PipelineLayoutCompatChecker::Init(const PipelineLayoutCompatConfig& config) {
    m_config = config;
    m_nextId = 0;
    m_totalValidations = 0;
    m_compatiblePairs = 0;
    m_incompatiblePairs = 0;
    m_pushConstantIssues = 0;
    m_setLayoutIssues = 0;

    NGE_LOG_INFO("Pipeline layout compat checker initialized: maxLayouts={}, maxSets={}, maxPushSize={}",
                 config.maxLayouts, config.maxDescriptorSets, config.maxPushConstantSize);
    return true;
}

void PipelineLayoutCompatChecker::Shutdown() {
    m_layouts.clear();
}

u32 PipelineLayoutCompatChecker::RegisterLayout(const std::vector<u64>& setLayoutHashes,
                                                   const std::vector<PushConstantRange>& pushConstants,
                                                   const std::string& name) {
    std::lock_guard lock(m_mutex);

    if (m_layouts.size() >= m_config.maxLayouts) {
        NGE_LOG_ERROR("Pipeline layout compat: max layouts reached ({})", m_config.maxLayouts);
        return UINT32_MAX;
    }

    u32 id = m_nextId++;

    PipelineLayoutDesc desc;
    desc.layoutId = id;
    desc.setLayoutHashes = setLayoutHashes;
    desc.pushConstantRanges = pushConstants;
    desc.debugName = name;

    m_layouts[id] = std::move(desc);
    return id;
}

bool PipelineLayoutCompatChecker::AreCompatible(u32 layoutA, u32 layoutB) const {
    std::lock_guard lock(m_mutex);
    m_totalValidations++;

    auto itA = m_layouts.find(layoutA);
    auto itB = m_layouts.find(layoutB);
    if (itA == m_layouts.end() || itB == m_layouts.end()) return false;

    const auto& a = itA->second;
    const auto& b = itB->second;

    // Compatible if all shared set indices have matching layout hashes
    u32 minSets = std::min(static_cast<u32>(a.setLayoutHashes.size()),
                            static_cast<u32>(b.setLayoutHashes.size()));

    for (u32 i = 0; i < minSets; ++i) {
        if (a.setLayoutHashes[i] != b.setLayoutHashes[i]) {
            m_incompatiblePairs++;
            m_setLayoutIssues++;
            return false;
        }
    }

    m_compatiblePairs++;
    return true;
}

bool PipelineLayoutCompatChecker::AreCompatibleUpToSet(u32 layoutA, u32 layoutB, u32 maxSet) const {
    std::lock_guard lock(m_mutex);
    m_totalValidations++;

    auto itA = m_layouts.find(layoutA);
    auto itB = m_layouts.find(layoutB);
    if (itA == m_layouts.end() || itB == m_layouts.end()) return false;

    const auto& a = itA->second;
    const auto& b = itB->second;

    u32 checkSets = std::min({maxSet + 1,
                               static_cast<u32>(a.setLayoutHashes.size()),
                               static_cast<u32>(b.setLayoutHashes.size())});

    for (u32 i = 0; i < checkSets; ++i) {
        if (a.setLayoutHashes[i] != b.setLayoutHashes[i]) {
            m_incompatiblePairs++;
            return false;
        }
    }

    m_compatiblePairs++;
    return true;
}

std::vector<CompatIssue> PipelineLayoutCompatChecker::ValidateLayout(u32 layoutId) const {
    std::lock_guard lock(m_mutex);
    m_totalValidations++;

    std::vector<CompatIssue> issues;

    auto it = m_layouts.find(layoutId);
    if (it == m_layouts.end()) return issues;

    const auto& desc = it->second;

    // Check set count
    if (desc.setLayoutHashes.size() > m_config.maxDescriptorSets) {
        CompatIssue issue;
        issue.type = CompatIssueType::TooManyDescriptorSets;
        issue.layoutA = layoutId;
        issue.layoutB = UINT32_MAX;
        issue.setIndex = static_cast<u32>(desc.setLayoutHashes.size());
        issue.description = "Layout has " + std::to_string(desc.setLayoutHashes.size()) +
                            " sets, max is " + std::to_string(m_config.maxDescriptorSets);
        issues.push_back(std::move(issue));
    }

    // Check for sparse sets (gaps with hash 0)
    if (m_config.warnOnSparseSet) {
        for (u32 i = 0; i < static_cast<u32>(desc.setLayoutHashes.size()); ++i) {
            if (desc.setLayoutHashes[i] == 0 && i < desc.setLayoutHashes.size() - 1) {
                CompatIssue issue;
                issue.type = CompatIssueType::MissingSetLayout;
                issue.layoutA = layoutId;
                issue.layoutB = UINT32_MAX;
                issue.setIndex = i;
                issue.description = "Set " + std::to_string(i) + " has null layout (sparse set gap)";
                issues.push_back(std::move(issue));
            }
        }
    }

    // Check push constant ranges
    u32 totalPushSize = 0;
    for (u32 i = 0; i < static_cast<u32>(desc.pushConstantRanges.size()); ++i) {
        const auto& rangeA = desc.pushConstantRanges[i];
        totalPushSize = std::max(totalPushSize, rangeA.offset + rangeA.size);

        // Check overlap with other ranges
        for (u32 j = i + 1; j < static_cast<u32>(desc.pushConstantRanges.size()); ++j) {
            const auto& rangeB = desc.pushConstantRanges[j];

            if (CheckPushConstantOverlap(rangeA, rangeB)) {
                // Check if they have overlapping stage flags
                if (rangeA.stageFlags & rangeB.stageFlags) {
                    CompatIssue issue;
                    issue.type = CompatIssueType::PushConstantStageConflict;
                    issue.layoutA = layoutId;
                    issue.layoutB = UINT32_MAX;
                    issue.setIndex = 0;
                    issue.description = "Push constant ranges '" + rangeA.debugName +
                                        "' and '" + rangeB.debugName +
                                        "' overlap with shared stage flags";
                    issues.push_back(std::move(issue));
                    m_pushConstantIssues++;
                }
            }
        }
    }

    if (totalPushSize > m_config.maxPushConstantSize) {
        CompatIssue issue;
        issue.type = CompatIssueType::PushConstantExceedsLimit;
        issue.layoutA = layoutId;
        issue.layoutB = UINT32_MAX;
        issue.setIndex = 0;
        issue.description = "Total push constant size " + std::to_string(totalPushSize) +
                            " exceeds limit " + std::to_string(m_config.maxPushConstantSize);
        issues.push_back(std::move(issue));
        m_pushConstantIssues++;
    }

    return issues;
}

std::vector<CompatIssue> PipelineLayoutCompatChecker::FindIssues(u32 layoutA, u32 layoutB) const {
    std::lock_guard lock(m_mutex);
    m_totalValidations++;

    std::vector<CompatIssue> issues;

    auto itA = m_layouts.find(layoutA);
    auto itB = m_layouts.find(layoutB);
    if (itA == m_layouts.end() || itB == m_layouts.end()) return issues;

    const auto& a = itA->second;
    const auto& b = itB->second;

    u32 minSets = std::min(static_cast<u32>(a.setLayoutHashes.size()),
                            static_cast<u32>(b.setLayoutHashes.size()));

    for (u32 i = 0; i < minSets; ++i) {
        if (a.setLayoutHashes[i] != b.setLayoutHashes[i]) {
            CompatIssue issue;
            issue.type = CompatIssueType::SetLayoutMismatch;
            issue.layoutA = layoutA;
            issue.layoutB = layoutB;
            issue.setIndex = i;
            issue.description = "Set " + std::to_string(i) + " layout mismatch between '" +
                                a.debugName + "' and '" + b.debugName + "'";
            issues.push_back(std::move(issue));
            m_setLayoutIssues++;
        }
    }

    return issues;
}

const PipelineLayoutDesc* PipelineLayoutCompatChecker::GetLayout(u32 layoutId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_layouts.find(layoutId);
    if (it == m_layouts.end()) return nullptr;
    return &it->second;
}

u32 PipelineLayoutCompatChecker::GetPushConstantSize(u32 layoutId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_layouts.find(layoutId);
    if (it == m_layouts.end()) return 0;

    u32 maxEnd = 0;
    for (const auto& range : it->second.pushConstantRanges) {
        u32 end = range.offset + range.size;
        if (end > maxEnd) maxEnd = end;
    }
    return maxEnd;
}

void PipelineLayoutCompatChecker::Unregister(u32 layoutId) {
    std::lock_guard lock(m_mutex);
    m_layouts.erase(layoutId);
}

u32 PipelineLayoutCompatChecker::GetLayoutCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_layouts.size());
}

void PipelineLayoutCompatChecker::Reset() {
    std::lock_guard lock(m_mutex);
    m_layouts.clear();
    m_nextId = 0;
    m_totalValidations = 0;
    m_compatiblePairs = 0;
    m_incompatiblePairs = 0;
    m_pushConstantIssues = 0;
    m_setLayoutIssues = 0;
}

PipelineLayoutCompatStats PipelineLayoutCompatChecker::GetStats() const {
    std::lock_guard lock(m_mutex);

    PipelineLayoutCompatStats stats{};
    stats.totalLayouts = static_cast<u32>(m_layouts.size());
    stats.totalValidations = m_totalValidations;
    stats.compatiblePairs = m_compatiblePairs;
    stats.incompatiblePairs = m_incompatiblePairs;
    stats.pushConstantIssues = m_pushConstantIssues;
    stats.setLayoutIssues = m_setLayoutIssues;

    return stats;
}

bool PipelineLayoutCompatChecker::CheckPushConstantOverlap(const PushConstantRange& a,
                                                              const PushConstantRange& b) const {
    u32 aEnd = a.offset + a.size;
    u32 bEnd = b.offset + b.size;
    return !(aEnd <= b.offset || bEnd <= a.offset);
}

} // namespace nge::rhi
