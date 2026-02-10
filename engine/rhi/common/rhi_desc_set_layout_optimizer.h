#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Descriptor Set Layout Optimizer ─────────────────────────────────
// Analyzes descriptor set usage patterns across passes and optimizes
// layout assignment to minimize descriptor set rebinds. Groups frequently
// co-bound descriptors and separates per-frame, per-pass, and per-draw
// bindings into distinct sets.
//
// Use cases:
//   - Minimize vkCmdBindDescriptorSets calls
//   - Separate global (set 0), per-pass (set 1), per-material (set 2),
//     per-draw (set 3) bindings by update frequency
//   - Detect unused bindings for layout compaction
//   - Merge compatible layouts to reduce VkDescriptorSetLayout objects

enum class DescriptorType : u8 {
    UniformBuffer,
    StorageBuffer,
    SampledImage,
    StorageImage,
    Sampler,
    CombinedImageSampler,
    UniformTexelBuffer,
    StorageTexelBuffer,
    InputAttachment,
    AccelerationStructure,
};

enum class UpdateFrequency : u8 {
    PerFrame,      // Set 0: globals, camera, time
    PerPass,       // Set 1: pass-specific resources
    PerMaterial,   // Set 2: material textures, parameters
    PerDraw,       // Set 3: per-object transforms, instance data
};

struct DescriptorBinding {
    u32             binding;
    DescriptorType  type;
    u32             count;          // Array size (1 for non-array)
    u32             stageFlags;     // Shader stage visibility bits
    UpdateFrequency frequency;
    std::string     debugName;
};

struct DescriptorSetLayout {
    u32                          layoutId;
    u32                          setIndex;
    std::vector<DescriptorBinding> bindings;
    u64                          layoutHash;
    u32                          refCount;
    std::string                  debugName;
};

struct LayoutOptimizerConfig {
    u32  maxLayouts = 256;
    u32  maxBindingsPerSet = 32;
    bool enableMerging = true;       // Merge compatible layouts
    bool enableCompaction = true;    // Remove unused bindings
    bool sortByFrequency = true;     // Assign set index by update frequency
};

struct LayoutOptimizerStats {
    u32 totalLayouts;
    u32 totalBindings;
    u32 layoutsMerged;
    u32 bindingsRemoved;
    u32 uniqueLayoutHashes;
    u32 rebindsAvoided;             // Estimated rebinds saved by optimization
};

class DescriptorSetLayoutOptimizer {
public:
    bool Init(const LayoutOptimizerConfig& config = {});
    void Shutdown();

    // Declare a binding for a specific set
    void DeclareBinding(u32 setIndex, u32 binding, DescriptorType type, u32 count,
                         u32 stageFlags, UpdateFrequency frequency,
                         const std::string& name = "");

    // Build optimized layouts from declared bindings
    std::vector<DescriptorSetLayout> BuildOptimizedLayouts();

    // Get layout by set index (after building)
    const DescriptorSetLayout* GetLayout(u32 setIndex) const;

    // Check if two layouts are compatible (same bindings)
    bool AreCompatible(u32 layoutA, u32 layoutB) const;

    // Record a bind event (for stats)
    void RecordBind(u32 setIndex);

    // Get binding count for a set
    u32 GetBindingCount(u32 setIndex) const;

    u32 GetLayoutCount() const;

    void Clear();
    void Reset();

    LayoutOptimizerStats GetStats() const;

private:
    u64 ComputeLayoutHash(const std::vector<DescriptorBinding>& bindings) const;
    void SortBindingsByFrequency(std::vector<DescriptorBinding>& bindings) const;
    void CompactBindings(std::vector<DescriptorBinding>& bindings) const;

    LayoutOptimizerConfig m_config;

    // Declared bindings per set index
    std::unordered_map<u32, std::vector<DescriptorBinding>> m_declaredBindings;

    // Built layouts
    std::vector<DescriptorSetLayout> m_layouts;

    u32 m_layoutsMerged = 0;
    u32 m_bindingsRemoved = 0;
    u32 m_totalBinds = 0;
    u32 m_rebindsAvoided = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
