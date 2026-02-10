#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Occlusion Query Batch Manager ───────────────────────────────────
// Manages batched occlusion queries for visibility determination. Pools
// query slots, tracks readback results, and provides per-object visibility
// with configurable latency and conservative fallback.
//
// Use cases:
//   - Hardware occlusion queries for CPU-side culling
//   - Batched query allocation from a query pool
//   - Multi-frame latency handling (query result from N-1 frame)
//   - Conservative visibility: visible until proven occluded
//   - Stats: visible/occluded counts, query pool utilization

enum class QueryState : u8 {
    Free,
    Pending,       // Query issued, result not yet available
    ResultReady,   // GPU result read back
};

struct OcclusionQuerySlot {
    u32         slotId;
    u32         objectId;
    QueryState  state;
    u64         samplesPassed;   // GPU result: number of samples passed
    u32         issuedFrame;
    u32         resultFrame;
    bool        visible;         // Final visibility decision
};

struct OcclusionQueryBatchConfig {
    u32  poolSize = 4096;            // Total query slots
    u32  maxQueriesPerFrame = 1024;
    u32  latencyFrames = 1;          // Frames of delay before result available
    u64  visibilityThreshold = 0;    // Samples needed to be "visible" (0 = any)
    bool conservativeDefault = true; // Default visible when no result yet
};

struct OcclusionQueryBatchStats {
    u32 totalSlots;
    u32 slotsInUse;
    u32 slotsFree;
    u32 queriesIssuedThisFrame;
    u32 resultsReadThisFrame;
    u32 visibleObjects;
    u32 occludedObjects;
    u32 totalQueriesIssued;
    u32 totalResultsRead;
    float occlusionRatio;
};

class OcclusionQueryBatchManager {
public:
    bool Init(const OcclusionQueryBatchConfig& config = {});
    void Shutdown();

    // Allocate a query slot for an object. Returns slot ID.
    u32 AllocateQuery(u32 objectId);

    // Mark a query as issued (GPU command recorded)
    void MarkIssued(u32 slotId, u32 currentFrame);

    // Submit GPU readback result for a slot
    void SubmitResult(u32 slotId, u64 samplesPassed, u32 currentFrame);

    // Check if an object is visible (uses latest available result)
    bool IsVisible(u32 objectId) const;

    // Get the samples-passed count for an object
    u64 GetSamplesPassed(u32 objectId) const;

    // Free a query slot
    void FreeQuery(u32 slotId);

    // Free all queries for a specific object
    void FreeObjectQueries(u32 objectId);

    // Process frame: age queries, handle latency
    void ProcessFrame(u32 currentFrame);

    // Get slot info
    const OcclusionQuerySlot* GetSlot(u32 slotId) const;

    // Find slot by object ID
    u32 FindSlotForObject(u32 objectId) const;

    u32 GetAllocatedCount() const;
    u32 GetFreeCount() const;

    void Reset();

    OcclusionQueryBatchStats GetStats() const;

private:
    OcclusionQueryBatchConfig m_config;
    std::vector<OcclusionQuerySlot> m_slots;
    std::unordered_map<u32, u32> m_objectToSlot; // objectId -> slotId

    u32 m_queriesThisFrame = 0;
    u32 m_resultsThisFrame = 0;
    u32 m_totalIssued = 0;
    u32 m_totalResults = 0;
    u32 m_visibleCount = 0;
    u32 m_occludedCount = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
