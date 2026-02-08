#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/renderer/materials/material_system.h"
#include <vector>

namespace nge::renderer {

// ─── GPU Instance Manager ────────────────────────────────────────────────
// Collects per-frame instance data from the ECS and uploads it to a GPU
// structured buffer for indirect/bindless rendering.
//
// Each instance = world transform + mesh reference + material ID + flags.
// The GPU culling pipeline reads this buffer to determine visibility.

using MeshId = u32;
inline constexpr MeshId INVALID_MESH = UINT32_MAX;

struct alignas(16) GPUInstanceData {
    math::Mat4 worldMatrix;
    math::Mat4 prevWorldMatrix;  // For motion vectors
    math::Vec4 aabbMin;          // xyz = AABB min, w = mesh ID
    math::Vec4 aabbMax;          // xyz = AABB max, w = material ID
    u32        meshId;
    u32        materialId;
    u32        flags;            // Visibility flags, shadow caster, etc.
    u32        lodBias;          // Per-instance LOD bias
};

enum class InstanceFlags : u32 {
    None         = 0,
    Visible      = 1 << 0,
    CastShadow   = 1 << 1,
    ReceiveShadow = 1 << 2,
    Static       = 1 << 3,
    Dynamic      = 1 << 4,
    Skinned      = 1 << 5,
    Transparent  = 1 << 6,
};

inline InstanceFlags operator|(InstanceFlags a, InstanceFlags b) {
    return static_cast<InstanceFlags>(static_cast<u32>(a) | static_cast<u32>(b));
}

class InstanceManager {
public:
    bool Init(rhi::IDevice* device, u32 maxInstances = 65536);
    void Shutdown();

    // Per-frame lifecycle
    void BeginFrame();
    void EndFrame();

    // Submit an instance for rendering this frame
    u32 Submit(const GPUInstanceData& instance);

    // Batch submit
    void Submit(const GPUInstanceData* instances, u32 count);

    // Upload collected instances to GPU
    void Upload(rhi::ICommandList* cmd);

    // Get GPU buffer for shader binding
    rhi::BufferHandle GetInstanceBuffer() const { return m_gpuBuffer; }
    rhi::BufferHandle GetPrevInstanceBuffer() const { return m_prevGpuBuffer; }

    // Query
    u32 GetInstanceCount() const { return m_currentCount; }
    u32 GetMaxInstances() const { return m_maxInstances; }
    f32 GetUtilization() const;

    // Sort instances by material for better GPU coherence
    void SortByMaterial();

private:
    rhi::IDevice* m_device = nullptr;
    u32 m_maxInstances = 0;
    u32 m_currentCount = 0;

    // CPU-side staging
    std::vector<GPUInstanceData> m_cpuBuffer;

    // GPU buffers (double-buffered for motion vectors)
    rhi::BufferHandle m_gpuBuffer;
    rhi::BufferHandle m_prevGpuBuffer;
    rhi::BufferHandle m_stagingBuffer;

    bool m_dirty = false;
};

} // namespace nge::renderer
