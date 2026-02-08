#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>

namespace nge::renderer {

// ─── Irradiance Probe System (Lumen-like Hybrid GI) ─────────────────────
// World-space probe grid storing Spherical Harmonic coefficients.
// Probes are updated incrementally each frame via ray tracing.
//
// Architecture:
//   1. Place probes on a uniform grid (or cascaded grids)
//   2. Each frame, trace rays from a subset of probes
//   3. Store irradiance as L2 SH (9 coefficients × 3 colors = 27 floats)
//   4. During shading, trilinearly interpolate probes for smooth diffuse GI
//   5. For specular: screen-space reflections + SDF ray march fallback

// ─── SH Coefficients (L2 = 9 coefficients per color channel) ────────────
struct SH9 {
    f32 c[9] = {};

    static SH9 Zero() { return SH9{}; }

    SH9 operator+(const SH9& o) const {
        SH9 r;
        for (int i = 0; i < 9; ++i) r.c[i] = c[i] + o.c[i];
        return r;
    }
    SH9 operator*(f32 s) const {
        SH9 r;
        for (int i = 0; i < 9; ++i) r.c[i] = c[i] * s;
        return r;
    }

    // Project a direction onto SH basis functions
    static SH9 Evaluate(const math::Vec3& dir) {
        SH9 r;
        // Band 0
        r.c[0] = 0.282095f; // Y_0^0
        // Band 1
        r.c[1] = 0.488603f * dir.y;  // Y_1^-1
        r.c[2] = 0.488603f * dir.z;  // Y_1^0
        r.c[3] = 0.488603f * dir.x;  // Y_1^1
        // Band 2
        r.c[4] = 1.092548f * dir.x * dir.y;                      // Y_2^-2
        r.c[5] = 1.092548f * dir.y * dir.z;                      // Y_2^-1
        r.c[6] = 0.315392f * (3.0f * dir.z * dir.z - 1.0f);     // Y_2^0
        r.c[7] = 1.092548f * dir.x * dir.z;                      // Y_2^1
        r.c[8] = 0.546274f * (dir.x * dir.x - dir.y * dir.y);   // Y_2^2
        return r;
    }

    // Reconstruct irradiance from SH in a given direction
    f32 Decode(const math::Vec3& dir) const {
        SH9 basis = Evaluate(dir);
        f32 result = 0;
        for (int i = 0; i < 9; ++i) result += c[i] * basis.c[i];
        return result;
    }
};

// ─── GPU Probe Data (uploaded to storage buffer) ─────────────────────────
struct alignas(16) GPUProbeData {
    SH9 shR;    // 36 bytes
    SH9 shG;    // 36 bytes
    SH9 shB;    // 36 bytes
    f32 pad[7]; // Pad to 128 bytes for alignment
};

static_assert(sizeof(GPUProbeData) == 128, "GPUProbeData must be 128 bytes");

// ─── Probe Grid Configuration ────────────────────────────────────────────
struct ProbeGridConfig {
    math::Vec3 origin    = {0, 0, 0};   // Grid origin (world space)
    math::Vec3 spacing   = {2, 2, 2};   // Distance between probes
    u32        countX    = 32;           // Probes per axis
    u32        countY    = 16;
    u32        countZ    = 32;
    u32        raysPerProbe = 64;        // Rays traced per probe per update
    u32        probesPerFrame = 256;     // Probes updated per frame (budget)
    f32        hysteresis = 0.97f;       // Temporal blend factor
};

// ─── GI Probe System ─────────────────────────────────────────────────────

class GIProbeSystem {
public:
    bool Init(rhi::IDevice* device, const ProbeGridConfig& config = {});
    void Shutdown();

    // Per-frame update: trace rays from subset of probes, update SH
    void Update(rhi::ICommandList* cmd,
                const math::Vec3& cameraPos,
                rhi::AccelStructHandle tlas);

    // Reconfigure grid (e.g., on level load)
    void SetGridConfig(const ProbeGridConfig& config);

    // GPU buffers for shader access
    rhi::BufferHandle GetProbeBuffer() const { return m_probeBuffer; }
    rhi::BufferHandle GetProbeGridInfoBuffer() const { return m_gridInfoBuffer; }

    // Stats
    u32 GetTotalProbes() const { return m_config.countX * m_config.countY * m_config.countZ; }
    u32 GetProbesUpdatedThisFrame() const { return m_probesUpdatedThisFrame; }

    const ProbeGridConfig& GetConfig() const { return m_config; }

private:
    // Select which probes to update this frame (round-robin + distance bias)
    void SelectProbesToUpdate(const math::Vec3& cameraPos);

    // World position of probe at grid coordinates
    math::Vec3 ProbeWorldPos(u32 ix, u32 iy, u32 iz) const;

    // Grid index from 3D coordinates
    u32 ProbeIndex(u32 ix, u32 iy, u32 iz) const {
        return iz * m_config.countY * m_config.countX + iy * m_config.countX + ix;
    }

    rhi::IDevice*    m_device = nullptr;
    ProbeGridConfig  m_config;

    // GPU buffers
    rhi::BufferHandle m_probeBuffer;     // SH data per probe
    rhi::BufferHandle m_gridInfoBuffer;  // Grid config for shaders
    rhi::BufferHandle m_rayResultBuffer; // Traced ray results (staging)

    // Pipelines
    rhi::PipelineHandle m_traceProbesPipeline;  // RT or compute for probe tracing
    rhi::PipelineHandle m_updateProbesPipeline; // Compute: integrate rays → SH

    // CPU-side probe data (for initial upload and readback)
    std::vector<GPUProbeData> m_probeData;

    // Update scheduling
    u32 m_updateOffset = 0;     // Round-robin offset
    u32 m_probesUpdatedThisFrame = 0;
};

// ─── GPU Grid Info (uploaded to uniform buffer) ──────────────────────────
struct GPUProbeGridInfo {
    math::Vec4 origin;     // xyz = origin, w = unused
    math::Vec4 spacing;    // xyz = spacing, w = unused
    u32        countX;
    u32        countY;
    u32        countZ;
    u32        totalProbes;
    math::Vec4 gridMin;    // xyz = origin (same as origin for axis-aligned)
    math::Vec4 gridMax;    // xyz = origin + count * spacing
};

} // namespace nge::renderer
