#include "engine/rhi/common/rhi_render_pass_manager.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool RenderPassManager::Init(IDevice* device, const RenderPassManagerConfig& config) {
    m_device = device;
    m_config = config;

    NGE_LOG_INFO("Render pass manager initialized: autoMerge={}, tileGPU={}, maxSubpasses={}",
                 config.enableAutoMerge, config.tileBasedGPU, config.maxSubpasses);
    return true;
}

void RenderPassManager::Shutdown() {
    m_submittedPasses.clear();
    m_mergedPasses.clear();
    m_passCache.clear();
}

void RenderPassManager::SubmitPassSequence(const std::vector<RPMRenderPassDesc>& passes) {
    std::lock_guard lock(m_mutex);
    m_submittedPasses = passes;
    m_mergedPasses.clear();

    if (!m_config.enableAutoMerge || passes.empty()) {
        // No merging — each pass is its own merged pass
        for (const auto& pass : passes) {
            MergedRenderPass merged;
            merged.subpasses = {pass};
            merged.allAttachments = pass.colorAttachments;
            if (pass.hasDepth) merged.allAttachments.push_back(pass.depthAttachment);
            merged.debugName = pass.name;
            merged.hash = HashPass(merged);
            m_mergedPasses.push_back(std::move(merged));
        }
        return;
    }

    // Greedy forward merge
    size_t i = 0;
    while (i < passes.size()) {
        MergedRenderPass merged;
        merged.subpasses.push_back(passes[i]);
        merged.debugName = passes[i].name;

        // Try to merge subsequent passes
        size_t j = i + 1;
        while (j < passes.size() &&
               merged.subpasses.size() < m_config.maxSubpasses &&
               CanMerge(merged.subpasses.back(), passes[j])) {

            // Build subpass dependency
            auto dep = BuildDependency(
                static_cast<u32>(merged.subpasses.size() - 1),
                static_cast<u32>(merged.subpasses.size()),
                merged.subpasses.back(), passes[j]);
            merged.dependencies.push_back(dep);

            merged.subpasses.push_back(passes[j]);
            merged.debugName += "+" + passes[j].name;
            j++;
        }

        // Deduplicate attachments
        for (const auto& subpass : merged.subpasses) {
            for (const auto& att : subpass.colorAttachments) {
                bool found = false;
                for (const auto& existing : merged.allAttachments) {
                    if (existing.textureHandle == att.textureHandle) { found = true; break; }
                }
                if (!found) merged.allAttachments.push_back(att);
            }
            if (subpass.hasDepth) {
                bool found = false;
                for (const auto& existing : merged.allAttachments) {
                    if (existing.textureHandle == subpass.depthAttachment.textureHandle) { found = true; break; }
                }
                if (!found) merged.allAttachments.push_back(subpass.depthAttachment);
            }
        }

        if (merged.allAttachments.size() > m_config.maxAttachments) {
            // Too many attachments — split back into individual passes
            for (const auto& subpass : merged.subpasses) {
                MergedRenderPass single;
                single.subpasses = {subpass};
                single.allAttachments = subpass.colorAttachments;
                if (subpass.hasDepth) single.allAttachments.push_back(subpass.depthAttachment);
                single.debugName = subpass.name;
                single.hash = HashPass(single);
                m_mergedPasses.push_back(std::move(single));
            }
        } else {
            merged.hash = HashPass(merged);
            m_mergedPasses.push_back(std::move(merged));
        }

        i = j;
    }

    NGE_LOG_INFO("Render pass merging: {} input passes -> {} merged passes",
                 passes.size(), m_mergedPasses.size());
}

