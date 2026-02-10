#include "engine/rhi/common/rhi_indirect_draw_count.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool IndirectDrawCountManager::Init(const IndirectDrawCountConfig& config) {
    m_config = config;
    m_batches.reserve(config.maxBatches);
    m_active.reserve(config.maxBatches);
    m_totalSubmissions = 0;
    m_totalDrawCalls = 0;
    m_totalMaxDrawCalls = 0;
    m_peakBatches = 0;

    NGE_LOG_INFO("Indirect draw count manager initialized: maxBatches={}, defaultMax={}, validate={}",
                 config.maxBatches, config.defaultMaxDrawCount, config.validateMaxCount);
    return true;
}

void IndirectDrawCountManager::Shutdown() {
    m_batches.clear();
    m_active.clear();
}

u32 IndirectDrawCountManager::RegisterBatch(IndirectDrawType type, u64 argBuffer, u64 argOffset,
                                              u32 argStride, u64 countBuffer, u64 countOffset,
                                              u32 maxDrawCount, const std::string& name) {
    std::lock_guard lock(m_mutex);

    // Find a free slot or add new
    u32 batchId = UINT32_MAX;
    for (u32 i = 0; i < static_cast<u32>(m_active.size()); ++i) {
        if (!m_active[i]) {
            batchId = i;
            break;
        }
    }

    if (batchId == UINT32_MAX) {
        if (m_batches.size() >= m_config.maxBatches) {
            NGE_LOG_ERROR("Indirect draw count: max batches reached ({})", m_config.maxBatches);
            return UINT32_MAX;
        }
        batchId = static_cast<u32>(m_batches.size());
        m_batches.emplace_back();
        m_active.push_back(false);
    }

    IndirectBatch& batch = m_batches[batchId];
    batch.batchId = batchId;
    batch.type = type;
    batch.argBufferHandle = argBuffer;
    batch.argBufferOffset = argOffset;
    batch.argStride = argStride;
    batch.countBufferHandle = countBuffer;
    batch.countBufferOffset = countOffset;
    batch.maxDrawCount = maxDrawCount > 0 ? maxDrawCount : m_config.defaultMaxDrawCount;
    batch.debugName = name;

    m_active[batchId] = true;

    u32 activeCount = 0;
    for (bool a : m_active) { if (a) activeCount++; }
    if (activeCount > m_peakBatches) m_peakBatches = activeCount;

    return batchId;
}

void IndirectDrawCountManager::UpdateBatch(u32 batchId, u64 argBuffer, u64 argOffset,
                                             u64 countBuffer, u64 countOffset) {
    std::lock_guard lock(m_mutex);

    if (batchId >= m_batches.size() || !m_active[batchId]) return;

    m_batches[batchId].argBufferHandle = argBuffer;
    m_batches[batchId].argBufferOffset = argOffset;
    m_batches[batchId].countBufferHandle = countBuffer;
    m_batches[batchId].countBufferOffset = countOffset;
}

void IndirectDrawCountManager::SetMaxDrawCount(u32 batchId, u32 maxDrawCount) {
    std::lock_guard lock(m_mutex);

    if (batchId >= m_batches.size() || !m_active[batchId]) return;

    m_batches[batchId].maxDrawCount = maxDrawCount;
}

void IndirectDrawCountManager::RecordSubmission(u32 batchId, u32 actualDrawCount) {
    std::lock_guard lock(m_mutex);

    if (!m_config.trackStats) return;
    if (batchId >= m_batches.size() || !m_active[batchId]) return;

    m_totalSubmissions++;

    u32 clampedCount = actualDrawCount;
    if (m_config.validateMaxCount) {
        clampedCount = std::min(actualDrawCount, m_batches[batchId].maxDrawCount);
    }

    m_totalDrawCalls += clampedCount;
    m_totalMaxDrawCalls += m_batches[batchId].maxDrawCount;
}

const IndirectBatch* IndirectDrawCountManager::GetBatch(u32 batchId) const {
    std::lock_guard lock(m_mutex);

    if (batchId >= m_batches.size() || !m_active[batchId]) return nullptr;
    return &m_batches[batchId];
}

std::vector<u32> IndirectDrawCountManager::GetActiveBatches() const {
    std::lock_guard lock(m_mutex);

    std::vector<u32> result;
    for (u32 i = 0; i < static_cast<u32>(m_active.size()); ++i) {
        if (m_active[i]) result.push_back(i);
    }
    return result;
}

void IndirectDrawCountManager::RemoveBatch(u32 batchId) {
    std::lock_guard lock(m_mutex);

    if (batchId >= m_active.size()) return;
    m_active[batchId] = false;
}

u32 IndirectDrawCountManager::GetBatchCount() const {
    std::lock_guard lock(m_mutex);

    u32 count = 0;
    for (bool a : m_active) { if (a) count++; }
    return count;
}

void IndirectDrawCountManager::ClearAll() {
    std::lock_guard lock(m_mutex);

    for (u32 i = 0; i < m_active.size(); ++i) {
        m_active[i] = false;
    }
}

void IndirectDrawCountManager::Reset() {
    std::lock_guard lock(m_mutex);

    m_batches.clear();
    m_active.clear();
    m_totalSubmissions = 0;
    m_totalDrawCalls = 0;
    m_totalMaxDrawCalls = 0;
    m_peakBatches = 0;
}

IndirectDrawCountStats IndirectDrawCountManager::GetStats() const {
    std::lock_guard lock(m_mutex);

    IndirectDrawCountStats stats{};

    u32 activeCount = 0;
    for (bool a : m_active) { if (a) activeCount++; }

    stats.totalBatches = activeCount;
    stats.totalSubmissions = m_totalSubmissions;
    stats.totalDrawCalls = m_totalDrawCalls;
    stats.totalMaxDrawCalls = m_totalMaxDrawCalls;
    stats.peakBatches = m_peakBatches;
    stats.avgUtilization = m_totalMaxDrawCalls > 0
        ? static_cast<float>(m_totalDrawCalls) / static_cast<float>(m_totalMaxDrawCalls)
        : 0.0f;

    return stats;
}

} // namespace nge::rhi
