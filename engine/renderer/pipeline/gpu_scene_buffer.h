#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <mutex>

namespace nge::renderer {

// ─── GPU Scene Buffer ────────────────────────────────────────────────────
// Centralized GPU-side scene representation. Maintains structured buffers
// for all per-frame scene data consumed by GPU-driven rendering:
//   - Instance transforms (current + previous for motion vectors)
//   - Material parameters
//   - Light data
//   - Mesh draw arguments
//
// Updated once per frame via staging upload. All shaders bind this as
// a single set of SRVs rather than per-object constant buffers.

struct GPUSceneInstance {
    math::Mat4 worldMatrix;
    math::Mat4 prevWorldMatrix;
    math::Vec4 boundingSphere;   // xyz=center, w=radius
    u32        materialIndex;
    u32        meshIndex;
    u32        flags;
    u32        pad;
};

struct GPUSceneMaterial {
    math::Vec4 baseColorFactor;
    f32        metallicFactor;
    f32        roughnessFactor;
    f32        normalScale;
    f32        occlusionStrength;
    math::Vec3 emissiveFactor;
    f32        alphaCutoff;
    u32        baseColorTexIndex;
    u32        normalTexIndex;
    u32        metallicRoughnessTexIndex;
    u32        emissiveTexIndex;
    u32        occlusionTexIndex;
    u32        flags;
    u32        pad[2];
};

struct GPUSceneConfig {
    u32 maxInstances = 65536;
    u32 maxMaterials = 4096;
    u32 maxLights = 4096;
    u32 maxDrawCommands = 65536;
};

class GPUSceneBuffer {
public:
    bool Init(rhi::IDevice* device, const GPUSceneConfig& config = {});
    void Shutdown();

    // Per-frame update
    void BeginFrame();

    // Upload instance data
    void SetInstances(const GPUSceneInstance* data, u32 count);

    // Upload material data
    void SetMaterials(const GPUSceneMaterial* data, u32 count);

    // Upload to GPU (call after all Set* calls)
    void Upload(rhi::ICommandList* cmd);

    // Bind scene buffers to a command list
    void Bind(rhi::ICommandList* cmd) const;

    // Accessors
    rhi::BufferHandle GetInstanceBuffer() const { return m_instanceBuffer; }
    rhi::BufferHandle GetPrevInstanceBuffer() const { return m_prevInstanceBuffer; }
    rhi::BufferHandle GetMaterialBuffer() const { return m_materialBuffer; }

    u32 GetInstanceCount() const { return m_instanceCount; }
    u32 GetMaterialCount() const { return m_materialCount; }

    const GPUSceneConfig& GetConfig() const { return m_config; }

private:
    rhi::IDevice* m_device = nullptr;
    GPUSceneConfig m_config;

    // GPU buffers
    rhi::BufferHandle m_instanceBuffer;
    rhi::BufferHandle m_prevInstanceBuffer;
    rhi::BufferHandle m_materialBuffer;
    rhi::BufferHandle m_stagingBuffer;

    // CPU staging
    std::vector<GPUSceneInstance> m_cpuInstances;
    std::vector<GPUSceneMaterial> m_cpuMaterials;

    u32 m_instanceCount = 0;
    u32 m_materialCount = 0;
    bool m_instancesDirty = false;
    bool m_materialsDirty = false;

    std::mutex m_mutex;
};

} // namespace nge::renderer
