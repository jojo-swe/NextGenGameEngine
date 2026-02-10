#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Resource Transition Batch Optimizer ─────────────────────────────
// Coalesces redundant barriers into split/merged transitions to minimize
// pipeline stalls. Works as a post-processing pass on the render graph's
// barrier list before submission.
//
// Optimizations:
//   1. Redundancy elimination: skip barriers where src == dst state
//   2. Merge: combine multiple barriers on the same resource into one
//   3. Split barriers: convert immediate barriers to split (begin/end)
//      for overlapping work between the two halves
//   4. Batch: group barriers by pipeline stage for fewer API calls
//   5. Cross-queue: detect and flag queue ownership transfers

// ResourceState is defined in rhi_types.h (included via rhi_device.h)

enum class BarrierType : u8 {
    Immediate,    // Full pipeline barrier
    SplitBegin,   // Begin of split barrier
    SplitEnd,     // End of split barrier
};

struct ResourceBarrier {
    u64            resourceHandle;
    ResourceState  srcState;
    ResourceState  dstState;
    BarrierType    type = BarrierType::Immediate;
    u32            srcQueueFamily = 0;
    u32            dstQueueFamily = 0;
    u32            subresource = UINT32_MAX; // UINT32_MAX = all subresources
    u32            passIndex = 0;
    std::string    debugName;
};

struct OptimizedBarrierBatch {
    u32                        pipelineStageSrc; // VkPipelineStageFlags
    u32                        pipelineStageDst;
    std::vector<ResourceBarrier> barriers;
};

struct BarrierOptimizerConfig {
    bool enableRedundancyElimination = true;
    bool enableMerging = true;
    bool enableSplitBarriers = true;
    bool enableBatching = true;
    u32  minPassGapForSplit = 2; // Min passes between begin/end for split
};

struct BarrierOptimizerStats {
    u32 inputBarriers;
    u32 outputBarriers;
    u32 redundantEliminated;
    u32 merged;
    u32 splitConverted;
    u32 batchCount;
    u32 crossQueueTransfers;
    f32 reductionPercent;
};

class BarrierBatchOptimizer {
public:
    bool Init(const BarrierOptimizerConfig& config = {});
    void Shutdown();

    // Submit barriers for optimization
    void Submit(const std::vector<ResourceBarrier>& barriers);

    // Run optimization passes and produce batched output
    void Optimize();

    // Get optimized barrier batches (call after Optimize)
    const std::vector<OptimizedBarrierBatch>& GetBatches() const { return m_batches; }

    // Get flat list of optimized barriers
    std::vector<ResourceBarrier> GetOptimizedBarriers() const;

    // Clear all state for next frame
    void Clear();

    BarrierOptimizerStats GetStats() const;

private:
    void EliminateRedundant();
    void MergeBarriers();
    void ConvertToSplitBarriers();
    void BatchByPipelineStage();

    u32 StateToPipelineStage(ResourceState state) const;

    BarrierOptimizerConfig m_config;
    std::vector<ResourceBarrier> m_input;
    std::vector<ResourceBarrier> m_optimized;
    std::vector<OptimizedBarrierBatch> m_batches;

    u32 m_redundantEliminated = 0;
    u32 m_merged = 0;
    u32 m_splitConverted = 0;
    u32 m_crossQueueTransfers = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
