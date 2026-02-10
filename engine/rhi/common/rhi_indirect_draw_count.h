#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::rhi {

// ─── GPU Indirect Draw Count Manager ─────────────────────────────────────
// Wraps VK_KHR_draw_indirect_count for GPU-driven rendering pipelines.
// Manages count buffers, argument buffers, and draw call submission for
// multi-draw indirect with GPU-determined draw counts.
//
// Use cases:
//   - GPU culling output -> indirect draw with variable count
//   - Meshlet rendering (GPU determines visible meshlet count)
//   - LOD selection with GPU-side instance culling
//   - Cascaded shadow map per-cascade draw count

enum class IndirectDrawType : u8 {
    Draw,           // vkCmdDrawIndirectCount
    DrawIndexed,    // vkCmdDrawIndexedIndirectCount
};

struct IndirectBatch {
    u32              batchId;
    IndirectDrawType type;
    u64              argBufferHandle;    // VkBuffer with draw args
    u64              argBufferOffset;
    u32              argStride;          // Bytes per draw arg
    u64              countBufferHandle;  // VkBuffer with draw count (u32)
    u64              countBufferOffset;
    u32              maxDrawCount;       // Upper bound for safety
    std::string      debugName;
};

struct IndirectDrawCountConfig {
    u32  maxBatches = 256;
    u32  defaultMaxDrawCount = 65536;
    bool validateMaxCount = true;       // Clamp to maxDrawCount
    bool trackStats = true;
};

struct IndirectDrawCountStats {
    u32 totalBatches;
    u32 totalSubmissions;
    u64 totalDrawCalls;         // Sum of actual draw counts
    u64 totalMaxDrawCalls;      // Sum of maxDrawCount (potential waste)
    u32 peakBatches;
    float avgUtilization;       // actual / max ratio
};

class IndirectDrawCountManager {
public:
    bool Init(const IndirectDrawCountConfig& config = {});
    void Shutdown();

    // Register an indirect draw batch
    u32 RegisterBatch(IndirectDrawType type, u64 argBuffer, u64 argOffset, u32 argStride,
                       u64 countBuffer, u64 countOffset, u32 maxDrawCount,
                       const std::string& name = "");

    // Update batch parameters (e.g., new buffer after resize)
    void UpdateBatch(u32 batchId, u64 argBuffer, u64 argOffset, u64 countBuffer, u64 countOffset);

    // Update max draw count (e.g., after instance buffer resize)
    void SetMaxDrawCount(u32 batchId, u32 maxDrawCount);

    // Record a submission (for stats tracking)
    void RecordSubmission(u32 batchId, u32 actualDrawCount);

    // Get batch info
    const IndirectBatch* GetBatch(u32 batchId) const;

    // Get all active batch IDs
    std::vector<u32> GetActiveBatches() const;

    // Remove a batch
    void RemoveBatch(u32 batchId);

    u32 GetBatchCount() const;

    void ClearAll();
    void Reset();

    IndirectDrawCountStats GetStats() const;

private:
    IndirectDrawCountConfig m_config;
    std::vector<IndirectBatch> m_batches;
    std::vector<bool> m_active;

    u32 m_totalSubmissions = 0;
    u64 m_totalDrawCalls = 0;
    u64 m_totalMaxDrawCalls = 0;
    u32 m_peakBatches = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
