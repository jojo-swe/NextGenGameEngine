#include "engine/rhi/common/rhi_renderpass_merge_optimizer.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool RenderPassMergeOptimizer::Init(const RenderPassMergeConfig& config) {
    m_config = config;
    m_passes.reserve(config.maxPasses);
    m_mergeAttempts = 0;
    m_mergeSuccesses = 0;
    m_mergeRejections = 0;
    m_maxSubpasses = 0;
    m_attachmentsSaved = 0;

    NGE_LOG_INFO("Render pass merge optimizer initialized: maxPasses={}, maxSubpasses={}, seqOnly={}",
                 config.maxPasses, config.rules.maxSubpassesPerMerge, config.rules.mergeSequentialOnly);
    return true;
}

void RenderPassMergeOptimizer::Shutdown() {
    m_passes.clear();
    m_passIndex.clear();
}

void RenderPassMergeOptimizer::DeclarePass(const RenderPassDecl& pass) {
    std::lock_guard lock(m_mutex);

    if (m_passes.size() >= m_config.maxPasses) {
        NGE_LOG_WARN("Render pass merge optimizer: max passes reached ({})", m_config.maxPasses);
        return;
    }

    u32 index = static_cast<u32>(m_passes.size());
    m_passIndex[pass.passId] = index;
    m_passes.push_back(pass);
}

std::vector<MergedPass> RenderPassMergeOptimizer::Optimize() {
    std::lock_guard lock(m_mutex);

    std::vector<MergedPass> result;
    if (m_passes.empty()) return result;

    // Greedy sequential merging
    std::vector<std::vector<u32>> mergeGroups;
    std::vector<u32> currentGroup;
    currentGroup.push_back(0);

    for (u32 i = 1; i < static_cast<u32>(m_passes.size()); ++i) {
        m_mergeAttempts++;

        bool canMerge = true;

        // Check compatibility with every pass in the current group
        for (u32 idx : currentGroup) {
            if (!CheckCompatibility(m_passes[idx], m_passes[i])) {
                canMerge = false;
                break;
            }
        }

        // Check max subpasses limit
        if (currentGroup.size() >= m_config.rules.maxSubpassesPerMerge) {
            canMerge = false;
        }

        // Check for resource dependencies that prevent merging
        // (output of earlier pass used as non-input-attachment in later pass)
        if (canMerge && !m_config.rules.allowInputAttachmentMerge) {
            if (HasResourceDependency(m_passes[currentGroup.back()], m_passes[i])) {
                canMerge = false;
            }
        }

        if (canMerge) {
            currentGroup.push_back(i);
            m_mergeSuccesses++;
        } else {
            m_mergeRejections++;
            mergeGroups.push_back(std::move(currentGroup));
            currentGroup.clear();
            currentGroup.push_back(i);
        }
    }

    // Don't forget the last group
    if (!currentGroup.empty()) {
        mergeGroups.push_back(std::move(currentGroup));
    }

    // Build merged passes from groups
    for (const auto& group : mergeGroups) {
        std::vector<const RenderPassDecl*> passes;
        for (u32 idx : group) {
            passes.push_back(&m_passes[idx]);
        }
        result.push_back(MergePasses(passes));

        u32 subpasses = static_cast<u32>(group.size());
        if (subpasses > m_maxSubpasses) m_maxSubpasses = subpasses;

        // Attachments saved: each merge eliminates intermediate load/stores
        if (subpasses > 1) {
            m_attachmentsSaved += (subpasses - 1) * 2; // Approximate: save 1 load + 1 store per merge
        }
    }

    NGE_LOG_INFO("Render pass merge: {} passes -> {} merged ({} eliminated)",
                 m_passes.size(), result.size(), m_passes.size() - result.size());

    return result;
}

bool RenderPassMergeOptimizer::AreMergeable(u32 passIdA, u32 passIdB) const {
    std::lock_guard lock(m_mutex);

    auto itA = m_passIndex.find(passIdA);
    auto itB = m_passIndex.find(passIdB);
    if (itA == m_passIndex.end() || itB == m_passIndex.end()) return false;

    return CheckCompatibility(m_passes[itA->second], m_passes[itB->second]);
}

