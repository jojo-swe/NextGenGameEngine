#include "engine/rhi/common/rhi_timestamp_calibration.h"
#include "engine/core/logging/log.h"
#include <chrono>
#include <numeric>
#include <algorithm>

namespace nge::rhi {

bool TimestampCalibration::Init(IDevice* device, const CalibrationConfig& config) {
    m_device = device;
    m_config = config;
    m_calibrationCount = 0;
    m_lastCalibrationFrame = 0;
    m_estimatedDriftNs = 0;

    // Query GPU timestamp period from device
    // VkPhysicalDeviceProperties::limits::timestampPeriod gives ns per tick
    // For now, use device query or default
    m_currentCalibration.gpuTickPeriodNs = 1.0; // Default 1ns per tick (override from device)

    // TODO: Query actual timestamp period
    // VkPhysicalDeviceProperties props;
    // vkGetPhysicalDeviceProperties(physicalDevice, &props);
    // m_currentCalibration.gpuTickPeriodNs = props.limits.timestampPeriod;

    // Perform initial warmup calibration
    m_history.reserve(config.warmupSamples + 100);
    for (u32 i = 0; i < config.warmupSamples; ++i) {
        Calibrate();
    }

    NGE_LOG_INFO("Timestamp calibration initialized: tick period = {:.3f} ns, {} warmup samples",
                 m_currentCalibration.gpuTickPeriodNs, config.warmupSamples);
    return true;
}

void TimestampCalibration::Shutdown() {
    m_history.clear();
}

void TimestampCalibration::Calibrate() {
    std::lock_guard lock(m_mutex);

    auto point = SampleCalibrationPoint();

    // Check drift from previous calibration
    if (m_calibrationCount > 0) {
        const auto& prev = m_currentCalibration;
        f64 gpuElapsedNs = static_cast<f64>(point.gpuTimestamp - prev.gpuTimestamp) * point.gpuTickPeriodNs;
        f64 cpuElapsedNs = static_cast<f64>(point.cpuTimestampNs - prev.cpuTimestampNs);
        m_estimatedDriftNs = cpuElapsedNs - gpuElapsedNs;
    }

    m_currentCalibration = point;
    m_history.push_back(point);
    m_calibrationCount++;
}

void TimestampCalibration::Update(u64 frameNumber) {
    if (frameNumber - m_lastCalibrationFrame >= m_config.recalibrationIntervalFrames) {
        Calibrate();
        m_lastCalibrationFrame = frameNumber;
    }
}

f64 TimestampCalibration::GpuTicksToNs(u64 gpuTicks) const {
    std::lock_guard lock(m_mutex);
    return static_cast<f64>(gpuTicks) * m_currentCalibration.gpuTickPeriodNs;
}

f64 TimestampCalibration::GpuTicksToCpuNs(u64 gpuTicks) const {
    std::lock_guard lock(m_mutex);
    const auto& cal = m_currentCalibration;

    // Map GPU tick to CPU wall-clock using calibration offset
    f64 gpuNs = static_cast<f64>(gpuTicks) * cal.gpuTickPeriodNs;
    f64 calGpuNs = static_cast<f64>(cal.gpuTimestamp) * cal.gpuTickPeriodNs;
    f64 offsetNs = gpuNs - calGpuNs;

    return static_cast<f64>(cal.cpuTimestampNs) + offsetNs;
}

f64 TimestampCalibration::GpuDurationMs(u64 startTick, u64 endTick) const {
    std::lock_guard lock(m_mutex);
    if (endTick < startTick) return 0.0;
    f64 deltaTicks = static_cast<f64>(endTick - startTick);
    return deltaTicks * m_currentCalibration.gpuTickPeriodNs / 1e6;
}

CalibrationPoint TimestampCalibration::GetCalibration() const {
    std::lock_guard lock(m_mutex);
    return m_currentCalibration;
}

CalibrationStats TimestampCalibration::GetStats() const {
    std::lock_guard lock(m_mutex);
    CalibrationStats stats;
    stats.gpuTickPeriodNs = m_currentCalibration.gpuTickPeriodNs;
    stats.estimatedDriftNs = m_estimatedDriftNs;
    stats.calibrationCount = m_calibrationCount;
    stats.lastCalibrationFrame = m_lastCalibrationFrame;
    return stats;
}

CalibrationPoint TimestampCalibration::SampleCalibrationPoint() {
    CalibrationPoint point;

    // TODO: Use VK_EXT_calibrated_timestamps if available
    // VkCalibratedTimestampInfoEXT timestampInfos[2];
    // timestampInfos[0].sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT;
    // timestampInfos[0].timeDomain = VK_TIME_DOMAIN_DEVICE_EXT;
    // timestampInfos[1].sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT;
    // timestampInfos[1].timeDomain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT; // or QPC on Windows
    // u64 timestamps[2], maxDeviation;
    // vkGetCalibratedTimestampsEXT(device, 2, timestampInfos, timestamps, &maxDeviation);
    // point.gpuTimestamp = timestamps[0];
    // point.cpuTimestampNs = timestamps[1];

    // Fallback: use CPU clock as approximation
    auto now = std::chrono::high_resolution_clock::now();
    point.cpuTimestampNs = static_cast<u64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
    point.gpuTimestamp = point.cpuTimestampNs; // Stub: assume GPU clock ≈ CPU clock
    point.gpuTickPeriodNs = m_currentCalibration.gpuTickPeriodNs;

    return point;
}

} // namespace nge::rhi
