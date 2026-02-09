#include "engine/rhi/common/rhi_draw_call_merger.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool DrawCallMerger::Init(const DrawCallMergerConfig& config) {
    m_config = config;
    m_requests.reserve(config.maxDrawsPerBatch * config.maxBatches);

    NGE_LOG_INFO("Draw call merger initialized: maxPerBatch={}, maxBatches={}, sort={}, merge={}",
                 config.maxDrawsPerBatch, config.maxBatches, config.sortByDepth, config.enableMerging);
    return true;
}

void DrawCallMerger::Shutdown() {
    m_requests.clear();
    m_batches.clear();
}

void DrawCallMerger::Submit(const DrawRequest& request) {
    std::lock_guard lock(m_mutex);
    m_requests.push_back(request);
}

void DrawCallMerger::SubmitBatch(const DrawRequest* requests, u32 count) {
    std::lock_guard lock(m_mutex);
    m_requests.insert(m_requests.end(), requests, requests + count);
}

void DrawCallMerger::Merge() {
    std::lock_guard lock(m_mutex);
    m_batches.clear();

    if (m_requests.empty()) return;

    if (!m_config.enableMerging) {
        // No merging: one batch per draw
        for (const auto& req : m_requests) {
            MergedBatch batch;
            batch.psoHash = req.psoHash;
            batch.materialId = req.materialId;
            batch.vertexBufferHandle = req.vertexBufferHandle;
            batch.indexBufferHandle = req.indexBufferHandle;
            batch.drawCount = 1;

            MergedBatch::IndirectCommand cmd;
            cmd.indexCount = req.indexCount;
            cmd.instanceCount = req.instanceCount;
            cmd.firstIndex = req.firstIndex;
            cmd.vertexOffset = req.vertexOffset;
            cmd.firstInstance = req.firstInstance;
            batch.commands.push_back(cmd);

            m_batches.push_back(std::move(batch));
        }
        return;
    }

    // Optional: sort by sort key within compatible groups
    if (m_config.sortByDepth) {
        std::stable_sort(m_requests.begin(), m_requests.end(),
            [](const DrawRequest& a, const DrawRequest& b) {
                return a.sortKey < b.sortKey;
            });
    }

    // Group by batch key
    std::unordered_map<BatchKey, std::vector<size_t>, BatchKeyHash> groups;

    for (size_t i = 0; i < m_requests.size(); ++i) {
        BatchKey key;
        key.psoHash = m_requests[i].psoHash;
        key.materialId = m_requests[i].materialId;
        key.vertexBuffer = m_requests[i].vertexBufferHandle;
        key.indexBuffer = m_requests[i].indexBufferHandle;
        groups[key].push_back(i);
    }

    // Build merged batches
    for (auto& [key, indices] : groups) {
        // Split into sub-batches if exceeding max draws per batch
        size_t offset = 0;
        while (offset < indices.size() && m_batches.size() < m_config.maxBatches) {
            MergedBatch batch;
            batch.psoHash = key.psoHash;
            batch.materialId = key.materialId;
            batch.vertexBufferHandle = key.vertexBuffer;
            batch.indexBufferHandle = key.indexBuffer;

            u32 batchSize = std::min(static_cast<u32>(indices.size() - offset),
                                      m_config.maxDrawsPerBatch);

            for (u32 i = 0; i < batchSize; ++i) {
                const auto& req = m_requests[indices[offset + i]];
                MergedBatch::IndirectCommand cmd;
                cmd.indexCount = req.indexCount;
                cmd.instanceCount = req.instanceCount;
                cmd.firstIndex = req.firstIndex;
                cmd.vertexOffset = req.vertexOffset;
                cmd.firstInstance = req.firstInstance;
                batch.commands.push_back(cmd);
            }

            batch.drawCount = static_cast<u32>(batch.commands.size());
            m_batches.push_back(std::move(batch));
            offset += batchSize;
        }
    }
}

std::vector<MergedBatch::IndirectCommand> DrawCallMerger::GetIndirectBuffer(u32 batchIndex) const {
    std::lock_guard lock(m_mutex);
    if (batchIndex >= m_batches.size()) return {};
    return m_batches[batchIndex].commands;
}

void DrawCallMerger::Clear() {
    std::lock_guard lock(m_mutex);
    m_requests.clear();
    m_batches.clear();
}

DrawCallMergerStats DrawCallMerger::GetStats() const {
    std::lock_guard lock(m_mutex);
    DrawCallMergerStats stats{};
    stats.inputDrawCalls = static_cast<u32>(m_requests.size());
    stats.outputBatches = static_cast<u32>(m_batches.size());

    u32 totalCommands = 0;
    u32 largest = 0;
    for (const auto& batch : m_batches) {
        totalCommands += batch.drawCount;
        largest = std::max(largest, batch.drawCount);
    }

    stats.outputDrawCommands = totalCommands;
    stats.mergedDrawCalls = stats.inputDrawCalls > stats.outputBatches ?
        stats.inputDrawCalls - stats.outputBatches : 0;
    stats.reductionPercent = stats.inputDrawCalls > 0 ?
        (1.0f - static_cast<f32>(stats.outputBatches) / static_cast<f32>(stats.inputDrawCalls)) * 100.0f : 0.0f;
    stats.largestBatchSize = largest;

    return stats;
}

} // namespace nge::rhi