std::vector<MergeOpportunity> RenderPassManager::AnalyzeMergeOpportunities() const {
    std::lock_guard lock(m_mutex);
    std::vector<MergeOpportunity> opportunities;

    for (size_t i = 0; i + 1 < m_submittedPasses.size(); ++i) {
        MergeOpportunity opp;
        opp.passA = static_cast<u32>(i);
        opp.passB = static_cast<u32>(i + 1);

        const auto& a = m_submittedPasses[i];
        const auto& b = m_submittedPasses[i + 1];

        if (a.width != b.width || a.height != b.height) {
            opp.canMerge = false;
            opp.reason = "Resolution mismatch";
            opp.savingsEstimate = 0.0f;
        } else if (a.samples != b.samples) {
            opp.canMerge = false;
            opp.reason = "Sample count mismatch";
            opp.savingsEstimate = 0.0f;
        } else if (!AttachmentsCompatible(a, b)) {
            opp.canMerge = false;
            opp.reason = "Incompatible attachments";
            opp.savingsEstimate = 0.0f;
        } else {
            opp.canMerge = true;
            opp.reason = "Compatible for subpass merge";
            // Estimate savings: shared attachments save load/store
            u32 shared = 0;
            for (const auto& attA : a.colorAttachments) {
                for (const auto& attB : b.colorAttachments) {
                    if (attA.textureHandle == attB.textureHandle) shared++;
                }
            }
            if (a.hasDepth && b.hasDepth &&
                a.depthAttachment.textureHandle == b.depthAttachment.textureHandle) shared++;

            u32 total = static_cast<u32>(a.colorAttachments.size() + b.colorAttachments.size());
            if (a.hasDepth) total++;
            if (b.hasDepth) total++;

            opp.savingsEstimate = total > 0 ? static_cast<f32>(shared) / static_cast<f32>(total) : 0.0f;

            // TBDR GPUs benefit more from merging
            if (m_config.tileBasedGPU) {
                opp.savingsEstimate = std::min(opp.savingsEstimate * 1.5f, 1.0f);
            }
        }

        opportunities.push_back(opp);
    }

    return opportunities;
}

std::vector<MergedRenderPass> RenderPassManager::GetMergedPasses() const {
    std::lock_guard lock(m_mutex);
    return m_mergedPasses;
}

const MergedRenderPass* RenderPassManager::GetMergedPass(u64 hash) const {
    std::lock_guard lock(m_mutex);
    auto it = m_passCache.find(hash);
    if (it != m_passCache.end()) return &it->second;

    for (const auto& pass : m_mergedPasses) {
        if (pass.hash == hash) return &pass;
    }
    return nullptr;
}

bool RenderPassManager::CanMerge(const RPMRenderPassDesc& a, const RPMRenderPassDesc& b) const {
    // Must have same resolution and sample count
    if (a.width != b.width || a.height != b.height) return false;
    if (a.samples != b.samples) return false;

    // Must share at least one attachment (otherwise no benefit)
    if (!AttachmentsCompatible(a, b)) return false;

    // Total attachments must not exceed limit
    u32 totalAttachments = static_cast<u32>(a.colorAttachments.size() + b.colorAttachments.size());
    if (a.hasDepth) totalAttachments++;
    if (b.hasDepth) totalAttachments++;
    if (totalAttachments > m_config.maxAttachments) return false;

    return true;
}

MergedRenderPass RenderPassManager::MergePasses(const RPMRenderPassDesc& a, const RPMRenderPassDesc& b) const {
    MergedRenderPass merged;
    merged.subpasses = {a, b};
    merged.dependencies = {BuildDependency(0, 1, a, b)};
    merged.debugName = a.name + "+" + b.name;

    // Deduplicate attachments
    merged.allAttachments = a.colorAttachments;
    if (a.hasDepth) merged.allAttachments.push_back(a.depthAttachment);
    for (const auto& att : b.colorAttachments) {
        bool found = false;
        for (const auto& existing : merged.allAttachments) {
            if (existing.textureHandle == att.textureHandle) { found = true; break; }
        }
        if (!found) merged.allAttachments.push_back(att);
    }
    if (b.hasDepth) {
        bool found = false;
        for (const auto& existing : merged.allAttachments) {
            if (existing.textureHandle == b.depthAttachment.textureHandle) { found = true; break; }
        }
        if (!found) merged.allAttachments.push_back(b.depthAttachment);
    }

    merged.hash = HashPass(merged);
    return merged;
}

