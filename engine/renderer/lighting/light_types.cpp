#include "engine/renderer/lighting/light_types.h"
#include "engine/core/logging/log.h"

namespace nge::renderer {

bool LightManager::Init(rhi::IDevice* device, u32 maxLights) {
    m_device = device;

    rhi::BufferDesc desc;
    desc.size        = sizeof(GPULightData) * maxLights;
    desc.usage       = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;
    desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
    desc.debugName   = "LightBuffer";
    m_gpuBuffer = device->CreateBuffer(desc);

    // Default sun
    m_sunLight.type      = LightType::Directional;
    m_sunLight.direction = math::Vec3(0.5f, -0.8f, 0.3f).Normalized();
    m_sunLight.color     = {1.0f, 0.95f, 0.9f};
    m_sunLight.intensity = 5.0f;
    m_sunLight.castShadow = true;

    NGE_LOG_INFO("Light manager initialized: max {} lights", maxLights);
    return m_gpuBuffer.IsValid();
}

void LightManager::Shutdown() {
    if (m_device && m_gpuBuffer.IsValid()) {
        m_device->DestroyBuffer(m_gpuBuffer);
    }
    m_lights.clear();
}

u32 LightManager::AddLight(const Light& light) {
    u32 id = static_cast<u32>(m_lights.size());
    m_lights.push_back(light);
    m_dirty = true;
    return id;
}

void LightManager::RemoveLight(u32 id) {
    if (id < m_lights.size()) {
        m_lights[id].isActive = false;
        m_dirty = true;
    }
}

Light* LightManager::GetLight(u32 id) {
    if (id >= m_lights.size()) return nullptr;
    return &m_lights[id];
}

void LightManager::SetSunLight(const Light& sun) {
    m_sunLight = sun;
    m_sunLight.type = LightType::Directional;
    m_dirty = true;
}

void LightManager::UploadToGPU() {
    if (!m_dirty) return;

    std::vector<GPULightData> gpuData;
    gpuData.reserve(m_lights.size() + 1);

    // Sun is always light 0
    gpuData.push_back(m_sunLight.ToGPU());

    for (const auto& light : m_lights) {
        if (light.isActive) {
            gpuData.push_back(light.ToGPU());
        }
    }

    m_activeLightCount = static_cast<u32>(gpuData.size());

    // TODO: Upload via staging buffer → GPU buffer copy
    // m_device->UploadBufferData(m_gpuBuffer, gpuData.data(), gpuData.size() * sizeof(GPULightData));

    m_dirty = false;
    NGE_LOG_DEBUG("Uploaded {} lights to GPU", m_activeLightCount);
}

} // namespace nge::renderer
