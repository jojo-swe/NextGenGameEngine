#include "engine/rhi/common/rhi_render_pass_merge_optimizer.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool RenderPassMergeOptimizer::Init(const RenderPassMergeConfig& config) {
    m_config = config;
    m_optimized = false;

    NGE_LOG_INFO("Render pass merge optimizer initialized: maxPasses={}, merge={}, sameRes={}, sameSamples={}",
                 config.maxPasses, config.enableMerging, config.requireSameResolution, config.requireSameSampleCount);
    return true;
}

void RenderPassMergeOptimizer::Shutdown() {
    m_passes.clear();
    m_passOrder.clear();
    m_mergedGroups.clear();
    m_passToGroup.clear();
}

bool RenderPassMergeOptimizer::AddPass(const MergeRenderPassDesc& pass) {
    std::lock_guard lock(m_mutex);

    if (m_passes.size() >= m_config.maxPasses) {
        NGE_LOG_WARN("Render pass merge: max passes reached ({})", m_config.maxPasses);
        return false;
    }

    if (pass.attachments.size() > m_config.maxAttachmentsPerPass) {
        NGE_LOG_WARN("Render pass merge: pass '{}' exceeds max attachments ({})", pass.name, m_config.maxAttachmentsPerPass);
        return false;
    }

    m_passes[pass.passId] = pass;
    m_passOrder.push_back(pass.passId);
    m_optimized = false;
    return true;
}

void RenderPassMergeOptimizer::Optimize() {
    std::lock_guard lock(m_mutex);

    m_mergedGroups.clear();
    m_passToGroup.clear();

    if (!m_config.enableMerging || m_passOrder.empty()) {
        // Each pass is its own group
        for (u32 id : m_passOrder) {
            MergedPassGroup group;
            group.passIds = {id};
            group.mergedName = m_passes[id].name;
            group.width = m_passes[id].width;
            group.height = m_passes[id].height;
            group.sampleCount = m_passes[id].sampleCount;
            group.loadOpsSaved = 0;
            group.storeOpsSaved = 0;

            m_passToGroup[id] = static_cast<u32>(m_mergedGroups.size());
            m_mergedGroups.push_back(std::move(group));
        }
        m_optimized = true;
        return;
    }

    // Greedy merge: try to merge consecutive passes
    std::vector<bool> merged(m_passOrder.size(), false);

    for (size_t i = 0; i < m_passOrder.size(); ++i) {
        if (merged[i]) continue;

        u32 baseId = m_passOrder[i];
        const auto& basePass = m_passes[baseId];

        MergedPassGroup group;
        group.passIds = {baseId};
        group.mergedName = basePass.name;
        group.width = basePass.width;
        group.height = basePass.height;
        group.sampleCount = basePass.sampleCount;

        // Try to merge subsequent passes
        for (size_t j = i + 1; j < m_passOrder.size(); ++j) {
            if (merged[j]) continue;

            u32 candidateId = m_passOrder[j];
            const auto& candidatePass = m_passes[candidateId];

            // Check if candidate depends on any pass not yet in this group
            bool hasExternalDep = false;
            for (u32 dep : candidatePass.dependsOn) {
                if (dep != baseId) {
                    // Check if dep is in current group
                    bool inGroup = false;
                    for (u32 gid : group.passIds) {
                        if (gid == dep) { inGroup = true; break; }
                    }
                    if (!inGroup) {
                        hasExternalDep = true;
                        break;
                    }
                }
            }

            if (hasExternalDep) continue;

            if (CanMerge(basePass, candidatePass)) {
                group.passIds.push_back(candidateId);
                group.mergedName += "+" + candidatePass.name;
                merged[j] = true;
            }
        }

        group.loadOpsSaved = CountLoadOpsSaved(group);
        group.storeOpsSaved = CountStoreOpsSaved(group);

        for (u32 pid : group.passIds) {
            m_passToGroup[pid] = static_cast<u32>(m_mergedGroups.size());
        }

        m_mergedGroups.push_back(std::move(group));
    }

    m_optimized = true;
}

const std::vector<MergedPassGroup>& RenderPassMergeOptimizer::GetMergedGroups() const {
    return m_mergedGroups;
}

u32 RenderPassMergeOptimizer::GetMergedGroupCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_mergedGroups.size());
}

bool RenderPassMergeOptimizer::AreCompatible(u32 passIdA, u32 passIdB) const {
    std::lock_guard lock(m_mutex);

    auto itA = m_passes.find(passIdA);
    auto itB = m_passes.find(passIdB);
    if (itA == m_passes.end() || itB == m_passes.end()) return false;

    return CanMerge(itA->second, itB->second);
}

bool RenderPassMergeOptimizer::IsMerged(u32 passId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_passToGroup.find(passId);
    if (it == m_passToGroup.end()) return false;

    u32 groupIdx = it->second;
    if (groupIdx >= m_mergedGroups.size()) return false;

    return m_mergedGroups[groupIdx].passIds.size() > 1;
}

u32 RenderPassMergeOptimizer::GetGroupForPass(u32 passId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_passToGroup.find(passId);
    if (it == m_passToGroup.end()) return UINT32_MAX;

    return it->second;
}

