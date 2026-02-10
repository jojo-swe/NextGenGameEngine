#include "engine/rhi/common/rhi_draw_call_compactor.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool DrawCallCompactor::Init(const CompactorConfig& config) {
    m_config = config;
    m_isCompacted = false;
    m_removedZeroInstance = 0;
    m_removedZeroVertex = 0;
    m_mergedCount = 0;
    m_totalInstancesBefore = 0;
    m_totalInstancesAfter = 0;

    m_inputDraws.reserve(config.maxDraws);

    NGE_LOG_INFO("Draw call compactor initialized: maxDraws={}, removeZeroInst={}, merge={}, sortPipeline={}",
                 config.maxDraws, config.removeZeroInstance, config.mergeSameMesh, config.sortByPipeline);
    return true;
}

void DrawCallCompactor::Shutdown() {
    m_inputDraws.clear();
    m_compactedDraws.clear();
}

bool DrawCallCompactor::AddDraw(const CompactDrawArgs& draw) {
    std::lock_guard lock(m_mutex);

    if (m_inputDraws.size() >= m_config.maxDraws) {
        NGE_LOG_WARN("Draw call compactor: max draws reached ({})", m_config.maxDraws);
        return false;
    }

    m_inputDraws.push_back(draw);
    m_isCompacted = false;
    return true;
}

bool DrawCallCompactor::AddDraws(const std::vector<CompactDrawArgs>& draws) {
    std::lock_guard lock(m_mutex);

    if (m_inputDraws.size() + draws.size() > m_config.maxDraws) {
        NGE_LOG_WARN("Draw call compactor: batch would exceed max draws ({} + {} > {})",
                     m_inputDraws.size(), draws.size(), m_config.maxDraws);
        return false;
    }

    m_inputDraws.insert(m_inputDraws.end(), draws.begin(), draws.end());
    m_isCompacted = false;
    return true;
}

void DrawCallCompactor::Compact() {
    std::lock_guard lock(m_mutex);

    m_removedZeroInstance = 0;
    m_removedZeroVertex = 0;
    m_mergedCount = 0;

    // Count total instances before compaction
    m_totalInstancesBefore = 0;
    for (const auto& d : m_inputDraws) {
        m_totalInstancesBefore += d.instanceCount;
    }

    // Start with a copy
    m_compactedDraws = m_inputDraws;

    // Step 1: Remove zero draws
    RemoveZeroDraws();

    // Step 2: Sort (before merge so compatible draws are adjacent)
    SortDraws();

    // Step 3: Merge compatible draws
    if (m_config.mergeSameMesh) {
        MergeCompatible();
    }

    // Count total instances after compaction
    m_totalInstancesAfter = 0;
    for (const auto& d : m_compactedDraws) {
        m_totalInstancesAfter += d.instanceCount;
    }

    m_isCompacted = true;
}

const std::vector<CompactDrawArgs>& DrawCallCompactor::GetCompactedDraws() const {
    return m_compactedDraws;
}

u32 DrawCallCompactor::GetCompactedCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_compactedDraws.size());
}

u32 DrawCallCompactor::GetOriginalCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_inputDraws.size());
}

bool DrawCallCompactor::IsCompacted() const {
    std::lock_guard lock(m_mutex);
    return m_isCompacted;
}

void DrawCallCompactor::Clear() {
    std::lock_guard lock(m_mutex);
    m_inputDraws.clear();
    m_compactedDraws.clear();
    m_isCompacted = false;
}

void DrawCallCompactor::Reset() {
    std::lock_guard lock(m_mutex);
    m_inputDraws.clear();
    m_compactedDraws.clear();
    m_isCompacted = false;
    m_removedZeroInstance = 0;
    m_removedZeroVertex = 0;
    m_mergedCount = 0;
    m_totalInstancesBefore = 0;
    m_totalInstancesAfter = 0;
}

CompactorStats DrawCallCompactor::GetStats() const {
    std::lock_guard lock(m_mutex);

    CompactorStats stats{};
    stats.inputDraws = static_cast<u32>(m_inputDraws.size());
    stats.outputDraws = static_cast<u32>(m_compactedDraws.size());
    stats.removedZeroInstance = m_removedZeroInstance;
    stats.removedZeroVertex = m_removedZeroVertex;
    stats.mergedDraws = m_mergedCount;
    stats.totalInstancesBefore = m_totalInstancesBefore;
    stats.totalInstancesAfter = m_totalInstancesAfter;

    stats.compactionRatio = stats.inputDraws > 0
        ? static_cast<float>(stats.outputDraws) / static_cast<float>(stats.inputDraws)
        : 1.0f;

    return stats;
}

void DrawCallCompactor::RemoveZeroDraws() {
    auto it = std::remove_if(m_compactedDraws.begin(), m_compactedDraws.end(),
                              [this](const CompactDrawArgs& d) {
                                  if (m_config.removeZeroInstance && d.instanceCount == 0) {
                                      m_removedZeroInstance++;
                                      return true;
                                  }
                                  if (m_config.removeZeroVertex && d.indexCount == 0) {
                                      m_removedZeroVertex++;
                                      return true;
                                  }
                                  return false;
                              });
    m_compactedDraws.erase(it, m_compactedDraws.end());
}

void DrawCallCompactor::MergeCompatible() {
    if (m_compactedDraws.size() <= 1) return;

    std::vector<CompactDrawArgs> merged;
    merged.push_back(m_compactedDraws[0]);

    for (size_t i = 1; i < m_compactedDraws.size(); ++i) {
        auto& prev = merged.back();
        const auto& curr = m_compactedDraws[i];

        // Merge if same mesh, material, pipeline, and adjacent index ranges
        if (prev.meshId == curr.meshId &&
            prev.materialId == curr.materialId &&
            prev.pipelineKey == curr.pipelineKey &&
            prev.vertexOffset == curr.vertexOffset &&
            prev.firstIndex + prev.indexCount == curr.firstIndex) {
            prev.indexCount += curr.indexCount;
            prev.instanceCount = std::max(prev.instanceCount, curr.instanceCount);
            m_mergedCount++;
        } else {
            merged.push_back(curr);
        }
    }

    m_compactedDraws = std::move(merged);
}

void DrawCallCompactor::SortDraws() {
    if (m_config.sortByPipeline) {
        std::sort(m_compactedDraws.begin(), m_compactedDraws.end(),
                  [](const CompactDrawArgs& a, const CompactDrawArgs& b) {
                      if (a.pipelineKey != b.pipelineKey) return a.pipelineKey < b.pipelineKey;
                      if (a.materialId != b.materialId) return a.materialId < b.materialId;
                      if (a.meshId != b.meshId) return a.meshId < b.meshId;
                      return a.firstIndex < b.firstIndex;
                  });
    } else if (m_config.sortByMaterial) {
        std::sort(m_compactedDraws.begin(), m_compactedDraws.end(),
                  [](const CompactDrawArgs& a, const CompactDrawArgs& b) {
                      if (a.materialId != b.materialId) return a.materialId < b.materialId;
                      if (a.meshId != b.meshId) return a.meshId < b.meshId;
                      return a.firstIndex < b.firstIndex;
                  });
    }
}

} // namespace nge::rhi
