#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::rhi {

// ─── GPU Occlusion Query Compactor ───────────────────────────────────────
// Reads occlusion query results and compacts the visible instance list,
// removing instances that were fully occluded. Output is a tightly packed
// buffer of visible instance IDs + an indirect draw argument buffer.
//
// Pipeline:
//   1. Previous frame: GPU runs occlusion queries per instance/batch
//   2. This frame: readback results (or GPU-side predication)
//   3. Compact: stream compact visible IDs into output buffer
//   4. Build indirect args from compacted count
//
// Used by:
//   - Two-pass occlusion culling system
//   - GPU-driven rendering pipeline
//   - Meshlet visibility culling

struct OcclusionInstance {
    u32  instanceId;
    u64  queryResult;   // 0 = occluded, >0 = visible sample count
    bool visible;
};

struct CompactedResult {
    u32                    visibleCount;
    u32                    occludedCount;
    std::vector<u32>       visibleInstanceIds;
    // Indirect draw arguments (VkDrawIndexedIndirectCommand compatible)
    u32                    indirectIndexCount;
    u32                    indirectInstanceCount;
    u32                    indirectFirstIndex;
    i32                    indirectVertexOffset;
    u32                    indirectFirstInstance;
};

struct OcclusionCompactorConfig {
    u32  maxInstances = 65536;
    u32  occlusionThreshold = 0;  // Min visible samples to be considered visible
    bool conservativeMode = true; // If query not ready, assume visible
};

struct OcclusionCompactorStats {
    u32 totalInstances;
    u32 visibleInstances;
    u32 occludedInstances;
    f32 occlusionRate;
    u32 queryNotReadyCount;
};

class OcclusionCompactor {
public:
    bool Init(IDevice* device, const OcclusionCompactorConfig& config = {});
    void Shutdown();

    // Submit instance occlusion results for compaction
    void SubmitResults(const OcclusionInstance* instances, u32 count);

    // Run CPU-side compaction (for readback path)
    CompactedResult Compact();

    // Build indirect draw arguments from compacted result
    void BuildIndirectArgs(CompactedResult& result, u32 indexCountPerInstance,
                            u32 firstIndex = 0, i32 vertexOffset = 0);

    // Get the visible instance ID buffer (for upload to GPU)
    const std::vector<u32>& GetVisibleIds() const;

    // Reset for next frame
    void Reset();

    OcclusionCompactorStats GetStats() const;

private:
    IDevice* m_device = nullptr;
    OcclusionCompactorConfig m_config;

    std::vector<OcclusionInstance> m_instances;
    std::vector<u32> m_visibleIds;
    u32 m_queryNotReady = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
