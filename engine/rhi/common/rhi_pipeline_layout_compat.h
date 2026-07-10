#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Pipeline Layout Compatibility Checker ───────────────────────────
// Validates pipeline layout compatibility for descriptor set binding and
// push constant usage. Ensures that pipelines sharing descriptor sets are
// layout-compatible per Vulkan spec, and validates push constant ranges.
//
// Use cases:
//   - Validate descriptor set compatibility between pipelines
//   - Check push constant range overlap and stage conflicts
//   - Detect mismatched set layouts when binding across pipelines
//   - Pre-validate pipeline layout before VkPipelineLayout creation

struct CompatPushConstantRange {
    u32         stageFlags;
    u32         offset;
    u32         size;
    std::string debugName;
};

struct CompatPipelineLayoutDesc {
    u32                          layoutId;
    std::vector<u64>             setLayoutHashes;  // Hash per descriptor set
    std::vector<CompatPushConstantRange> pushConstantRanges;
    std::string                  debugName;
};

enum class CompatIssueType : u8 {
    SetLayoutMismatch,           // Different set layout at same index
    PushConstantOverlap,         // Overlapping push constant ranges
    PushConstantStageConflict,   // Same stage in multiple ranges
    PushConstantExceedsLimit,    // Total push constant size too large
    TooManyDescriptorSets,       // Exceeds max bound sets
    MissingSetLayout,            // Set index gap (sparse sets)
};

struct CompatIssue {
    CompatIssueType type;
    u32             layoutA;
    u32             layoutB;
    u32             setIndex;       // For set-related issues
    std::string     description;
};

struct PipelineLayoutCompatConfig {
    u32  maxLayouts = 512;
    u32  maxDescriptorSets = 4;        // Vulkan typical max
    u32  maxPushConstantSize = 128;    // Bytes (Vulkan minimum guarantee)
    bool warnOnSparseSet = true;
    bool strictMode = false;           // Treat warnings as errors
};

struct PipelineLayoutCompatStats {
    u32 totalLayouts;
    u32 totalValidations;
    u32 compatiblePairs;
    u32 incompatiblePairs;
    u32 pushConstantIssues;
    u32 setLayoutIssues;
};

class PipelineLayoutCompatChecker {
public:
    bool Init(const PipelineLayoutCompatConfig& config = {});
    void Shutdown();

    // Register a pipeline layout
    u32 RegisterLayout(const std::vector<u64>& setLayoutHashes,
                        const std::vector<CompatPushConstantRange>& pushConstants,
                        const std::string& name = "");

    // Check if two layouts are compatible (all shared sets match)
    bool AreCompatible(u32 layoutA, u32 layoutB) const;

    // Check compatibility up to a specific set index
    bool AreCompatibleUpToSet(u32 layoutA, u32 layoutB, u32 maxSet) const;

    // Validate a single layout for internal consistency
    std::vector<CompatIssue> ValidateLayout(u32 layoutId) const;

    // Find all compatibility issues between two layouts
    std::vector<CompatIssue> FindIssues(u32 layoutA, u32 layoutB) const;

    // Get layout info
    const CompatPipelineLayoutDesc* GetLayout(u32 layoutId) const;

    // Get total push constant size for a layout
    u32 GetPushConstantSize(u32 layoutId) const;

    // Unregister
    void Unregister(u32 layoutId);

    u32 GetLayoutCount() const;

    void Reset();

    PipelineLayoutCompatStats GetStats() const;

private:
    bool CheckPushConstantOverlap(const CompatPushConstantRange& a, const CompatPushConstantRange& b) const;

    PipelineLayoutCompatConfig m_config;
    std::unordered_map<u32, CompatPipelineLayoutDesc> m_layouts;

    u32 m_nextId = 0;
    mutable u32 m_totalValidations = 0;
    mutable u32 m_compatiblePairs = 0;
    mutable u32 m_incompatiblePairs = 0;
    mutable u32 m_pushConstantIssues = 0;
    mutable u32 m_setLayoutIssues = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
