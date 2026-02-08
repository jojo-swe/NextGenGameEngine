#include "engine/rhi/common/rhi_buffer_usage_tracker.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool BufferUsageTracker::Init(const BufferUsageTrackerConfig& config) {
    m_config = config;
    m_currentFrame = 0;
    NGE_LOG_INFO("Buffer usage tracker initialized: maxBuffers={}, aliasing={}, hazards={}",
                 config.maxTrackedBuffers, config.enableAliasingAnalysis, config.enableHazardDetection);
    return true;
}

void BufferUsageTracker::Shutdown() {
    m_currentFrameAccesses.clear();
    m_lifetimes.clear();
    m_aliasingCandidates.clear();
    m_hazardWarnings.clear();
}

void BufferUsageTracker::RecordAccess(u64 bufferHandle, u32 passIndex, const std::string& passName,
                                       AccessType access, u32 queueFamily, u64 sizeBytes,
                                       const std::string& debugName) {
    std::lock_guard lock(m_mutex);

    if (m_lifetimes.size() >= m_config.maxTrackedBuffers &&
        m_lifetimes.find(bufferHandle) == m_lifetimes.end()) {
        return; // Limit reached
    }

    BufferAccessRecord record;
    record.bufferHandle = bufferHandle;
    record.passIndex = passIndex;
    record.passName = passName;
    record.access = access;
    record.queueFamily = queueFamily;
    record.frameNumber = m_currentFrame;
    m_currentFrameAccesses.push_back(record);

    // Update or create lifetime
    auto it = m_lifetimes.find(bufferHandle);
    if (it == m_lifetimes.end()) {
        BufferLifetime lifetime;
        lifetime.bufferHandle = bufferHandle;
        lifetime.debugName = debugName;
        lifetime.firstPassIndex = passIndex;
        lifetime.lastPassIndex = passIndex;
        lifetime.readCount = 0;
        lifetime.writeCount = 0;
        lifetime.crossQueue = false;
        lifetime.sizeBytes = sizeBytes;
        m_lifetimes[bufferHandle] = lifetime;
        it = m_lifetimes.find(bufferHandle);
    }

    it->second.firstPassIndex = std::min(it->second.firstPassIndex, passIndex);
    it->second.lastPassIndex = std::max(it->second.lastPassIndex, passIndex);

    if (access == AccessType::Read || access == AccessType::ReadWrite) it->second.readCount++;
    if (access == AccessType::Write || access == AccessType::ReadWrite) it->second.writeCount++;

    if (sizeBytes > 0) it->second.sizeBytes = sizeBytes;
    if (!debugName.empty()) it->second.debugName = debugName;
}

void BufferUsageTracker::EndFrame(u64 frameNumber) {
    std::lock_guard lock(m_mutex);
    m_currentFrame = frameNumber;

    AnalyzeLifetimes();

    if (m_config.enableHazardDetection) {
        DetectHazards();
    }

    if (m_config.enableAliasingAnalysis) {
        FindAliasingCandidates();
    }
}

BufferLifetime BufferUsageTracker::GetLifetime(u64 bufferHandle) const {
    std::lock_guard lock(m_mutex);
    auto it = m_lifetimes.find(bufferHandle);
    if (it != m_lifetimes.end()) return it->second;
    return {};
}

std::vector<BufferLifetime> BufferUsageTracker::GetAllLifetimes() const {
    std::lock_guard lock(m_mutex);
    std::vector<BufferLifetime> result;
    result.reserve(m_lifetimes.size());
    for (const auto& [handle, lifetime] : m_lifetimes) {
        result.push_back(lifetime);
    }
    return result;
}

std::vector<AliasingCandidate> BufferUsageTracker::GetAliasingCandidates() const {
    std::lock_guard lock(m_mutex);
    return m_aliasingCandidates;
}

std::vector<BufferAccessRecord> BufferUsageTracker::GetAccessHistory(u64 bufferHandle) const {
    std::lock_guard lock(m_mutex);
    std::vector<BufferAccessRecord> result;
    for (const auto& record : m_currentFrameAccesses) {
        if (record.bufferHandle == bufferHandle) {
            result.push_back(record);
        }
    }
    return result;
}

std::vector<std::string> BufferUsageTracker::GetHazardWarnings() const {
    std::lock_guard lock(m_mutex);
    return m_hazardWarnings;
}

void BufferUsageTracker::Clear() {
    std::lock_guard lock(m_mutex);
    m_currentFrameAccesses.clear();
    m_lifetimes.clear();
    m_aliasingCandidates.clear();
    m_hazardWarnings.clear();
}

