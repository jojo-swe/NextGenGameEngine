#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace nge::rhi {

// ─── GPU Render Pass Merge Optimizer ─────────────────────────────────────
// Analyzes render pass sequences and merges compatible passes into
// subpasses to reduce load/store operations and improve tile-based GPU
// performance. Validates attachment compatibility and dependency ordering.
//
// Use cases:
//   - Merge GBuffer + lighting into single render pass with subpasses
//   - Reduce attachment load/store on tile-based GPUs (mobile, Apple)
//   - Validate subpass dependencies (input attachments)
//   - Detect mergeable pass sequences automatically
//   - Stats: passes merged, load/store ops saved

enum class PassAttachmentOp : u8 {
    Load,
    Clear,
    DontCare,
};

enum class PassStoreOp : u8 {
    Store,
    DontCare,
};

struct MergePassAttachment {
    u32              attachmentId;
    u32              format;          // Opaque format identifier
    PassAttachmentOp loadOp;
    PassStoreOp      storeOp;
    bool             isInput;         // Used as input attachment in this pass
    bool             isOutput;        // Written to in this pass
};

struct MergeRenderPassDesc {
    u32                        passId;
    std::string                name;
    std::vector<MergePassAttachment> attachments;
    std::vector<u32>           dependsOn;    // Pass IDs this pass depends on
    bool                       usesDepth;
    u32                        width;
    u32                        height;
    u32                        sampleCount;
};

struct MergedPassGroup {
    std::vector<u32> passIds;        // Original pass IDs in execution order
    std::string      mergedName;
    u32              width;
    u32              height;
    u32              sampleCount;
    u32              loadOpsSaved;
    u32              storeOpsSaved;
};

struct RenderPassMergeConfig {
    u32  maxPasses = 256;
    u32  maxAttachmentsPerPass = 8;
    bool enableMerging = true;
    bool requireSameResolution = true;
    bool requireSameSampleCount = true;
};

struct RenderPassMergeStats {
    u32 totalPasses;
    u32 mergedGroups;
    u32 passesMerged;
    u32 passesUnmerged;
    u32 loadOpsSaved;
    u32 storeOpsSaved;
    float mergeRatio;
};

class RenderPassMergeOptimizer {
public:
    bool Init(const RenderPassMergeConfig& config = {});
    void Shutdown();

    // Register a render pass
    bool AddPass(const MergeRenderPassDesc& pass);

    // Run merge optimization
    void Optimize();

    // Get merged pass groups
    const std::vector<MergedPassGroup>& GetMergedGroups() const;

    // Get number of merged groups
    u32 GetMergedGroupCount() const;

    // Check if two passes are compatible for merging
    bool AreCompatible(u32 passIdA, u32 passIdB) const;

    // Check if a pass was merged
    bool IsMerged(u32 passId) const;

    // Get the group a pass belongs to
    u32 GetGroupForPass(u32 passId) const;

    // Get original pass info
    const MergeRenderPassDesc* GetPass(u32 passId) const;

    u32 GetPassCount() const;

    void Clear();
    void Reset();

    RenderPassMergeStats GetStats() const;

private:
    bool CanMerge(const MergeRenderPassDesc& a, const MergeRenderPassDesc& b) const;
    bool HasDependency(u32 fromPass, u32 toPass) const;
    u32 CountLoadOpsSaved(const MergedPassGroup& group) const;
    u32 CountStoreOpsSaved(const MergedPassGroup& group) const;

    RenderPassMergeConfig m_config;
    std::unordered_map<u32, MergeRenderPassDesc> m_passes;
    std::vector<u32> m_passOrder; // Insertion order
    std::vector<MergedPassGroup> m_mergedGroups;
    std::unordered_map<u32, u32> m_passToGroup; // passId -> group index

    bool m_optimized = false;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
