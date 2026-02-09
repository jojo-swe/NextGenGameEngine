#include "engine/rhi/common/rhi_barrier_batch_optimizer.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool BarrierBatchOptimizer::Init(const BarrierOptimizerConfig& config) {
    m_config = config;
    NGE_LOG_INFO("Barrier batch optimizer initialized: redundancy={}, merge={}, split={}, batch={}",
                 config.enableRedundancyElimination, config.enableMerging,
                 config.enableSplitBarriers, config.enableBatching);
    return true;
}

void BarrierBatchOptimizer::Shutdown() {
    m_input.clear();
    m_optimized.clear();
    m_batches.clear();
}

void BarrierBatchOptimizer::Submit(const std::vector<ResourceBarrier>& barriers) {
    std::lock_guard lock(m_mutex);
    m_input.insert(m_input.end(), barriers.begin(), barriers.end());
}

void BarrierBatchOptimizer::Optimize() {
    std::lock_guard lock(m_mutex);

    m_redundantEliminated = 0;
    m_merged = 0;
    m_splitConverted = 0;
    m_crossQueueTransfers = 0;

    m_optimized = m_input;

    if (m_config.enableRedundancyElimination) {
        EliminateRedundant();
    }

    if (m_config.enableMerging) {
        MergeBarriers();
    }

    if (m_config.enableSplitBarriers) {
        ConvertToSplitBarriers();
    }

    // Count cross-queue transfers
    for (const auto& b : m_optimized) {
        if (b.srcQueueFamily != b.dstQueueFamily) {
            m_crossQueueTransfers++;
        }
    }

    if (m_config.enableBatching) {
        BatchByPipelineStage();
    }
}

std::vector<ResourceBarrier> BarrierBatchOptimizer::GetOptimizedBarriers() const {
    std::lock_guard lock(m_mutex);
    return m_optimized;
}

void BarrierBatchOptimizer::Clear() {
    std::lock_guard lock(m_mutex);
    m_input.clear();
    m_optimized.clear();
    m_batches.clear();
    m_redundantEliminated = 0;
    m_merged = 0;
    m_splitConverted = 0;
    m_crossQueueTransfers = 0;
}

BarrierOptimizerStats BarrierBatchOptimizer::GetStats() const {
    std::lock_guard lock(m_mutex);
    BarrierOptimizerStats stats{};
    stats.inputBarriers = static_cast<u32>(m_input.size());
    stats.outputBarriers = static_cast<u32>(m_optimized.size());
    stats.redundantEliminated = m_redundantEliminated;
    stats.merged = m_merged;
    stats.splitConverted = m_splitConverted;
    stats.batchCount = static_cast<u32>(m_batches.size());
    stats.crossQueueTransfers = m_crossQueueTransfers;
    stats.reductionPercent = stats.inputBarriers > 0 ?
        (1.0f - static_cast<f32>(stats.outputBarriers) / static_cast<f32>(stats.inputBarriers)) * 100.0f : 0.0f;
    return stats;
}

void BarrierBatchOptimizer::EliminateRedundant() {
    std::vector<ResourceBarrier> filtered;
    filtered.reserve(m_optimized.size());

    for (const auto& barrier : m_optimized) {
        // Skip if src == dst state (no-op transition)
        if (barrier.srcState == barrier.dstState &&
            barrier.srcQueueFamily == barrier.dstQueueFamily) {
            m_redundantEliminated++;
            continue;
        }

        // Skip if transitioning from Undefined to Undefined
        if (barrier.srcState == ResourceState::Undefined &&
            barrier.dstState == ResourceState::Undefined) {
            m_redundantEliminated++;
            continue;
        }

        filtered.push_back(barrier);
    }

    m_optimized = std::move(filtered);
}

void BarrierBatchOptimizer::MergeBarriers() {
    // Group barriers by resource handle + subresource
    struct MergeKey {
        u64 handle;
        u32 subresource;
        bool operator==(const MergeKey& other) const {
            return handle == other.handle && subresource == other.subresource;
        }
    };

    struct MergeKeyHash {
        size_t operator()(const MergeKey& k) const {
            return std::hash<u64>()(k.handle) ^ (std::hash<u32>()(k.subresource) << 1);
        }
    };

    std::unordered_map<MergeKey, std::vector<size_t>, MergeKeyHash> groups;

    for (size_t i = 0; i < m_optimized.size(); ++i) {
        MergeKey key{m_optimized[i].resourceHandle, m_optimized[i].subresource};
        groups[key].push_back(i);
    }

    std::vector<ResourceBarrier> merged;
    merged.reserve(m_optimized.size());

    for (auto& [key, indices] : groups) {
        if (indices.size() == 1) {
            merged.push_back(m_optimized[indices[0]]);
            continue;
        }

        // Sort by pass index
        std::sort(indices.begin(), indices.end(),
            [this](size_t a, size_t b) {
                return m_optimized[a].passIndex < m_optimized[b].passIndex;
            });

        // Merge chain: first src → last dst
        ResourceBarrier mergedBarrier = m_optimized[indices[0]];
        mergedBarrier.dstState = m_optimized[indices.back()].dstState;
        mergedBarrier.dstQueueFamily = m_optimized[indices.back()].dstQueueFamily;

        merged.push_back(mergedBarrier);
        m_merged += static_cast<u32>(indices.size() - 1);
    }

    m_optimized = std::move(merged);
}

