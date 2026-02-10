#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Acceleration Structure Compaction Manager ───────────────────────
// Manages BLAS/TLAS compaction for VK_KHR_acceleration_structure. After
// initial build, acceleration structures often have unused memory that
// can be reclaimed through compaction, reducing VRAM usage by 30-60%.
//
// Use cases:
//   - Compact BLAS after initial geometry build
//   - Compact TLAS after scene updates
//   - Track compaction savings for memory budgeting
//   - Batch compaction queries (vkCmdWriteAccelerationStructuresPropertiesKHR)
//   - Deferred compaction (spread across frames)

enum class AccelStructType : u8 {
    BLAS,   // Bottom-Level Acceleration Structure
    TLAS,   // Top-Level Acceleration Structure
};

enum class CompactionState : u8 {
    Unbuilt,
    Built,               // Initial build complete, not yet queried
    QueryPending,        // Compacted size query submitted
    QueryReady,          // Compacted size available
    Compacting,          // Compaction copy in progress
    Compacted,           // Compaction complete
    Failed,
};

struct AccelStructInfo {
    u64               handle;          // VkAccelerationStructureKHR
    u64               bufferHandle;    // Backing VkBuffer
    AccelStructType   type;
    CompactionState   state;
    u64               originalSize;    // Pre-compaction size
    u64               compactedSize;   // Post-compaction size (0 if unknown)
    u32               geometryCount;   // Number of geometries (BLAS) or instances (TLAS)
    u32               buildFrame;      // Frame when built
    u32               compactFrame;    // Frame when compacted
    std::string       debugName;
};

struct CompactionRequest {
    u64 accelStructId;
    u32 priority;        // Higher = compact sooner
};

struct AccelCompactionConfig {
    u32  maxStructures = 4096;
    u32  maxCompactionsPerFrame = 8;    // Limit GPU work per frame
    u64  minSavingsThreshold = 4096;    // Don't compact if savings < this
    float minSavingsRatio = 0.1f;       // Don't compact if savings < 10%
    bool autoCompact = true;            // Auto-queue compaction after build
    bool deferCompaction = true;        // Spread across multiple frames
};

struct AccelCompactionStats {
    u32 totalStructures;
    u32 totalBuilt;
    u32 totalCompacted;
    u32 totalFailed;
    u32 pendingCompactions;
    u64 totalOriginalSize;
    u64 totalCompactedSize;
    u64 totalMemorySaved;
    float averageSavingsRatio;
};

class AccelStructCompactionManager {
public:
    bool Init(const AccelCompactionConfig& config = {});
    void Shutdown();

    // Register a newly built acceleration structure
    u64 RegisterBuilt(AccelStructType type, u64 handle, u64 bufferHandle,
                       u64 originalSize, u32 geometryCount, const std::string& name = "");

    // Mark that a compaction size query has been submitted
    void MarkQueryPending(u64 accelId);

    // Provide the compacted size result from the query
    void SetCompactedSize(u64 accelId, u64 compactedSize);

    // Check if compaction is worthwhile for a structure
    bool ShouldCompact(u64 accelId) const;

    // Mark compaction as in progress
    void MarkCompacting(u64 accelId);

    // Mark compaction as complete with new handle/buffer
    void MarkCompacted(u64 accelId, u64 newHandle, u64 newBuffer, u32 currentFrame);

    // Mark compaction as failed
    void MarkFailed(u64 accelId);

    // Get structures ready for compaction query
    std::vector<u64> GetBuiltStructures() const;

    // Get structures ready to compact (query result available, worth compacting)
    std::vector<u64> GetReadyToCompact(u32 maxCount = UINT32_MAX) const;

    // Get info about a structure
    const AccelStructInfo* GetInfo(u64 accelId) const;

    // Unregister a structure (destroyed)
    void Unregister(u64 accelId);

    // Process a frame: returns IDs to compact this frame
    std::vector<u64> ProcessFrame(u32 currentFrame);

    u32 GetCount() const;

    void Reset();

    AccelCompactionStats GetStats() const;

private:
    AccelCompactionConfig m_config;
    std::unordered_map<u64, AccelStructInfo> m_structures;

    u64 m_nextId = 1;
    u32 m_totalBuilt = 0;
    u32 m_totalCompacted = 0;
    u32 m_totalFailed = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
