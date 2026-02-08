#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace nge::renderer {

// ─── Frame Graph Compiler ────────────────────────────────────────────────
// Takes a declared frame graph (passes + resource dependencies) and
// produces an optimized execution plan:
//   1. Dead-code elimination (remove passes whose outputs are unused)
//   2. Topological sort (dependency-respecting ordering)
//   3. Resource lifetime analysis (first/last use per resource)
//   4. Aliasing opportunity detection (non-overlapping lifetimes)
//   5. Barrier placement (minimal transitions between passes)
//   6. Async compute extraction (identify passes safe for async queue)

enum class PassType : u8 {
    Graphics,
    Compute,
    Transfer,
    AsyncCompute,
};

enum class ResourceUsage : u8 {
    ColorAttachmentWrite,
    DepthAttachmentWrite,
    ShaderRead,
    ShaderWrite,
    TransferSrc,
    TransferDst,
    Present,
};

struct GraphResource {
    std::string name;
    u32         id;
    bool        imported = false;   // External resource (swapchain, etc.)
    bool        transient = true;   // Can be aliased
    u64         sizeBytes = 0;
    rhi::Format format = rhi::Format::RGBA8_UNORM;
};

struct GraphPass {
    std::string                      name;
    u32                              id;
    PassType                         type = PassType::Graphics;
    std::vector<std::pair<u32, ResourceUsage>> reads;   // (resourceId, usage)
    std::vector<std::pair<u32, ResourceUsage>> writes;  // (resourceId, usage)
    bool                             hasSideEffects = false; // Prevents dead-code elimination
    bool                             canRunAsync = false;
};

struct CompiledBarrier {
    u32             resourceId;
    ResourceUsage   beforeUsage;
    ResourceUsage   afterUsage;
    u32             beforePass;
    u32             afterPass;
};

struct AliasingGroup {
    std::vector<u32> resourceIds;
    u64              peakSize;
};

struct CompiledPass {
    u32                          passId;
    u32                          executionOrder;
    PassType                     queueType;
    std::vector<CompiledBarrier> barriers;
    std::vector<u32>             syncBefore; // Wait on these passes
};

struct CompiledGraph {
    std::vector<CompiledPass>   passes;
    std::vector<AliasingGroup>  aliasingGroups;
    std::vector<u32>            eliminatedPasses;
    u32                         asyncPassCount = 0;
    u64                         peakMemory = 0;
    u64                         aliasedMemory = 0;
};

class FrameGraphCompiler {
public:
    // Build phase: declare passes and resources
    u32 AddResource(const std::string& name, u64 sizeBytes, rhi::Format format = rhi::Format::RGBA8_UNORM,
                    bool imported = false, bool transient = true);

    u32 AddPass(const std::string& name, PassType type = PassType::Graphics, bool hasSideEffects = false);

    void PassReads(u32 passId, u32 resourceId, ResourceUsage usage);
    void PassWrites(u32 passId, u32 resourceId, ResourceUsage usage);

    void MarkAsyncCapable(u32 passId);

    // Compile phase
    CompiledGraph Compile();

    // Reset for next frame
    void Reset();

    // Query
    const std::vector<GraphPass>& GetPasses() const { return m_passes; }
    const std::vector<GraphResource>& GetResources() const { return m_resources; }

private:
    void EliminateDeadPasses(std::vector<bool>& alive);
    std::vector<u32> TopologicalSort(const std::vector<bool>& alive);
    void AnalyzeLifetimes(const std::vector<u32>& order,
                          std::unordered_map<u32, u32>& firstUse,
                          std::unordered_map<u32, u32>& lastUse);
    std::vector<AliasingGroup> FindAliasingOpportunities(
        const std::unordered_map<u32, u32>& firstUse,
        const std::unordered_map<u32, u32>& lastUse);
    std::vector<CompiledBarrier> PlaceBarriers(const std::vector<u32>& order);

    std::vector<GraphPass> m_passes;
    std::vector<GraphResource> m_resources;
};

} // namespace nge::renderer
