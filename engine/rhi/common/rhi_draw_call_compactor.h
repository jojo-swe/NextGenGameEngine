#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::rhi {

// ─── GPU Draw Call Compactor ─────────────────────────────────────────────
// Removes zero-instance and zero-vertex draws from indirect argument
// buffers, merges compatible consecutive draws, and sorts for optimal
// GPU throughput. Operates on CPU-side draw argument arrays before upload.
//
// Use cases:
//   - Strip culled draws (instanceCount == 0) after GPU culling readback
//   - Merge adjacent draws sharing the same mesh/material
//   - Sort draws by pipeline state to minimize state changes
//   - Remove degenerate draws (zero vertices/indices)
//   - Track compaction statistics for profiling

struct CompactDrawArgs {
    u32 indexCount;
    u32 instanceCount;
    u32 firstIndex;
    i32 vertexOffset;
    u32 firstInstance;
    u32 materialId;       // For sorting/merging by material
    u32 meshId;           // For sorting/merging by mesh
    u64 pipelineKey;      // For sorting by pipeline state
};

struct CompactorConfig {
    bool removeZeroInstance = true;
    bool removeZeroVertex = true;
    bool mergeSameMesh = true;
    bool sortByPipeline = true;
    bool sortByMaterial = false;
    u32  maxDraws = 65536;
};

struct CompactorStats {
    u32 inputDraws;
    u32 outputDraws;
    u32 removedZeroInstance;
    u32 removedZeroVertex;
    u32 mergedDraws;
    u32 totalInstancesBefore;
    u32 totalInstancesAfter;
    float compactionRatio;    // outputDraws / inputDraws
};

class DrawCallCompactor {
public:
    bool Init(const CompactorConfig& config = {});
    void Shutdown();

    // Add a draw call to the compaction buffer
    bool AddDraw(const CompactDrawArgs& draw);

    // Batch add draws
    bool AddDraws(const std::vector<CompactDrawArgs>& draws);

    // Run compaction: remove zero draws, merge, sort
    void Compact();

    // Get compacted draw list
    const std::vector<CompactDrawArgs>& GetCompactedDraws() const;

    // Get compacted draw count
    u32 GetCompactedCount() const;

    // Get original (pre-compaction) draw count
    u32 GetOriginalCount() const;

    // Check if compaction has been run
    bool IsCompacted() const;

    // Clear all draws
    void Clear();

    void Reset();

    CompactorStats GetStats() const;

private:
    void RemoveZeroDraws();
    void MergeCompatible();
    void SortDraws();

    CompactorConfig m_config;

    std::vector<CompactDrawArgs> m_inputDraws;
    std::vector<CompactDrawArgs> m_compactedDraws;
    bool m_isCompacted = false;

    u32 m_removedZeroInstance = 0;
    u32 m_removedZeroVertex = 0;
    u32 m_mergedCount = 0;
    u32 m_totalInstancesBefore = 0;
    u32 m_totalInstancesAfter = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
