#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Frame Graph Resource Aliaser ────────────────────────────────────
// Automatically determines which transient resources in a render graph can
// share the same physical memory allocation. Uses lifetime analysis and
// graph-coloring to maximize memory reuse across non-overlapping passes.
//
// Use cases:
//   - Minimize VRAM for transient render targets in a frame graph
//   - Automatically alias GBuffer attachments with post-process temps
//   - Reduce peak memory by 40-60% for complex render graphs
//   - Integrate with transient attachment allocator for actual allocation

enum class AliasResourceType : u8 {
    RenderTarget,
    DepthStencil,
    StorageImage,
    StorageBuffer,
    TransientBuffer,
};

struct FrameGraphResource {
    u32          resourceId;
    AliasResourceType type;
    u64          sizeBytes;        // For buffers: byte count. For images: w*h*bpp
    u32          width;
    u32          height;
    u32          format;           // Opaque format enum
    u32          sampleCount;
    u32          firstPassIndex;   // First pass that uses this resource
    u32          lastPassIndex;    // Last pass that uses this resource
    std::string  debugName;
};

struct AliasGroup {
    u32              groupId;
    u64              physicalSize;       // Size of the physical allocation
    std::vector<u32> resourceIds;        // Resources sharing this allocation
    std::string      debugName;
};

struct AliaserConfig {
    u32  maxResources = 1024;
    u32  maxPasses = 256;
    bool enableAliasing = true;
    bool requireSameType = true;       // Only alias same ResourceType
    bool requireSameFormat = false;    // Stricter: same format required
    bool requireSameSize = false;      // Strictest: same dimensions required
    u64  minResourceSize = 0;          // Don't alias resources smaller than this
};

struct AliaserStats {
    u32 totalResources;
    u32 totalAliasGroups;
    u64 totalLogicalSize;       // Sum of all resource sizes
    u64 totalPhysicalSize;      // Sum of alias group sizes (actual VRAM)
    u64 memorySaved;
    float savingsRatio;          // memorySaved / totalLogicalSize
    u32 maxOverlap;              // Max resources alive simultaneously
};

class FrameGraphResourceAliaser {
public:
    bool Init(const AliaserConfig& config = {});
    void Shutdown();

    // Declare a transient resource with its pass lifetime
    u32 DeclareResource(AliasResourceType type, u64 sizeBytes, u32 firstPass, u32 lastPass,
                         const std::string& name = "");

    // Declare with full image info
    u32 DeclareImageResource(AliasResourceType type, u32 width, u32 height, u32 format,
                              u32 sampleCount, u32 firstPass, u32 lastPass,
                              const std::string& name = "");

    // Check if two resources have overlapping lifetimes
    bool Overlaps(u32 resourceA, u32 resourceB) const;

    // Check if two resources are compatible for aliasing
    bool AreCompatible(u32 resourceA, u32 resourceB) const;

    // Run the aliasing algorithm. Returns alias groups.
    std::vector<AliasGroup> ComputeAliasing();

    // Get which alias group a resource belongs to (after ComputeAliasing)
    u32 GetAliasGroup(u32 resourceId) const;

    // Get resource info
    const FrameGraphResource* GetResource(u32 resourceId) const;

    u32 GetResourceCount() const;

    void Clear();
    void Reset();

    AliaserStats GetStats() const;

private:
    bool CanAlias(const FrameGraphResource& a, const FrameGraphResource& b) const;
    void BuildInterferenceGraph();
    std::vector<AliasGroup> GraphColorAssign();

    AliaserConfig m_config;
    std::vector<FrameGraphResource> m_resources;

    // Interference graph: adjacency list (resources that overlap)
    std::vector<std::vector<u32>> m_interference;

    // Result: resource -> alias group mapping
    std::vector<u32> m_resourceToGroup;
    std::vector<AliasGroup> m_aliasGroups;

    bool m_dirty = true;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
