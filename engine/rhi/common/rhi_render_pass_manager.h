#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Render Pass Manager ─────────────────────────────────────────────
// Automatic pass merging and subpass dependency resolution. Analyzes
// sequential render passes to determine which can be merged into a single
// Vulkan render pass with subpasses, reducing load/store operations.
//
// When VK_KHR_dynamic_rendering is used, this acts as an optimization
// advisor that suggests merge opportunities for tile-based GPUs.

enum class AttachmentLoadOp : u8 {
    Load,
    Clear,
    DontCare,
};

enum class AttachmentStoreOp : u8 {
    Store,
    DontCare,
};

struct PassAttachment {
    u64              textureHandle;
    u32              format;        // RHI format enum
    AttachmentLoadOp loadOp;
    AttachmentStoreOp storeOp;
    bool             isDepthStencil;
    std::string      debugName;
};

struct RenderPassDesc {
    std::string                  name;
    std::vector<PassAttachment>  colorAttachments;
    PassAttachment               depthAttachment;
    bool                         hasDepth = false;
    u32                          width;
    u32                          height;
    u32                          samples = 1;
};

struct SubpassDependency {
    u32 srcSubpass;
    u32 dstSubpass;
    u32 srcStageMask;
    u32 dstStageMask;
    u32 srcAccessMask;
    u32 dstAccessMask;
    bool byRegion;
};

struct MergedRenderPass {
    std::vector<RenderPassDesc>  subpasses;
    std::vector<SubpassDependency> dependencies;
    std::vector<PassAttachment>  allAttachments;  // Deduplicated
    u64                          hash;
    std::string                  debugName;
};

struct MergeOpportunity {
    u32  passA;
    u32  passB;
    f32  savingsEstimate;  // Estimated bandwidth savings (0-1)
    bool canMerge;
    std::string reason;    // Why merge is/isn't possible
};

struct RenderPassManagerConfig {
    bool enableAutoMerge = true;
    bool tileBasedGPU = false;       // More aggressive merging for TBDR
    u32  maxSubpasses = 8;
    u32  maxAttachments = 16;
};

struct RenderPassManagerStats {
    u32 totalPasses;
    u32 mergedPasses;
    u32 mergeOpportunities;
    u32 subpassesGenerated;
    f32 estimatedBandwidthSavings;
};

class RenderPassManager {
public:
    bool Init(IDevice* device, const RenderPassManagerConfig& config = {});
    void Shutdown();

    // Submit a sequence of render passes for analysis
    void SubmitPassSequence(const std::vector<RenderPassDesc>& passes);

    // Get merge opportunities (advisory)
    std::vector<MergeOpportunity> AnalyzeMergeOpportunities() const;

    // Get merged render passes (optimized sequence)
    std::vector<MergedRenderPass> GetMergedPasses() const;

    // Get a specific merged pass by hash
    const MergedRenderPass* GetMergedPass(u64 hash) const;

    // Check if two passes can be merged
    bool CanMerge(const RenderPassDesc& a, const RenderPassDesc& b) const;

    // Force-merge two passes (manual override)
    MergedRenderPass MergePasses(const RenderPassDesc& a, const RenderPassDesc& b) const;

    // Clear all submitted passes
    void Clear();

    RenderPassManagerStats GetStats() const;

private:
    u64 HashPass(const MergedRenderPass& pass) const;
    bool AttachmentsCompatible(const RenderPassDesc& a, const RenderPassDesc& b) const;
    SubpassDependency BuildDependency(u32 srcSubpass, u32 dstSubpass,
                                       const RenderPassDesc& src, const RenderPassDesc& dst) const;

    IDevice* m_device = nullptr;
    RenderPassManagerConfig m_config;

    std::vector<RenderPassDesc> m_submittedPasses;
    std::vector<MergedRenderPass> m_mergedPasses;
    std::unordered_map<u64, MergedRenderPass> m_passCache;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
