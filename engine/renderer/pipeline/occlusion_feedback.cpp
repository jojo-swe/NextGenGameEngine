#include "engine/renderer/pipeline/occlusion_feedback.h"
#include "engine/core/logging/log.h"
#include <cstring>
#include <algorithm>

namespace nge::renderer {

bool OcclusionFeedback::Init(rhi::IDevice* device, const OcclusionFeedbackConfig& config) {
    m_device = device;
    m_config = config;

    m_frames.resize(config.framesInFlight);
    for (u32 i = 0; i < config.framesInFlight; ++i) {
        rhi::BufferDesc desc;
        // Each instance: u32 visible + u32 pixelCount = 8 bytes
        desc.size = config.maxInstances * 8;
        desc.usage = rhi::BufferUsage::TransferDst;
        desc.memoryUsage = rhi::MemoryUsage::GPU_To_CPU;
        desc.debugName = "OcclusionReadback_" + std::to_string(i);
        m_frames[i].readbackBuffer = device->CreateBuffer(desc);
        m_frames[i].ready = false;
    }

    m_results.reserve(config.maxInstances);

    NGE_LOG_INFO("Occlusion feedback initialized: {} max instances, {} frames latency",
                 config.maxInstances, config.framesInFlight);
    return true;
}

void OcclusionFeedback::Shutdown() {
    for (auto& frame : m_frames) {
        if (frame.readbackBuffer.IsValid()) {
            m_device->DestroyBuffer(frame.readbackBuffer);
            frame.readbackBuffer = {};
        }
    }
    m_results.clear();
}

void OcclusionFeedback::BeginFrame(u32 frameIndex) {
    m_currentFrame = frameIndex % m_config.framesInFlight;
    m_frames[m_currentFrame].ready = false;
    m_frames[m_currentFrame].instanceCount = 0;
}

void OcclusionFeedback::RecordResults(rhi::ICommandList* cmd,
                                        rhi::BufferHandle visibilityBuffer,
                                        u32 instanceCount) {
    auto& frame = m_frames[m_currentFrame];
    frame.instanceCount = std::min(instanceCount, m_config.maxInstances);

    // Copy GPU visibility results to readback buffer
    u64 copySize = static_cast<u64>(frame.instanceCount) * 8;
    cmd->CopyBuffer(visibilityBuffer, 0, frame.readbackBuffer, 0, copySize);

    // Barrier: transfer → host read
    cmd->BufferBarrier(frame.readbackBuffer,
                       rhi::ResourceState::TransferDst,
                       rhi::ResourceState::HostRead);

    frame.ready = true;
}

void OcclusionFeedback::ReadBack() {
    std::lock_guard lock(m_mutex);

    // Read from the oldest frame (N-2)
    u32 readFrame = (m_currentFrame + 1) % m_config.framesInFlight;
    auto& frame = m_frames[readFrame];

    if (!frame.ready || frame.instanceCount == 0) {
        m_results.clear();
        m_visibleCount = 0;
        return;
    }

    // Map and read
    void* mapped = m_device->MapBuffer(frame.readbackBuffer);
    if (!mapped) {
        m_results.clear();
        m_visibleCount = 0;
        return;
    }

    m_results.resize(frame.instanceCount);
    m_visibleCount = 0;

    const u32* data = static_cast<const u32*>(mapped);
    for (u32 i = 0; i < frame.instanceCount; ++i) {
        auto& result = m_results[i];
        result.instanceId = i;
        result.visible = data[i * 2] != 0;
        result.pixelCount = data[i * 2 + 1];
        // Approximate screen coverage (assuming 1920×1080)
        result.screenCoverage = static_cast<f32>(result.pixelCount) / (1920.0f * 1080.0f);

        if (result.visible) m_visibleCount++;
    }

    m_device->UnmapBuffer(frame.readbackBuffer);
    frame.ready = false;
}

bool OcclusionFeedback::IsVisible(u32 instanceId) const {
    std::lock_guard lock(m_mutex);
    if (instanceId < m_results.size()) {
        return m_results[instanceId].visible;
    }
    return true; // Conservatively visible if unknown
}

f32 OcclusionFeedback::GetScreenCoverage(u32 instanceId) const {
    std::lock_guard lock(m_mutex);
    if (instanceId < m_results.size()) {
        return m_results[instanceId].screenCoverage;
    }
    return 0.0f;
}

f32 OcclusionFeedback::GetLODBias(u32 instanceId) const {
    std::lock_guard lock(m_mutex);
    if (instanceId >= m_results.size()) return 0.0f;

    f32 coverage = m_results[instanceId].screenCoverage;
    // Higher coverage = use finer LODs (negative bias)
    // Lower coverage = use coarser LODs (positive bias)
    if (coverage > 0.1f) return -1.0f;  // Large on screen → finest LOD
    if (coverage > 0.01f) return 0.0f;   // Medium → default LOD
    if (coverage > 0.001f) return 1.0f;  // Small → coarser
    return 2.0f;                          // Tiny → coarsest
}

f32 OcclusionFeedback::GetStreamingPriority(u32 instanceId) const {
    std::lock_guard lock(m_mutex);
    if (instanceId >= m_results.size()) return 0.0f;

    const auto& result = m_results[instanceId];
    if (!result.visible) return 0.0f;

    // Priority based on screen coverage (larger = higher priority)
    return std::min(result.screenCoverage * 100.0f, 1.0f);
}

OcclusionFeedbackStats OcclusionFeedback::GetStats() const {
    std::lock_guard lock(m_mutex);
    OcclusionFeedbackStats stats;
    stats.totalInstances = static_cast<u32>(m_results.size());
    stats.visibleInstances = m_visibleCount;
    stats.occludedInstances = stats.totalInstances - m_visibleCount;
    stats.occlusionRate = stats.totalInstances > 0
        ? static_cast<f32>(stats.occludedInstances) / static_cast<f32>(stats.totalInstances)
        : 0.0f;
    stats.frameLatency = m_config.framesInFlight - 1;
    return stats;
}

} // namespace nge::renderer