void BarrierBatchOptimizer::ConvertToSplitBarriers() {
    for (auto& barrier : m_optimized) {
        if (barrier.type != BarrierType::Immediate) continue;
        if (barrier.srcQueueFamily != barrier.dstQueueFamily) continue; // Cross-queue can't split

        // Only split if there's enough pass gap
        // In practice, the render graph would provide begin/end pass indices
        // Here we mark candidates based on state transitions that benefit from splitting
        bool isHeavyTransition =
            (HasState(barrier.srcState, ResourceState::RenderTarget) &&
             HasState(barrier.dstState, ResourceState::ShaderRead)) ||
            (HasState(barrier.srcState, ResourceState::ShaderWrite) &&
             HasState(barrier.dstState, ResourceState::ShaderRead)) ||
            (HasState(barrier.srcState, ResourceState::DepthStencilWrite) &&
             HasState(barrier.dstState, ResourceState::DepthStencilRead));

        if (isHeavyTransition) {
            // Convert to split barrier pair
            // The caller is responsible for inserting SplitBegin at the right point
            barrier.type = BarrierType::SplitBegin;
            m_splitConverted++;

            // Add the end barrier
            ResourceBarrier endBarrier = barrier;
            endBarrier.type = BarrierType::SplitEnd;
            m_optimized.push_back(endBarrier);
        }
    }
}

void BarrierBatchOptimizer::BatchByPipelineStage() {
    m_batches.clear();

    // Group by (srcStage, dstStage) pair
    struct StageKey {
        u32 src;
        u32 dst;
        bool operator==(const StageKey& other) const { return src == other.src && dst == other.dst; }
    };
    struct StageKeyHash {
        size_t operator()(const StageKey& k) const {
            return std::hash<u32>()(k.src) ^ (std::hash<u32>()(k.dst) << 16);
        }
    };

    std::unordered_map<StageKey, std::vector<ResourceBarrier>, StageKeyHash> stageGroups;

    for (const auto& barrier : m_optimized) {
        u32 srcStage = StateToPipelineStage(barrier.srcState);
        u32 dstStage = StateToPipelineStage(barrier.dstState);
        StageKey key{srcStage, dstStage};
        stageGroups[key].push_back(barrier);
    }

    for (auto& [key, barriers] : stageGroups) {
        OptimizedBarrierBatch batch;
        batch.pipelineStageSrc = key.src;
        batch.pipelineStageDst = key.dst;
        batch.barriers = std::move(barriers);
        m_batches.push_back(std::move(batch));
    }
}

u32 BarrierBatchOptimizer::StateToPipelineStage(ResourceState state) const {
    // Map resource states to Vulkan pipeline stage flags (approximate)
    // VK_PIPELINE_STAGE_* values
    constexpr u32 TOP_OF_PIPE           = 0x00000001;
    constexpr u32 VERTEX_INPUT          = 0x00000004;
    constexpr u32 VERTEX_SHADER         = 0x00000008;
    constexpr u32 FRAGMENT_SHADER       = 0x00000080;
    constexpr u32 EARLY_FRAGMENT        = 0x00000100;
    constexpr u32 LATE_FRAGMENT         = 0x00000200;
    constexpr u32 COLOR_ATTACHMENT      = 0x00000400;
    constexpr u32 COMPUTE_SHADER        = 0x00000800;
    constexpr u32 TRANSFER              = 0x00001000;
    constexpr u32 DRAW_INDIRECT         = 0x00000002;

    u32 stage = TOP_OF_PIPE;

    if (HasState(state, ResourceState::VertexBuffer) || HasState(state, ResourceState::IndexBuffer))
        stage |= VERTEX_INPUT;
    if (HasState(state, ResourceState::UniformBuffer))
        stage |= VERTEX_SHADER | FRAGMENT_SHADER | COMPUTE_SHADER;
    if (HasState(state, ResourceState::ShaderRead))
        stage |= FRAGMENT_SHADER | COMPUTE_SHADER;
    if (HasState(state, ResourceState::ShaderWrite))
        stage |= COMPUTE_SHADER;
    if (HasState(state, ResourceState::RenderTarget))
        stage |= COLOR_ATTACHMENT;
    if (HasState(state, ResourceState::DepthStencilWrite))
        stage |= EARLY_FRAGMENT | LATE_FRAGMENT;
    if (HasState(state, ResourceState::DepthStencilRead))
        stage |= EARLY_FRAGMENT;
    if (HasState(state, ResourceState::CopySrc) || HasState(state, ResourceState::CopyDst))
        stage |= TRANSFER;
    if (HasState(state, ResourceState::IndirectArgument))
        stage |= DRAW_INDIRECT;

    return stage;
}

} // namespace nge::rhi
