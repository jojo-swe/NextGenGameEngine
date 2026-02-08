#include "engine/renderer/materials/material_system.h"
#include "engine/core/logging/log.h"
#include <cstring>

namespace nge::renderer {

bool MaterialManager::Init(rhi::IDevice* device, u32 maxMaterials) {
    m_device = device;
    m_maxMaterials = maxMaterials;

    // GPU structured buffer for all materials
    {
        rhi::BufferDesc desc;
        desc.size = maxMaterials * sizeof(GPUMaterialData);
        desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "MaterialBuffer";
        m_gpuBuffer = device->CreateBuffer(desc);
    }

    // Staging buffer for CPU → GPU uploads
    {
        rhi::BufferDesc desc;
        desc.size = maxMaterials * sizeof(GPUMaterialData);
        desc.usage = rhi::BufferUsage::TransferSrc;
        desc.memoryUsage = rhi::MemoryUsage::CPU_To_GPU;
        desc.debugName = "MaterialStaging";
        m_stagingBuffer = device->CreateBuffer(desc);
    }

    CreateDefaults();

    NGE_LOG_INFO("Material system initialized: max {} materials ({} KB GPU buffer)",
                 maxMaterials, maxMaterials * sizeof(GPUMaterialData) / 1024);
    return true;
}

void MaterialManager::Shutdown() {
    if (!m_device) return;

    if (m_gpuBuffer.IsValid()) { m_device->DestroyBuffer(m_gpuBuffer); m_gpuBuffer = {}; }
    if (m_stagingBuffer.IsValid()) { m_device->DestroyBuffer(m_stagingBuffer); m_stagingBuffer = {}; }

    m_materials.clear();
    m_nameToId.clear();
}

void MaterialManager::CreateDefaults() {
    // Default opaque: white, rough, non-metallic
    {
        m_defaultOpaque = CreateMaterial("__default_opaque");
        auto* mat = GetMaterial(m_defaultOpaque);
        mat->gpuData.baseColorFactor = {1, 1, 1, 1};
        mat->gpuData.metallicFactor = 0.0f;
        mat->gpuData.roughnessFactor = 0.8f;
        mat->gpuData.normalScale = 1.0f;
        mat->gpuData.flags = 0;
        mat->dirty = true;
    }

    // Default transparent
    {
        m_defaultTransparent = CreateMaterial("__default_transparent");
        auto* mat = GetMaterial(m_defaultTransparent);
        mat->gpuData.baseColorFactor = {1, 1, 1, 0.5f};
        mat->gpuData.metallicFactor = 0.0f;
        mat->gpuData.roughnessFactor = 0.5f;
        mat->gpuData.normalScale = 1.0f;
        mat->gpuData.flags = static_cast<u32>(MaterialFlags::AlphaBlend);
        mat->dirty = true;
    }
}

MaterialId MaterialManager::CreateMaterial(const std::string& name) {
    if (m_materials.size() >= m_maxMaterials) {
        NGE_LOG_ERROR("Material limit reached ({})", m_maxMaterials);
        return INVALID_MATERIAL;
    }

    MaterialId id = m_nextId++;

    MaterialInstance inst;
    inst.id = id;
    inst.name = name;
    inst.dirty = true;

    // Initialize GPU data with defaults
    std::memset(&inst.gpuData, 0, sizeof(GPUMaterialData));
    inst.gpuData.baseColorFactor = {1, 1, 1, 1};
    inst.gpuData.metallicFactor = 0.0f;
    inst.gpuData.roughnessFactor = 1.0f;
    inst.gpuData.normalScale = 1.0f;
    inst.gpuData.alphaCutoff = 0.5f;

    // No textures bound
    inst.gpuData.albedoTexIdx = UINT32_MAX;
    inst.gpuData.normalTexIdx = UINT32_MAX;
    inst.gpuData.metallicRoughnessTexIdx = UINT32_MAX;
    inst.gpuData.emissiveTexIdx = UINT32_MAX;
    inst.gpuData.aoTexIdx = UINT32_MAX;
    inst.gpuData.heightTexIdx = UINT32_MAX;
    inst.gpuData.detailAlbedoTexIdx = UINT32_MAX;
    inst.gpuData.detailNormalTexIdx = UINT32_MAX;

    for (auto& tex : inst.textures) tex = rhi::TextureHandle{};

    m_materials[id] = std::move(inst);
    m_nameToId[name] = id;

    return id;
}

MaterialId MaterialManager::CreateMaterialFromGLTF(const std::string& name,
                                                     const GPUMaterialData& data) {
    MaterialId id = CreateMaterial(name);
    if (id == INVALID_MATERIAL) return id;

    auto* mat = GetMaterial(id);
    mat->gpuData = data;
    mat->dirty = true;
    return id;
}

void MaterialManager::DestroyMaterial(MaterialId id) {
    auto it = m_materials.find(id);
    if (it != m_materials.end()) {
        m_nameToId.erase(it->second.name);
        m_materials.erase(it);
    }
}

MaterialInstance* MaterialManager::GetMaterial(MaterialId id) {
    auto it = m_materials.find(id);
    return it != m_materials.end() ? &it->second : nullptr;
}

const MaterialInstance* MaterialManager::GetMaterial(MaterialId id) const {
    auto it = m_materials.find(id);
    return it != m_materials.end() ? &it->second : nullptr;
}

void MaterialManager::SetBaseColor(MaterialId id, const math::Vec4& color) {
    auto* mat = GetMaterial(id);
    if (!mat) return;
    mat->gpuData.baseColorFactor = color;
    mat->dirty = true;
}

void MaterialManager::SetMetallicRoughness(MaterialId id, f32 metallic, f32 roughness) {
    auto* mat = GetMaterial(id);
    if (!mat) return;
    mat->gpuData.metallicFactor = metallic;
    mat->gpuData.roughnessFactor = roughness;
    mat->dirty = true;
}

void MaterialManager::SetEmissive(MaterialId id, f32 strength) {
    auto* mat = GetMaterial(id);
    if (!mat) return;
    mat->gpuData.emissiveStrength = strength;
    if (strength > 0) {
        mat->gpuData.flags |= static_cast<u32>(MaterialFlags::Emissive);
    }
    mat->dirty = true;
}

void MaterialManager::SetTexture(MaterialId id, TextureSlot slot,
                                   rhi::TextureHandle texture, u32 bindlessIdx) {
    auto* mat = GetMaterial(id);
    if (!mat) return;

    u32 slotIdx = static_cast<u32>(slot);
    if (slotIdx >= static_cast<u32>(TextureSlot::Count)) return;

    mat->textures[slotIdx] = texture;

    switch (slot) {
        case TextureSlot::Albedo:            mat->gpuData.albedoTexIdx = bindlessIdx; break;
        case TextureSlot::Normal:            mat->gpuData.normalTexIdx = bindlessIdx;
                                              mat->gpuData.flags |= static_cast<u32>(MaterialFlags::HasNormalMap); break;
        case TextureSlot::MetallicRoughness: mat->gpuData.metallicRoughnessTexIdx = bindlessIdx; break;
        case TextureSlot::Emissive:          mat->gpuData.emissiveTexIdx = bindlessIdx; break;
        case TextureSlot::AmbientOcclusion:  mat->gpuData.aoTexIdx = bindlessIdx; break;
        case TextureSlot::Height:            mat->gpuData.heightTexIdx = bindlessIdx;
                                              mat->gpuData.flags |= static_cast<u32>(MaterialFlags::HasHeightMap); break;
        case TextureSlot::DetailAlbedo:      mat->gpuData.detailAlbedoTexIdx = bindlessIdx;
                                              mat->gpuData.flags |= static_cast<u32>(MaterialFlags::HasDetailTextures); break;
        case TextureSlot::DetailNormal:      mat->gpuData.detailNormalTexIdx = bindlessIdx; break;
        default: break;
    }

    mat->dirty = true;
}

void MaterialManager::SetFlags(MaterialId id, MaterialFlags flags) {
    auto* mat = GetMaterial(id);
    if (!mat) return;
    mat->gpuData.flags = static_cast<u32>(flags);
    mat->dirty = true;
}

void MaterialManager::SetAlphaCutoff(MaterialId id, f32 cutoff) {
    auto* mat = GetMaterial(id);
    if (!mat) return;
    mat->gpuData.alphaCutoff = cutoff;
    mat->dirty = true;
}

MaterialId MaterialManager::FindByName(const std::string& name) const {
    auto it = m_nameToId.find(name);
    return it != m_nameToId.end() ? it->second : INVALID_MATERIAL;
}

void MaterialManager::UploadDirtyMaterials() {
    // Map staging buffer
    void* mapped = m_device->MapBuffer(m_stagingBuffer);
    if (!mapped) return;

    auto* staging = static_cast<GPUMaterialData*>(mapped);
    bool anyDirty = false;

    for (auto& [id, mat] : m_materials) {
        if (!mat.dirty) continue;
        if (id >= m_maxMaterials) continue;

        staging[id] = mat.gpuData;
        mat.dirty = false;
        anyDirty = true;
    }

    m_device->UnmapBuffer(m_stagingBuffer);

    if (anyDirty) {
        // Copy staging → GPU
        auto* cmd = m_device->GetCommandList();
        cmd->CopyBuffer(m_stagingBuffer, 0, m_gpuBuffer, 0,
                         m_maxMaterials * sizeof(GPUMaterialData));
    }
}

} // namespace nge::renderer
