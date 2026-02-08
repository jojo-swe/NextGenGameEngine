#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Buffer Usage Tracker ────────────────────────────────────────────
// Per-frame read/write access pattern analysis for GPU buffers.
// Tracks which passes read/write each buffer per frame, enabling:
//   - Automatic barrier insertion optimization
//   - Aliasing opportunity detection (non-overlapping lifetimes)
//   - Resource lifetime analysis for transient pool sizing
//   - Debug validation (read-after-write without barrier)

enum class AccessType : u8 {
    Read,
    Write,
    ReadWrite,
};

struct BufferAccessRecord {
    u64        bufferHandle;
    u32        passIndex;
    std::string passName;
    AccessType access;
    u32        queueFamily;
    u64        frameNumber;
};

struct BufferLifetime {
    u64        bufferHandle;
    std::string debugName;
    u32        firstPassIndex;
    u32        lastPassIndex;
    u32        readCount;
    u32        writeCount;
    bool       crossQueue;       // Accessed from multiple queue families
    u64        sizeBytes;
};

struct AliasingCandidate {
    u64 bufferA;
    u64 bufferB;
    f32 overlapFraction;  // 0 = no overlap, 1 = full overlap
    bool canAlias;
};

struct BufferUsageTrackerConfig {
    u32 maxTrackedBuffers = 4096;
    bool enableAliasingAnalysis = true;
    bool enableHazardDetection = true;
};

struct BufferUsageTrackerStats {
    u32 trackedBuffers;
    u32 accessRecordsThisFrame;
    u32 aliasingCandidates;
    u32 hazardsDetected;
    u32 crossQueueAccesses;
};

class BufferUsageTracker {
public:
    bool Init(const BufferUsageTrackerConfig& config = {});
    void Shutdown();

    // Record a buffer access for the current frame
    void RecordAccess(u64 bufferHandle, u32 passIndex, const std::string& passName,
                      AccessType access, u32 queueFamily = 0, u64 sizeBytes = 0,
                      const std::string& debugName = "");

    // End-of-frame analysis
    void EndFrame(u64 frameNumber);

    // Get lifetime info for a specific buffer
    BufferLifetime GetLifetime(u64 bufferHandle) const;

    // Get all buffer lifetimes for the current frame
    std::vector<BufferLifetime> GetAllLifetimes() const;

    // Get aliasing candidates (buffers with non-overlapping lifetimes)
    std::vector<AliasingCandidate> GetAliasingCandidates() const;

    // Get access records for a specific buffer
    std::vector<BufferAccessRecord> GetAccessHistory(u64 bufferHandle) const;

    // Check for read-after-write hazards (no barrier between write and read)
    std::vector<std::string> GetHazardWarnings() const;

    // Clear all tracking data
    void Clear();

    BufferUsageTrackerStats GetStats() const;

private:
    void AnalyzeLifetimes();
    void DetectHazards();
    void FindAliasingCandidates();

    BufferUsageTrackerConfig m_config;

    std::vector<BufferAccessRecord> m_currentFrameAccesses;
    std::unordered_map<u64, BufferLifetime> m_lifetimes;
    std::vector<AliasingCandidate> m_aliasingCandidates;
    std::vector<std::string> m_hazardWarnings;
    u64 m_currentFrame = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
