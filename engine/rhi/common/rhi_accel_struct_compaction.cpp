#include "engine/rhi/common/rhi_accel_struct_compaction.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool AccelStructCompactionManager::Init(const AccelCompactionConfig& config) {
    m_config = config;
    m_nextId = 1;
    m_totalBuilt = 0;
    m_totalCompacted = 0;
    m_totalFailed = 0;

    NGE_LOG_INFO("Accel struct compaction manager initialized: maxStructures={}, maxPerFrame={}, auto={}, defer={}",
                 config.maxStructures, config.maxCompactionsPerFrame, config.autoCompact, config.deferCompaction);
    return true;
}

void AccelStructCompactionManager::Shutdown() {
    if (!m_structures.empty()) {
        NGE_LOG_WARN("Accel struct compaction: {} structures still tracked at shutdown", m_structures.size());
    }
    m_structures.clear();
}

u64 AccelStructCompactionManager::RegisterBuilt(AccelStructType type, u64 handle, u64 bufferHandle,
                                                   u64 originalSize, u32 geometryCount,
                                                   const std::string& name) {
    std::lock_guard lock(m_mutex);

    if (m_structures.size() >= m_config.maxStructures) {
        NGE_LOG_WARN("Accel struct compaction: max structures reached ({})", m_config.maxStructures);
        return 0;
    }

    u64 id = m_nextId++;

    AccelStructInfo info;
    info.handle = handle;
    info.bufferHandle = bufferHandle;
    info.type = type;
    info.state = CompactionState::Built;
    info.originalSize = originalSize;
    info.compactedSize = 0;
    info.geometryCount = geometryCount;
    info.buildFrame = 0;
    info.compactFrame = 0;
    info.debugName = name;

    m_structures[id] = std::move(info);
    m_totalBuilt++;

    return id;
}

void AccelStructCompactionManager::MarkQueryPending(u64 accelId) {
    std::lock_guard lock(m_mutex);

    auto it = m_structures.find(accelId);
    if (it == m_structures.end()) return;

    if (it->second.state == CompactionState::Built) {
        it->second.state = CompactionState::QueryPending;
    }
}

void AccelStructCompactionManager::SetCompactedSize(u64 accelId, u64 compactedSize) {
    std::lock_guard lock(m_mutex);

    auto it = m_structures.find(accelId);
    if (it == m_structures.end()) return;

    it->second.compactedSize = compactedSize;
    it->second.state = CompactionState::QueryReady;
}

bool AccelStructCompactionManager::ShouldCompact(u64 accelId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_structures.find(accelId);
    if (it == m_structures.end()) return false;

    const auto& info = it->second;
    if (info.state != CompactionState::QueryReady) return false;
    if (info.compactedSize == 0 || info.compactedSize >= info.originalSize) return false;

    u64 savings = info.originalSize - info.compactedSize;

    // Check absolute threshold
    if (savings < m_config.minSavingsThreshold) return false;

    // Check ratio threshold
    float ratio = static_cast<float>(savings) / static_cast<float>(info.originalSize);
    if (ratio < m_config.minSavingsRatio) return false;

    return true;
}

void AccelStructCompactionManager::MarkCompacting(u64 accelId) {
    std::lock_guard lock(m_mutex);

    auto it = m_structures.find(accelId);
    if (it == m_structures.end()) return;

    it->second.state = CompactionState::Compacting;
}

void AccelStructCompactionManager::MarkCompacted(u64 accelId, u64 newHandle, u64 newBuffer, u32 currentFrame) {
    std::lock_guard lock(m_mutex);

    auto it = m_structures.find(accelId);
    if (it == m_structures.end()) return;

    it->second.handle = newHandle;
    it->second.bufferHandle = newBuffer;
    it->second.state = CompactionState::Compacted;
    it->second.compactFrame = currentFrame;
    m_totalCompacted++;
}

void AccelStructCompactionManager::MarkFailed(u64 accelId) {
    std::lock_guard lock(m_mutex);

    auto it = m_structures.find(accelId);
    if (it == m_structures.end()) return;

    it->second.state = CompactionState::Failed;
    m_totalFailed++;
}