const MergeRenderPassDesc* RenderPassMergeOptimizer::GetPass(u32 passId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_passes.find(passId);
    if (it == m_passes.end()) return nullptr;

    return &it->second;
}

u32 RenderPassMergeOptimizer::GetPassCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_passes.size());
}

void RenderPassMergeOptimizer::Clear() {
    std::lock_guard lock(m_mutex);
    m_passes.clear();
    m_passOrder.clear();
    m_mergedGroups.clear();
    m_passToGroup.clear();
    m_optimized = false;
}

void RenderPassMergeOptimizer::Reset() {
    Clear();
}

RenderPassMergeStats RenderPassMergeOptimizer::GetStats() const {
    std::lock_guard lock(m_mutex);

    RenderPassMergeStats stats{};
    stats.totalPasses = static_cast<u32>(m_passes.size());
    stats.mergedGroups = static_cast<u32>(m_mergedGroups.size());

    u32 mergedCount = 0;
    u32 loadSaved = 0;
    u32 storeSaved = 0;

    for (const auto& group : m_mergedGroups) {
        if (group.passIds.size() > 1) {
            mergedCount += static_cast<u32>(group.passIds.size());
        }
        loadSaved += group.loadOpsSaved;
        storeSaved += group.storeOpsSaved;
    }

    stats.passesMerged = mergedCount;
    stats.passesUnmerged = stats.totalPasses - mergedCount;
    stats.loadOpsSaved = loadSaved;
    stats.storeOpsSaved = storeSaved;
    stats.mergeRatio = stats.totalPasses > 0
        ? static_cast<float>(stats.mergedGroups) / static_cast<float>(stats.totalPasses)
        : 1.0f;

    return stats;
}

bool RenderPassMergeOptimizer::CanMerge(const MergeRenderPassDesc& a, const MergeRenderPassDesc& b) const {
    // Resolution must match
    if (m_config.requireSameResolution && (a.width != b.width || a.height != b.height)) {
        return false;
    }

    // Sample count must match
    if (m_config.requireSameSampleCount && a.sampleCount != b.sampleCount) {
        return false;
    }

    // Total attachments must not exceed limit
    // Count unique attachments
    std::unordered_set<u32> uniqueAttachments;
    for (const auto& att : a.attachments) uniqueAttachments.insert(att.attachmentId);
    for (const auto& att : b.attachments) uniqueAttachments.insert(att.attachmentId);

    if (uniqueAttachments.size() > m_config.maxAttachmentsPerPass) {
        return false;
    }

    // Check for conflicting attachment usage (both writing to same attachment differently)
    for (const auto& attA : a.attachments) {
        for (const auto& attB : b.attachments) {
            if (attA.attachmentId == attB.attachmentId) {
                // Format must match
                if (attA.format != attB.format) return false;
            }
        }
    }

    return true;
}

bool RenderPassMergeOptimizer::HasDependency(u32 fromPass, u32 toPass) const {
    auto it = m_passes.find(fromPass);
    if (it == m_passes.end()) return false;

    for (u32 dep : it->second.dependsOn) {
        if (dep == toPass) return true;
    }
    return false;
}

u32 RenderPassMergeOptimizer::CountLoadOpsSaved(const MergedPassGroup& group) const {
    if (group.passIds.size() <= 1) return 0;

    u32 saved = 0;

    // For each pass after the first, count attachments that were stored by a
    // previous pass in the group and loaded by this pass -> those loads are saved
    for (size_t i = 1; i < group.passIds.size(); ++i) {
        auto it = m_passes.find(group.passIds[i]);
        if (it == m_passes.end()) continue;

        for (const auto& att : it->second.attachments) {
            if (att.loadOp == PassAttachmentOp::Load) {
                // Check if a previous pass in group wrote to this attachment
                for (size_t j = 0; j < i; ++j) {
                    auto prevIt = m_passes.find(group.passIds[j]);
                    if (prevIt == m_passes.end()) continue;

                    for (const auto& prevAtt : prevIt->second.attachments) {
                        if (prevAtt.attachmentId == att.attachmentId && prevAtt.isOutput) {
                            saved++;
                            goto nextAtt;
                        }
                    }
                }
            }
            nextAtt:;
        }
    }

    return saved;
}

u32 RenderPassMergeOptimizer::CountStoreOpsSaved(const MergedPassGroup& group) const {
    if (group.passIds.size() <= 1) return 0;

    u32 saved = 0;

    // For each pass before the last, count attachments that are stored and
    // then loaded by a subsequent pass in the group -> those stores are saved
    for (size_t i = 0; i < group.passIds.size() - 1; ++i) {
        auto it = m_passes.find(group.passIds[i]);
        if (it == m_passes.end()) continue;

        for (const auto& att : it->second.attachments) {
            if (att.storeOp == PassStoreOp::Store && att.isOutput) {
                for (size_t j = i + 1; j < group.passIds.size(); ++j) {
                    auto nextIt = m_passes.find(group.passIds[j]);
                    if (nextIt == m_passes.end()) continue;

                    for (const auto& nextAtt : nextIt->second.attachments) {
                        if (nextAtt.attachmentId == att.attachmentId && nextAtt.isInput) {
                            saved++;
                            goto nextStoreAtt;
                        }
                    }
                }
            }
            nextStoreAtt:;
        }
    }

    return saved;
}

} // namespace nge::rhi
