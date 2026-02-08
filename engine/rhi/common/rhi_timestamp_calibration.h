#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <mutex>

namespace nge::rhi {

// ─── GPU Timestamp Calibration ───────────────────────────────────────────
// Synchronizes CPU and GPU clocks for accurate cross-domain profiling.
// Uses VK_EXT_calibrated_timestamps or fallback estimation to convert
// GPU timestamp ticks to CPU wall-clock time.
//
// GPU timestamps are in device-specific ticks. This system provides:
//   - GPU tick → nanoseconds conversion
//   - GPU tick → CPU wall-clock mapping
//   - Drift correction via periodic recalibration

struct CalibrationConfig {
    u32 recalibrationIntervalFrames = 60; // Recalibrate every N frames
    u32 warmupSamples = 5;                // Initial calibration samples
    f64 maxDriftToleranceNs = 1000.0;     // 1μs drift triggers recalibration
};

struct CalibrationPoint {
    u64 gpuTimestamp;
    u64 cpuTimestampNs;
    f64 gpuTickPeriodNs;  // Nanoseconds per GPU tick
};

struct CalibrationStats {
    f64 gpuTickPeriodNs;
    f64 estimatedDriftNs;
    u32 calibrationCount;
    u64 lastCalibrationFrame;
};

class TimestampCalibration {
public:
    bool Init(IDevice* device, const CalibrationConfig& config = {});
    void Shutdown();

    // Perform calibration (call periodically or when drift detected)
    void Calibrate();

    // Per-frame update (auto-recalibrates based on config interval)
    void Update(u64 frameNumber);

    // Convert GPU timestamp to nanoseconds (relative to GPU epoch)
    f64 GpuTicksToNs(u64 gpuTicks) const;

    // Convert GPU timestamp to CPU wall-clock nanoseconds
    f64 GpuTicksToCpuNs(u64 gpuTicks) const;

    // Convert duration between two GPU timestamps to milliseconds
    f64 GpuDurationMs(u64 startTick, u64 endTick) const;

    // Get current calibration point
    CalibrationPoint GetCalibration() const;

    // Get GPU tick period
    f64 GetTickPeriodNs() const { return m_currentCalibration.gpuTickPeriodNs; }

    CalibrationStats GetStats() const;

private:
    CalibrationPoint SampleCalibrationPoint();

    IDevice* m_device = nullptr;
    CalibrationConfig m_config;

    CalibrationPoint m_currentCalibration{};
    std::vector<CalibrationPoint> m_history;

    u32 m_calibrationCount = 0;
    u64 m_lastCalibrationFrame = 0;
    f64 m_estimatedDriftNs = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
