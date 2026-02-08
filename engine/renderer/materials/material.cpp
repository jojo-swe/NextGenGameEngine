#include "engine/renderer/materials/material.h"
#include "engine/core/logging/log.h"

namespace nge::renderer {

bool MaterialManager::Init(rhi::IDevice* device, u32 maxMaterials) {
    m_device = device;

    rhi::BufferDesc desc;
    desc.size        = sizeof(GPUMaterialData) * maxMaterials;
    desc.usage       = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;
    desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
    desc.debugName   = "MaterialBuffer";
    m_gpuBuffer = device->CreateBuffer(desc);

    NGE_LOG_INFO("Material manager initialized: max {} materials", maxMaterials);
    return m_gpuBuffer.IsValid();
}

void MaterialManager::Shutdown() {
    if (m_device && m_gpuBuffer.IsValid()) {
        m_device->DestroyBuffer(m_gpuBuffer);
    }
    m_materials.clear();
}

u32 MaterialManager::CreateMaterial(const Material& mat) {
    u32 id = static_cast<u32>(m_materials.size());
    Material copy = mat;
    copy.materialId = id;
    m_materials.push_back(std::move(copy));
    m_dirty = true;
    return id;
}

Material* MaterialManager::GetMaterial(u32 id) {
    if (id >= m_materials.size()) return nullptr;
    return &m_materials[id];
}

const Material* MaterialManager::GetMaterial(u32 id) const {
    if (id >= m_materials.size()) return nullptr;
    return &m_materials[id];
}

void MaterialManager::UploadToGPU() {
    if (!m_dirty || m_materials.empty()) return;

    std::vector<GPUMaterialData> gpuData;
    gpuData.reserve(m_materials.size());
    for (const auto& mat : m_materials) {
        gpuData.push_back(mat.ToGPU(m_device));
    }

    // TODO: Upload via staging buffer → GPU buffer copy
    // m_device->UploadBufferData(m_gpuBuffer, gpuData.data(), gpuData.size() * sizeof(GPUMaterialData));

    m_dirty = false;
    NGE_LOG_DEBUG("Uploaded {} materials to GPU", m_materials.size());
}

} // namespace nge::renderer
