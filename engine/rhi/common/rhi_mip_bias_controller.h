#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Texture Mip Bias Controller ─────────────────────────────────────
// Dynamic per-material mip bias for texture streaming LOD control.
// Adjusts sampler mip LOD bias based on streaming state, VRAM pressure,
// and screen-space coverage to avoid pop-in during streaming.
//
// Use cases:
//   - Smooth LOD transitions during texture streaming
//   - VRAM pressure response (increase bias to reduce memory)
//   - Per-material quality settings
//   - Virtual texturing residency-based bias

enum class MipBiasStrategy : u8 {
    Fixed,            // Constant bias value
    StreamingAdaptive,// Based on texture streaming state
    VRAMPressure,     // Increases bias as VRAM fills up
    ScreenCoverage,   // Based on screen-space texel density
};

struct MaterialMipBias {
    u32              materialId;
    f32              currentBias;
    f32              targetBias;
    f32              blendSpeed;    // Lerp speed per frame (default 0.1)
    MipBiasStrategy  strategy;
    bool             locked;        // Don't auto-adjust
};

struct MipBiasControllerConfig {
    f32  globalBiasOffset = 0.0f;   // Added to all biases
    f32  minBias = -2.0f;           // Sharpest allowed
    f32  maxBias = 4.0f;            // Blurriest allowed
    f32  vramPressureThreshold = 0.85f; // Start increasing bias at 85% VRAM
    f32  vramCriticalThreshold = 0.95f; // Max bias at 95% VRAM
    f32  defaultBlendSpeed = 0.1f;
    f32  streamingTransitionBias = 1.0f; // Extra bias during streaming transitions
};

struct MipBiasControllerStats {
    u32 trackedMaterials;
    f32 averageBias;
    f32 minActiveBias;
    f32 maxActiveBias;
    u32 materialsAtMaxBias;
    f32 currentVRAMPressure;
};

class MipBiasController {
public:
    bool Init(const MipBiasControllerConfig& config = {});
    void Shutdown();

    // Register a material for mip bias tracking
    void RegisterMaterial(u32 materialId, MipBiasStrategy strategy = MipBiasStrategy::StreamingAdaptive,
                           f32 initialBias = 0.0f);

    // Unregister
    void UnregisterMaterial(u32 materialId);

    // Set target bias for a specific material
    void SetTargetBias(u32 materialId, f32 bias);

    // Lock/unlock a material's bias (prevent auto-adjustment)
    void LockBias(u32 materialId, bool locked);

    // Get current effective bias for a material (includes global offset)
    f32 GetEffectiveBias(u32 materialId) const;

    // Get all material biases (for GPU upload)
    std::vector<f32> GetAllBiases(u32 maxMaterialId) const;

    // Per-frame update: blend toward targets, respond to VRAM pressure
    void Update(f32 deltaTime, f32 vramUsagePercent);

    // Notify that a material's textures are streaming
    void NotifyStreaming(u32 materialId, bool isStreaming);

    // Set global bias offset (e.g., from quality settings)
    void SetGlobalOffset(f32 offset);

    // Force all materials to a specific bias (e.g., during loading)
    void ForceAllBias(f32 bias);

    MipBiasControllerStats GetStats() const;

private:
    f32 ComputeVRAMPressureBias(f32 vramUsagePercent) const;

    MipBiasControllerConfig m_config;
    std::unordered_map<u32, MaterialMipBias> m_materials;
    f32 m_currentVRAMPressure = 0.0f;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
