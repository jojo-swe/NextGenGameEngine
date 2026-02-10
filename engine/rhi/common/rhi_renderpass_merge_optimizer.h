#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace nge::rhi {

// ─── GPU Render Pass Merge Optimizer ─────────────────────────────────────
// Analyzes a sequence of render passes and merges compatible ones into
// single render passes with multiple subpasses for reduced load/store
// overhead and improved tile-based GPU utilization.
//
// Use cases:
//   - GBuffer + lighting resolve → single tiled render pass
//   - Shadow cascade passes → merged if same depth format
//   - Post-process chain merging (same resolution, sequential)
//   - Tile-based GPU optimization (reduce framebuffer loads/stores)

enum class AttachmentLoadOp : u8 {
    Load,
    Clear,
    DontCare,
};

enum class AttachmentStoreOp : u8 {
    Store,
    DontCare,
};

enum class AttachmentUsage : u8 {
    ColorOutput,
    DepthStencilOutput,
    InputAttachment,
    Resolve,
    Preserve,
};

struct PassAttachment {
    u64              resourceHandle;
    u32              format;          // RHI format enum value
    u32              width;
    u32              height;
    AttachmentLoadOp loadOp;
    AttachmentStoreOp storeOp;
    AttachmentUsage  usage;
};

struct RenderPassDecl {
    u32                        passId;
    std::string                debugName;
    std::vector<PassAttachment> colorAttachments;
    PassAttachment             depthAttachment;
    bool                       hasDepth;
    u32                        width;
    u32                        height;
    u32                        samples;          // MSAA sample count
    bool                       usesInputAttachments;
};

struct MergedPass {
    std::vector<u32>  originalPassIds;
    std::string       debugName;
    u32               subpassCount;
    u32               width;
    u32               height;
    u32               samples;
    // Unique attachments across all merged subpasses
    std::vector<PassAttachment> allAttachments;
};

struct MergeRule {
    bool requireSameResolution = true;
    bool requireSameSampleCount = true;
    bool allowInputAttachmentMerge = true;
    u32  maxSubpassesPerMerge = 8;
    bool mergeSequentialOnly = true;   // Only merge adjacent passes
};

struct RenderPassMergeConfig {
    u32       maxPasses = 256;
    MergeRule rules;
    bool      enableStats = true;
};

struct RenderPassMergeStats {
    u32 totalPassesDeclared;
    u32 totalMergedPasses;
    u32 passesEliminated;       // Original - merged
    u32 maxSubpassesInMerge;
    u32 totalAttachmentsSaved;  // Load/stores avoided
    u32 mergeAttempts;
    u32 mergeSuccesses;
    u32 mergeRejections;
};

class RenderPassMergeOptimizer {
public:
    bool Init(const RenderPassMergeConfig& config = {});
    void Shutdown();

    // Declare passes in execution order
    void DeclarePass(const RenderPassDecl& pass);

    // Run the merge optimization
    std::vector<MergedPass> Optimize();

    // Check if two specific passes are merge-compatible
    bool AreMergeable(u32 passIdA, u32 passIdB) const;

    // Get the original pass declaration
    const RenderPassDecl* GetPass(u32 passId) const;

    // Clear all declarations
    void Clear();

    RenderPassMergeStats GetStats() const;

private:
    bool CheckCompatibility(const RenderPassDecl& a, const RenderPassDecl& b) const;
    bool HasResourceDependency(const RenderPassDecl& writer, const RenderPassDecl& reader) const;
    MergedPass MergePasses(const std::vector<const RenderPassDecl*>& passes) const;

    RenderPassMergeConfig m_config;
    std::vector<RenderPassDecl> m_passes;
    std::unordered_map<u32, u32> m_passIndex; // passId -> index

    mutable u32 m_mergeAttempts = 0;
    mutable u32 m_mergeSuccesses = 0;
    mutable u32 m_mergeRejections = 0;
    mutable u32 m_maxSubpasses = 0;
    mutable u32 m_attachmentsSaved = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