BufferUsageTrackerStats BufferUsageTracker::GetStats() const {
    std::lock_guard lock(m_mutex);
    BufferUsageTrackerStats stats{};
    stats.trackedBuffers = static_cast<u32>(m_lifetimes.size());
    stats.accessRecordsThisFrame = static_cast<u32>(m_currentFrameAccesses.size());
    stats.aliasingCandidates = static_cast<u32>(m_aliasingCandidates.size());
    stats.hazardsDetected = static_cast<u32>(m_hazardWarnings.size());

    u32 crossQueue = 0;
    for (const auto& [handle, lifetime] : m_lifetimes) {
        if (lifetime.crossQueue) crossQueue++;
    }
    stats.crossQueueAccesses = crossQueue;

    return stats;
}

void BufferUsageTracker::AnalyzeLifetimes() {
    // Detect cross-queue accesses
    for (auto& [handle, lifetime] : m_lifetimes) {
        std::unordered_map<u32, bool> queues;
        for (const auto& record : m_currentFrameAccesses) {
            if (record.bufferHandle == handle) {
                queues[record.queueFamily] = true;
            }
        }
        lifetime.crossQueue = queues.size() > 1;
    }
}

void BufferUsageTracker::DetectHazards() {
    m_hazardWarnings.clear();

    // Sort accesses by pass index
    auto sorted = m_currentFrameAccesses;
    std::sort(sorted.begin(), sorted.end(),
        [](const BufferAccessRecord& a, const BufferAccessRecord& b) {
            return a.passIndex < b.passIndex;
        });

    // For each buffer, check for RAW without explicit barrier
    // (Simplified: consecutive write then read in different passes without same-pass RW)
    std::unordered_map<u64, BufferAccessRecord> lastWrite;

    for (const auto& record : sorted) {
        if (record.access == AccessType::Write || record.access == AccessType::ReadWrite) {
            lastWrite[record.bufferHandle] = record;
        }

        if (record.access == AccessType::Read || record.access == AccessType::ReadWrite) {
            auto it = lastWrite.find(record.bufferHandle);
            if (it != lastWrite.end() && it->second.passIndex != record.passIndex) {
                // Write in pass A, read in pass B (A < B) — potential RAW hazard
                // In a proper system, the render graph would insert barriers
                // This is a diagnostic warning
                if (it->second.queueFamily != record.queueFamily) {
                    m_hazardWarnings.push_back(
                        "Cross-queue RAW: buffer " + std::to_string(record.bufferHandle) +
                        " written in '" + it->second.passName + "' (queue " +
                        std::to_string(it->second.queueFamily) + "), read in '" +
                        record.passName + "' (queue " + std::to_string(record.queueFamily) + ")");
                }
            }
        }
    }
}

void BufferUsageTracker::FindAliasingCandidates() {
    m_aliasingCandidates.clear();

    std::vector<std::pair<u64, BufferLifetime>> lifetimeVec(m_lifetimes.begin(), m_lifetimes.end());

    for (size_t i = 0; i < lifetimeVec.size(); ++i) {
        for (size_t j = i + 1; j < lifetimeVec.size(); ++j) {
            const auto& a = lifetimeVec[i].second;
            const auto& b = lifetimeVec[j].second;

            // Check if lifetimes overlap
            bool overlaps = !(a.lastPassIndex < b.firstPassIndex || b.lastPassIndex < a.firstPassIndex);

            AliasingCandidate candidate;
            candidate.bufferA = lifetimeVec[i].first;
            candidate.bufferB = lifetimeVec[j].first;

            if (!overlaps) {
                candidate.canAlias = true;
                candidate.overlapFraction = 0.0f;
                m_aliasingCandidates.push_back(candidate);
            } else {
                // Calculate overlap fraction
                u32 overlapStart = std::max(a.firstPassIndex, b.firstPassIndex);
                u32 overlapEnd = std::min(a.lastPassIndex, b.lastPassIndex);
                u32 totalRange = std::max(a.lastPassIndex, b.lastPassIndex) -
                                 std::min(a.firstPassIndex, b.firstPassIndex) + 1;
                float fraction = static_cast<f32>(overlapEnd - overlapStart + 1) /
                                 static_cast<f32>(totalRange);
                candidate.canAlias = false;
                candidate.overlapFraction = fraction;
                // Don't add overlapping pairs to candidates
            }
        }
    }
}

} // namespace nge::rhi