void RenderPassManager::Clear() {
    std::lock_guard lock(m_mutex);
    m_submittedPasses.clear();
    m_mergedPasses.clear();
}

RenderPassManagerStats RenderPassManager::GetStats() const {
    std::lock_guard lock(m_mutex);
    RenderPassManagerStats stats{};
    stats.totalPasses = static_cast<u32>(m_submittedPasses.size());
    stats.mergedPasses = static_cast<u32>(m_mergedPasses.size());

    u32 totalSubpasses = 0;
    u32 mergeOps = 0;
    for (const auto& merged : m_mergedPasses) {
        totalSubpasses += static_cast<u32>(merged.subpasses.size());
        if (merged.subpasses.size() > 1) mergeOps++;
    }
    stats.subpassesGenerated = totalSubpasses;
    stats.mergeOpportunities = mergeOps;

    stats.estimatedBandwidthSavings = stats.totalPasses > 0
        ? 1.0f - static_cast<f32>(stats.mergedPasses) / static_cast<f32>(stats.totalPasses)
        : 0.0f;

    return stats;
}

u64 RenderPassManager::HashPass(const MergedRenderPass& pass) const {
    u64 hash = 14695981039346656037ULL;
    for (const auto& subpass : pass.subpasses) {
        for (auto c : subpass.name) {
            hash ^= static_cast<u64>(c);
            hash *= 1099511628211ULL;
        }
        hash ^= static_cast<u64>(subpass.width);  hash *= 1099511628211ULL;
        hash ^= static_cast<u64>(subpass.height); hash *= 1099511628211ULL;
        hash ^= static_cast<u64>(subpass.samples); hash *= 1099511628211ULL;
        for (const auto& att : subpass.colorAttachments) {
            hash ^= att.textureHandle; hash *= 1099511628211ULL;
        }
    }
    return hash;
}

bool RenderPassManager::AttachmentsCompatible(const RPMRenderPassDesc& a, const RPMRenderPassDesc& b) const {
    // Check if any attachment is shared (input attachment pattern)
    for (const auto& attA : a.colorAttachments) {
        for (const auto& attB : b.colorAttachments) {
            if (attA.textureHandle == attB.textureHandle) return true;
        }
    }
    // Shared depth
    if (a.hasDepth && b.hasDepth &&
        a.depthAttachment.textureHandle == b.depthAttachment.textureHandle) return true;

    // Check if b reads from a's output (input attachment)
    // This would require dependency information not yet modeled
    // For now, allow merging if resolutions match
    return m_config.tileBasedGPU; // Only auto-merge on tile GPUs when no shared attachments
}

SubpassDependency RenderPassManager::BuildDependency(u32 srcSubpass, u32 dstSubpass,
                                                       const RPMRenderPassDesc& src,
                                                       const RPMRenderPassDesc& dst) const {
    SubpassDependency dep;
    dep.srcSubpass = srcSubpass;
    dep.dstSubpass = dstSubpass;

    // Conservative stage/access masks
    dep.srcStageMask = 0x00000400; // VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    dep.dstStageMask = 0x00000080; // VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    dep.srcAccessMask = 0x00000100; // VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
    dep.dstAccessMask = 0x00000020; // VK_ACCESS_INPUT_ATTACHMENT_READ_BIT
    dep.byRegion = true;

    // If depth is shared, add depth stage dependencies
    if (src.hasDepth && dst.hasDepth &&
        src.depthAttachment.textureHandle == dst.depthAttachment.textureHandle) {
        dep.srcStageMask |= 0x00000200; // VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
        dep.dstStageMask |= 0x00000100; // VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
        dep.srcAccessMask |= 0x00000400; // VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
        dep.dstAccessMask |= 0x00000200; // VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
    }

    (void)dst;
    return dep;
}

} // namespace nge::rhi
