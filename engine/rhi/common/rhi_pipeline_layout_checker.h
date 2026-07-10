#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Pipeline Layout Compatibility Checker ───────────────────────────
// Validates that pipeline layouts are compatible when binding descriptor
// sets or push constants across different pipelines within a render pass.
//
// Use cases:
//   - Detect descriptor set layout mismatches when switching pipelines
//   - Detect push constant range overlaps or gaps
//   - Validate set compatibility for partial rebind optimization
//   - CI regression testing for layout changes

enum class DescBindingType : u8 {
    Sampler,
    CombinedImageSampler,
    SampledImage,
    StorageImage,
    UniformBuffer,
    StorageBuffer,
    UniformBufferDynamic,
    StorageBufferDynamic,
    InputAttachment,
    AccelerationStructure,
};

enum class ShaderStage : u32 {
    Vertex   = 0x01,
    Fragment = 0x02,
    Compute  = 0x04,
    Geometry = 0x08,
    TessCtrl = 0x10,
    TessEval = 0x20,
    All      = 0xFF,
};

inline ShaderStage operator|(ShaderStage a, ShaderStage b) {
    return static_cast<ShaderStage>(static_cast<u32>(a) | static_cast<u32>(b));
}

struct LayoutBinding {
    u32             binding;
    DescBindingType type;
    u32             count;         // Array size (1 for non-array)
    ShaderStage     stageFlags;
};

struct PushConstantRange {
    ShaderStage stageFlags;
    u32         offset;
    u32         size;
};

struct PipelineSetLayoutDesc {
    u32                       setIndex;
    std::vector<LayoutBinding> bindings;
};

struct PipelineLayoutDesc {
    u64                             layoutId;
    std::string                     debugName;
    std::vector<PipelineSetLayoutDesc> setLayouts;  // Indexed by set number
    std::vector<PushConstantRange>   pushConstants;
};

enum class IncompatibilityType : u8 {
    SetLayoutMismatch,          // Different binding types/counts in same set
    PushConstantOverlap,        // Overlapping ranges with different stages
    PushConstantGap,            // Gap in push constant ranges
    MissingSet,                 // One layout has a set the other doesn't
    BindingCountMismatch,       // Different number of bindings in set
    BindingTypeMismatch,        // Same binding index, different type
    BindingStageMismatch,       // Same binding, different stage flags
    BindingArraySizeMismatch,   // Same binding, different array count
};

struct LayoutIncompatibility {
    IncompatibilityType type;
    u32                 setIndex;      // Which set (if applicable)
    u32                 bindingIndex;  // Which binding (if applicable)
    std::string         description;
};

struct PipelineLayoutCheckerConfig {
    u32  maxLayouts = 256;
    bool checkPushConstantGaps = true;
    bool checkPartialSetCompat = true;   // Check sets 0..N-1 compatibility
};

struct PipelineLayoutCheckerStats {
    u32 totalLayouts;
    u32 totalChecks;
    u32 compatiblePairs;
    u32 incompatiblePairs;
    u32 totalIncompatibilities;
};

class PipelineLayoutChecker {
public:
    bool Init(const PipelineLayoutCheckerConfig& config = {});
    void Shutdown();

    // Register a pipeline layout
    void RegisterLayout(const PipelineLayoutDesc& layout);

    // Remove a layout
    void RemoveLayout(u64 layoutId);

    // Check full compatibility between two layouts
    std::vector<LayoutIncompatibility> CheckCompatibility(u64 layoutA, u64 layoutB) const;

    // Check if sets 0..setIndex are compatible (for partial rebind)
    bool AreSetsPrefixCompatible(u64 layoutA, u64 layoutB, u32 upToSet) const;

    // Check push constant compatibility
    std::vector<LayoutIncompatibility> CheckPushConstantCompat(u64 layoutA, u64 layoutB) const;

    // Get a layout description
    const PipelineLayoutDesc* GetLayout(u64 layoutId) const;

    // Clear all
    void Reset();

    PipelineLayoutCheckerStats GetStats() const;

private:
    std::vector<LayoutIncompatibility> CheckSetCompat(const PipelineSetLayoutDesc& a,
                                                        const PipelineSetLayoutDesc& b) const;

    PipelineLayoutCheckerConfig m_config;
    std::unordered_map<u64, PipelineLayoutDesc> m_layouts;

    mutable u32 m_totalChecks = 0;
    mutable u32 m_compatiblePairs = 0;
    mutable u32 m_incompatiblePairs = 0;
    mutable u32 m_totalIncompats = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