std::vector<u64> AccelStructCompactionManager::GetBuiltStructures() const {
    std::lock_guard lock(m_mutex);

    std::vector<u64> result;
    for (const auto& [id, info] : m_structures) {
        if (info.state == CompactionState::Built) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<u64> AccelStructCompactionManager::GetReadyToCompact(u32 maxCount) const {
    std::lock_guard lock(m_mutex);

    std::vector<u64> result;
    for (const auto& [id, info] : m_structures) {
        if (info.state == CompactionState::QueryReady) {
            // Check if compaction is worthwhile
            if (info.compactedSize > 0 && info.compactedSize < info.originalSize) {
                u64 savings = info.originalSize - info.compactedSize;
                float ratio = static_cast<float>(savings) / static_cast<float>(info.originalSize);

                if (savings >= m_config.minSavingsThreshold && ratio >= m_config.minSavingsRatio) {
                    result.push_back(id);
                    if (result.size() >= maxCount) break;
                }
            }
        }
    }
    return result;
}

const AccelStructInfo* AccelStructCompactionManager::GetInfo(u64 accelId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_structures.find(accelId);
    if (it == m_structures.end()) return nullptr;
    return &it->second;
}

void AccelStructCompactionManager::Unregister(u64 accelId) {
    std::lock_guard lock(m_mutex);
    m_structures.erase(accelId);
}

std::vector<u64> AccelStructCompactionManager::ProcessFrame(u32 currentFrame) {
    std::lock_guard lock(m_mutex);

    std::vector<u64> toCompact;

    if (!m_config.autoCompact) return toCompact;

    u32 compactionsThisFrame = 0;

    for (auto& [id, info] : m_structures) {
        if (compactionsThisFrame >= m_config.maxCompactionsPerFrame) break;

        if (info.state == CompactionState::QueryReady) {
            if (info.compactedSize > 0 && info.compactedSize < info.originalSize) {
                u64 savings = info.originalSize - info.compactedSize;
                float ratio = static_cast<float>(savings) / static_cast<float>(info.originalSize);

                if (savings >= m_config.minSavingsThreshold && ratio >= m_config.minSavingsRatio) {
                    toCompact.push_back(id);
                    info.state = CompactionState::Compacting;
                    compactionsThisFrame++;
                }
            }
        }
    }

    return toCompact;
}

u32 AccelStructCompactionManager::GetCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_structures.size());
}

void AccelStructCompactionManager::Reset() {
    std::lock_guard lock(m_mutex);
    m_structures.clear();
    m_nextId = 1;
    m_totalBuilt = 0;
    m_totalCompacted = 0;
    m_totalFailed = 0;
}

AccelCompactionStats AccelStructCompactionManager::GetStats() const {
    std::lock_guard lock(m_mutex);

    AccelCompactionStats stats{};
    stats.totalStructures = static_cast<u32>(m_structures.size());
    stats.totalBuilt = m_totalBuilt;
    stats.totalCompacted = m_totalCompacted;
    stats.totalFailed = m_totalFailed;

    u32 pending = 0;
    u64 totalOriginal = 0;
    u64 totalCompacted = 0;
    u32 compactedCount = 0;

    for (const auto& [id, info] : m_structures) {
        totalOriginal += info.originalSize;

        if (info.state == CompactionState::Compacted) {
            totalCompacted += info.compactedSize;
            compactedCount++;
        } else {
            totalCompacted += info.originalSize; // Not yet compacted
        }

        if (info.state == CompactionState::QueryPending ||
            info.state == CompactionState::QueryReady ||
            info.state == CompactionState::Compacting) {
            pending++;
        }
    }

    stats.pendingCompactions = pending;
    stats.totalOriginalSize = totalOriginal;
    stats.totalCompactedSize = totalCompacted;
    stats.totalMemorySaved = totalOriginal > totalCompacted ? totalOriginal - totalCompacted : 0;
    stats.averageSavingsRatio = (compactedCount > 0 && totalOriginal > 0)
        ? static_cast<float>(stats.totalMemorySaved) / static_cast<float>(totalOriginal)
        : 0.0f;

    return stats;
}

} // namespace nge::rhi