const RenderPassDecl* RenderPassMergeOptimizer::GetPass(u32 passId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_passIndex.find(passId);
    if (it == m_passIndex.end()) return nullptr;
    return &m_passes[it->second];
}

void RenderPassMergeOptimizer::Clear() {
    std::lock_guard lock(m_mutex);
    m_passes.clear();
    m_passIndex.clear();
    m_mergeAttempts = 0;
    m_mergeSuccesses = 0;
    m_mergeRejections = 0;
    m_maxSubpasses = 0;
    m_attachmentsSaved = 0;
}

RenderPassMergeStats RenderPassMergeOptimizer::GetStats() const {
    std::lock_guard lock(m_mutex);
    RenderPassMergeStats stats{};
    stats.totalPassesDeclared = static_cast<u32>(m_passes.size());
    stats.totalMergedPasses = 0; // Computed after Optimize()
    stats.passesEliminated = 0;
    stats.maxSubpassesInMerge = m_maxSubpasses;
    stats.totalAttachmentsSaved = m_attachmentsSaved;
    stats.mergeAttempts = m_mergeAttempts;
    stats.mergeSuccesses = m_mergeSuccesses;
    stats.mergeRejections = m_mergeRejections;
    return stats;
}

bool RenderPassMergeOptimizer::CheckCompatibility(const RenderPassDecl& a, const RenderPassDecl& b) const {
    const auto& rules = m_config.rules;

    // Resolution must match
    if (rules.requireSameResolution) {
        if (a.width != b.width || a.height != b.height) return false;
    }

    // Sample count must match
    if (rules.requireSameSampleCount) {
        if (a.samples != b.samples) return false;
    }

    return true;
}

bool RenderPassMergeOptimizer::HasResourceDependency(const RenderPassDecl& writer,
                                                       const RenderPassDecl& reader) const {
    // Check if any output of writer is used as non-input-attachment input in reader
    std::unordered_set<u64> writerOutputs;
    for (const auto& att : writer.colorAttachments) {
        if (att.usage == AttachmentUsage::ColorOutput) {
            writerOutputs.insert(att.resourceHandle);
        }
    }
    if (writer.hasDepth && writer.depthAttachment.usage == AttachmentUsage::DepthStencilOutput) {
        writerOutputs.insert(writer.depthAttachment.resourceHandle);
    }

    // Check reader's color attachments for reads from writer's outputs
    for (const auto& att : reader.colorAttachments) {
        if (att.usage == AttachmentUsage::InputAttachment) {
            if (writerOutputs.count(att.resourceHandle)) return true;
        }
    }

    return false;
}

MergedPass RenderPassMergeOptimizer::MergePasses(const std::vector<const RenderPassDecl*>& passes) const {
    MergedPass merged;
    merged.subpassCount = static_cast<u32>(passes.size());
    merged.width = passes[0]->width;
    merged.height = passes[0]->height;
    merged.samples = passes[0]->samples;

    // Build debug name
    merged.debugName = "Merged[";
    for (size_t i = 0; i < passes.size(); ++i) {
        merged.originalPassIds.push_back(passes[i]->passId);
        if (i > 0) merged.debugName += "+";
        merged.debugName += passes[i]->debugName;
    }
    merged.debugName += "]";

    // Collect unique attachments across all subpasses
    std::unordered_set<u64> seenResources;
    for (const auto* pass : passes) {
        for (const auto& att : pass->colorAttachments) {
            if (seenResources.insert(att.resourceHandle).second) {
                merged.allAttachments.push_back(att);
            }
        }
        if (pass->hasDepth) {
            if (seenResources.insert(pass->depthAttachment.resourceHandle).second) {
                merged.allAttachments.push_back(pass->depthAttachment);
            }
        }
    }

    return merged;
}

} // namespace nge::rhi
