#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>

namespace nge::renderer {

// ─── Particle System ─────────────────────────────────────────────────────
// GPU-driven particle system using compute shaders.
// Particles are fully simulated and sorted on the GPU.
//
// Pipeline:
//   1. Emit: Spawn new particles from emitters (compute)
//   2. Simulate: Apply forces, integrate velocity, kill dead particles (compute)
//   3. Sort: Bitonic sort by depth for alpha blending (compute)
//   4. Render: Billboarded quads or mesh particles (vertex + fragment)

// ─── Particle Data (GPU-side, per particle) ──────────────────────────────

struct GPUParticle {
    math::Vec3 position;
    f32        age;
    math::Vec3 velocity;
    f32        lifetime;
    math::Vec4 color;       // Current color (interpolated over lifetime)
    f32        size;
    f32        rotation;
    f32        angularVelocity;
    u32        alive;        // 0 = dead, 1 = alive
};

// ─── Emitter Shape ───────────────────────────────────────────────────────

enum class EmitterShape : u8 {
    Point,
    Sphere,
    Hemisphere,
    Cone,
    Box,
    Circle,
    Edge,
    Mesh,         // Emit from mesh surface
};

// ─── Curve (for property animation over lifetime) ────────────────────────

struct ParticleCurve {
    static constexpr u32 MAX_KEYS = 8;
    struct Key {
        f32 time;  // 0..1 normalized lifetime
        f32 value;
    };
    Key  keys[MAX_KEYS] = {};
    u32  keyCount = 0;

    f32 Evaluate(f32 t) const {
        if (keyCount == 0) return 1.0f;
        if (keyCount == 1) return keys[0].value;
        if (t <= keys[0].time) return keys[0].value;
        if (t >= keys[keyCount - 1].time) return keys[keyCount - 1].value;

        for (u32 i = 0; i < keyCount - 1; ++i) {
            if (t >= keys[i].time && t <= keys[i + 1].time) {
                f32 localT = (t - keys[i].time) / (keys[i + 1].time - keys[i].time);
                return keys[i].value + (keys[i + 1].value - keys[i].value) * localT;
            }
        }
        return 1.0f;
    }

    static ParticleCurve Constant(f32 val) {
        ParticleCurve c;
        c.keys[0] = {0, val};
        c.keys[1] = {1, val};
        c.keyCount = 2;
        return c;
    }

    static ParticleCurve Linear(f32 start, f32 end) {
        ParticleCurve c;
        c.keys[0] = {0, start};
        c.keys[1] = {1, end};
        c.keyCount = 2;
        return c;
    }
};

// ─── Gradient (for color over lifetime) ──────────────────────────────────

struct ParticleGradient {
    static constexpr u32 MAX_STOPS = 8;
    struct Stop {
        f32        time;
        math::Vec4 color;
    };
    Stop stops[MAX_STOPS] = {};
    u32  stopCount = 0;

    math::Vec4 Evaluate(f32 t) const;

    static ParticleGradient Solid(const math::Vec4& color) {
        ParticleGradient g;
        g.stops[0] = {0, color};
        g.stops[1] = {1, color};
        g.stopCount = 2;
        return g;
    }
};

// ─── Emitter Descriptor ──────────────────────────────────────────────────

struct ParticleEmitterDesc {
    std::string   name;
    u32           maxParticles  = 10000;
    f32           emitRate      = 100.0f;  // Particles per second
    f32           burstCount    = 0;       // One-shot burst count (0 = continuous)

    // Spawn properties
    EmitterShape  shape         = EmitterShape::Cone;
    math::Vec3    position      = {0, 0, 0};
    math::Vec3    direction     = {0, 1, 0};
    f32           shapeRadius   = 0.5f;
    f32           shapeAngle    = 0.5f;    // Cone half-angle (radians)
    math::Vec3    shapeSize     = {1, 1, 1}; // Box dimensions

    // Initial properties (min/max for random range)
    f32           lifetimeMin   = 1.0f;
    f32           lifetimeMax   = 3.0f;
    f32           speedMin      = 1.0f;
    f32           speedMax      = 5.0f;
    f32           sizeMin       = 0.1f;
    f32           sizeMax       = 0.5f;
    f32           rotationMin   = 0;
    f32           rotationMax   = 6.28f;
    f32           angVelMin     = 0;
    f32           angVelMax     = 1.0f;

    // Over-lifetime animation
    ParticleCurve sizeOverLife  = ParticleCurve::Constant(1.0f);
    ParticleCurve speedOverLife = ParticleCurve::Constant(1.0f);
    ParticleGradient colorOverLife = ParticleGradient::Solid({1, 1, 1, 1});

    // Forces
    math::Vec3    gravity       = {0, -9.81f, 0};
    f32           drag          = 0.1f;
    f32           turbulence    = 0;
    f32           turbulenceFreq = 1.0f;

    // Rendering
    rhi::TextureHandle texture;
    bool          additive      = false; // Additive blending
    bool          depthWrite    = false;
    bool          faceCamera    = true;  // Billboard
    bool          stretchedBillboard = false;
    f32           stretchFactor = 1.0f;
};

// ─── Particle Emitter (runtime state) ────────────────────────────────────

class ParticleEmitter {
public:
    bool Init(rhi::IDevice* device, const ParticleEmitterDesc& desc);
    void Shutdown();

    void Update(rhi::ICommandList* cmd, f32 deltaTime,
                const math::Vec3& cameraPos, const math::Mat4& viewProj);

    void Render(rhi::ICommandList* cmd, const math::Mat4& viewProj);

    void SetPosition(const math::Vec3& pos) { m_desc.position = pos; }
    void SetDirection(const math::Vec3& dir) { m_desc.direction = dir; }
    void Burst(u32 count);

    u32 GetAliveCount() const { return m_aliveCount; }
    const ParticleEmitterDesc& GetDesc() const { return m_desc; }

private:
    rhi::IDevice* m_device = nullptr;
    ParticleEmitterDesc m_desc;

    // GPU buffers
    rhi::BufferHandle m_particleBuffer;     // GPUParticle[]
    rhi::BufferHandle m_aliveListBuffer;    // Indices of alive particles
    rhi::BufferHandle m_deadListBuffer;     // Free particle indices
    rhi::BufferHandle m_counterBuffer;      // Alive/dead counts
    rhi::BufferHandle m_drawArgsBuffer;     // Indirect draw arguments
    rhi::BufferHandle m_sortKeysBuffer;     // Depth sort keys

    // Pipelines
    rhi::PipelineHandle m_emitPipeline;
    rhi::PipelineHandle m_simulatePipeline;
    rhi::PipelineHandle m_sortPipeline;
    rhi::PipelineHandle m_renderPipeline;

    u32 m_aliveCount = 0;
    f32 m_emitAccumulator = 0; // Fractional emit accumulator
};

// ─── Particle Manager ────────────────────────────────────────────────────

class ParticleManager {
public:
    bool Init(rhi::IDevice* device);
    void Shutdown();

    u32 CreateEmitter(const ParticleEmitterDesc& desc);
    void DestroyEmitter(u32 id);
    ParticleEmitter* GetEmitter(u32 id);

    void Update(rhi::ICommandList* cmd, f32 deltaTime,
                const math::Vec3& cameraPos, const math::Mat4& viewProj);

    void Render(rhi::ICommandList* cmd, const math::Mat4& viewProj);

    u32 GetTotalAliveParticles() const;

private:
    rhi::IDevice* m_device = nullptr;
    std::vector<ParticleEmitter> m_emitters;
    std::vector<u32> m_freeSlots;
};

} // namespace nge::renderer
