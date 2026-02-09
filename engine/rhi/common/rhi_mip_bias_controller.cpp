#include "engine/rhi/common/rhi_mip_bias_controller.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <cmath>

namespace nge::rhi {

bool MipBiasController::Init(const MipBiasControllerConfig& config) {
    m_config = config;
    m_currentVRAMPressure = 0.0f;

    NGE_LOG_INFO("Mip bias controller initialized: globalOffset={}, range=[{}, {}], vramThreshold={}",
                 config.globalBiasOffset, config.minBias, config.maxBias, config.vramPressureThreshold);
    return true;
}

void MipBiasController::Shutdown() {
    m_materials.clear();
}

void MipBiasController::RegisterMaterial(u32 materialId, MipBiasStrategy strategy, f32 initialBias) {
    std::lock_guard lock(m_mutex);

    MaterialMipBias bias;
    bias.materialId = materialId;
    bias.currentBias = initialBias;
    bias.targetBias = initialBias;
    bias.blendSpeed = m_config.defaultBlendSpeed;
    bias.strategy = strategy;
    bias.locked = false;

    m_materials[materialId] = bias;
}

void MipBiasController::UnregisterMaterial(u32 materialId) {
    std::lock_guard lock(m_mutex);
    m_materials.erase(materialId);
}

void MipBiasController::SetTargetBias(u32 materialId, f32 bias) {
    std::lock_guard lock(m_mutex);
    auto it = m_materials.find(materialId);
    if (it == m_materials.end()) return;
    it->second.targetBias = std::clamp(bias, m_config.minBias, m_config.maxBias);
}

void MipBiasController::LockBias(u32 materialId, bool locked) {
    std::lock_guard lock(m_mutex);
    auto it = m_materials.find(materialId);
    if (it != m_materials.end()) it->second.locked = locked;
}

f32 MipBiasController::GetEffectiveBias(u32 materialId) const {
    std::lock_guard lock(m_mutex);
    auto it = m_materials.find(materialId);
    if (it == m_materials.end()) return m_config.globalBiasOffset;
    return std::clamp(it->second.currentBias + m_config.globalBiasOffset,
                       m_config.minBias, m_config.maxBias);
}

std::vector<f32> MipBiasController::GetAllBiases(u32 maxMaterialId) const {
    std::lock_guard lock(m_mutex);
    std::vector<f32> biases(maxMaterialId, m_config.globalBiasOffset);

    for (const auto& [id, mat] : m_materials) {
        if (id < maxMaterialId) {
            biases[id] = std::clamp(mat.currentBias + m_config.globalBiasOffset,
                                     m_config.minBias, m_config.maxBias);
        }
    }

    return biases;
}

void MipBiasController::Update(f32 deltaTime, f32 vramUsagePercent) {
    std::lock_guard lock(m_mutex);
    m_currentVRAMPressure = vramUsagePercent;

    f32 pressureBias = ComputeVRAMPressureBias(vramUsagePercent);

    for (auto& [id, mat] : m_materials) {
        if (mat.locked) continue;

        f32 target = mat.targetBias;

        // Apply strategy-specific adjustments
        switch (mat.strategy) {
            case MipBiasStrategy::Fixed:
                // No adjustment
                break;

            case MipBiasStrategy::StreamingAdaptive:
                // targetBias is set externally by NotifyStreaming
                break;

            case MipBiasStrategy::VRAMPressure:
                target += pressureBias;
                break;

            case MipBiasStrategy::ScreenCoverage:
                // Coverage-based bias would be set externally per frame
                break;
        }

        target = std::clamp(target, m_config.minBias, m_config.maxBias);

        // Smooth blend toward target
        f32 speed = mat.blendSpeed * deltaTime * 60.0f; // Normalize to 60 FPS
        speed = std::clamp(speed, 0.0f, 1.0f);
        mat.currentBias = mat.currentBias + (target - mat.currentBias) * speed;
    }
}

void MipBiasController::NotifyStreaming(u32 materialId, bool isStreaming) {
    std::lock_guard lock(m_mutex);
    auto it = m_materials.find(materialId);
    if (it == m_materials.end()) return;

    if (isStreaming) {
        // Add extra bias during streaming to hide pop-in
        it->second.targetBias = m_config.streamingTransitionBias;
    } else {
        // Streaming complete, bias back to 0
        it->second.targetBias = 0.0f;
    }
}

void MipBiasController::SetGlobalOffset(f32 offset) {
    std::lock_guard lock(m_mutex);
    m_config.globalBiasOffset = offset;
}

void MipBiasController::ForceAllBias(f32 bias) {
    std::lock_guard lock(m_mutex);
    for (auto& [id, mat] : m_materials) {
        mat.currentBias = bias;
        mat.targetBias = bias;
    }
}

MipBiasControllerStats MipBiasController::GetStats() const {
    std::lock_guard lock(m_mutex);
    MipBiasControllerStats stats{};
    stats.trackedMaterials = static_cast<u32>(m_materials.size());
    stats.currentVRAMPressure = m_currentVRAMPressure;

    if (m_materials.empty()) {
        stats.averageBias = 0.0f;
        stats.minActiveBias = 0.0f;
        stats.maxActiveBias = 0.0f;
        return stats;
    }

    f32 sum = 0.0f;
    f32 minB = 1e10f;
    f32 maxB = -1e10f;
    u32 atMax = 0;

    for (const auto& [id, mat] : m_materials) {
        f32 effective = mat.currentBias + m_config.globalBiasOffset;
        sum += effective;
        minB = std::min(minB, effective);
        maxB = std::max(maxB, effective);
        if (effective >= m_config.maxBias - 0.01f) atMax++;
    }

    stats.averageBias = sum / static_cast<f32>(m_materials.size());
    stats.minActiveBias = minB;
    stats.maxActiveBias = maxB;
    stats.materialsAtMaxBias = atMax;

    return stats;
}

f32 MipBiasController::ComputeVRAMPressureBias(f32 vramUsagePercent) const {
    if (vramUsagePercent < m_config.vramPressureThreshold) return 0.0f;

    f32 pressure = (vramUsagePercent - m_config.vramPressureThreshold) /
                    (m_config.vramCriticalThreshold - m_config.vramPressureThreshold);
    pressure = std::clamp(pressure, 0.0f, 1.0f);

    // Quadratic ramp for more aggressive bias at high pressure
    return pressure * pressure * m_config.maxBias;
}

} // namespace nge::rhi
