#include "engine/renderer/particles/particle_system.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"

namespace nge::renderer {

// ─── ParticleGradient ────────────────────────────────────────────────────

math::Vec4 ParticleGradient::Evaluate(f32 t) const {
    if (stopCount == 0) return {1, 1, 1, 1};
    if (stopCount == 1) return stops[0].color;
    if (t <= stops[0].time) return stops[0].color;
    if (t >= stops[stopCount - 1].time) return stops[stopCount - 1].color;

    for (u32 i = 0; i < stopCount - 1; ++i) {
        if (t >= stops[i].time && t <= stops[i + 1].time) {
            f32 localT = (t - stops[i].time) / (stops[i + 1].time - stops[i].time);
            return {
                stops[i].color.x + (stops[i + 1].color.x - stops[i].color.x) * localT,
                stops[i].color.y + (stops[i + 1].color.y - stops[i].color.y) * localT,
                stops[i].color.z + (stops[i + 1].color.z - stops[i].color.z) * localT,
                stops[i].color.w + (stops[i + 1].color.w - stops[i].color.w) * localT,
            };
        }
    }
    return {1, 1, 1, 1};
}

// ─── ParticleEmitter ─────────────────────────────────────────────────────

bool ParticleEmitter::Init(rhi::IDevice* device, const ParticleEmitterDesc& desc) {
    m_device = device;
    m_desc = desc;

    auto createBuf = [&](usize size, rhi::BufferUsage usage, const char* name) -> rhi::BufferHandle {
        rhi::BufferDesc bd;
        bd.size = size;
        bd.usage = usage;
        bd.memoryUsage = rhi::MemoryUsage::GPU_Only;
        bd.debugName = name;
        return device->CreateBuffer(bd);
    };

    auto storageIndirect = rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect;

    m_particleBuffer = createBuf(
        sizeof(GPUParticle) * desc.maxParticles,
        rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst,
        "Particle_Data");

    m_aliveListBuffer = createBuf(
        sizeof(u32) * desc.maxParticles,
        rhi::BufferUsage::Storage, "Particle_AliveList");

    m_deadListBuffer = createBuf(
        sizeof(u32) * desc.maxParticles,
        rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst,
        "Particle_DeadList");

    m_counterBuffer = createBuf(
        sizeof(u32) * 4, // aliveCount, deadCount, emitCount, pad
        rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst,
        "Particle_Counters");

    m_drawArgsBuffer = createBuf(
        sizeof(u32) * 5, // Indirect draw args
        storageIndirect, "Particle_DrawArgs");

    m_sortKeysBuffer = createBuf(
        sizeof(u64) * desc.maxParticles, // depth | index
        rhi::BufferUsage::Storage, "Particle_SortKeys");

    NGE_LOG_INFO("Particle emitter '{}' initialized: max {} particles",
                 desc.name, desc.maxParticles);
    return true;
}

void ParticleEmitter::Shutdown() {
    if (!m_device) return;

    auto destroyBuf = [&](rhi::BufferHandle& h) {
        if (h.IsValid()) { m_device->DestroyBuffer(h); h = rhi::BufferHandle{}; }
    };

    destroyBuf(m_particleBuffer);
    destroyBuf(m_aliveListBuffer);
    destroyBuf(m_deadListBuffer);
    destroyBuf(m_counterBuffer);
    destroyBuf(m_drawArgsBuffer);
    destroyBuf(m_sortKeysBuffer);
}

void ParticleEmitter::Update(rhi::ICommandList* cmd, f32 deltaTime,
                              const math::Vec3& /*cameraPos*/,
                              const math::Mat4& /*viewProj*/) {
    cmd->BeginDebugLabel("Particle Update", 0.8f, 0.6f, 0.2f);

    // Calculate emit count for this frame
    m_emitAccumulator += m_desc.emitRate * deltaTime;
    u32 emitCount = static_cast<u32>(m_emitAccumulator);
    m_emitAccumulator -= static_cast<f32>(emitCount);

    // Pass 1: Emit new particles
    if (m_emitPipeline.IsValid() && emitCount > 0) {
        cmd->BindComputePipeline(m_emitPipeline);
        // Push constants: emitCount, position, direction, shape params, lifetime range, speed range, etc.
        cmd->Dispatch((emitCount + 63) / 64, 1, 1);

        cmd->BufferBarrier(m_particleBuffer,
                            rhi::ResourceState::ShaderWrite,
                            rhi::ResourceState::ShaderWrite);
    }

    // Pass 2: Simulate (integrate, kill dead, apply forces)
    if (m_simulatePipeline.IsValid()) {
        cmd->BindComputePipeline(m_simulatePipeline);
        // Push constants: deltaTime, gravity, drag, turbulence params
        cmd->Dispatch((m_desc.maxParticles + 63) / 64, 1, 1);

        cmd->BufferBarrier(m_particleBuffer,
                            rhi::ResourceState::ShaderWrite,
                            rhi::ResourceState::ShaderRead);
    }

    // Pass 3: Sort by depth (for alpha blending)
    // TODO: Bitonic sort compute dispatch

    // Update alive count (read back or use atomic counter)
    // TODO: Read counter buffer for m_aliveCount

    cmd->EndDebugLabel();
}

void ParticleEmitter::Render(rhi::ICommandList* cmd, const math::Mat4& /*viewProj*/) {
    if (m_aliveCount == 0) return;

    cmd->BeginDebugLabel("Particle Render", 0.9f, 0.7f, 0.3f);

    if (m_renderPipeline.IsValid()) {
        cmd->BindGraphicsPipeline(m_renderPipeline);
        // Bind particle buffer, alive list, texture
        // Draw billboarded quads via indirect
        // cmd->DrawIndirect(m_drawArgsBuffer, 0, 1, sizeof(u32) * 5);
    }

    cmd->EndDebugLabel();
}

void ParticleEmitter::Burst(u32 count) {
    m_emitAccumulator += static_cast<f32>(count);
}

// ─── ParticleManager ─────────────────────────────────────────────────────

bool ParticleManager::Init(rhi::IDevice* device) {
    m_device = device;
    NGE_LOG_INFO("Particle manager initialized");
    return true;
}

void ParticleManager::Shutdown() {
    for (auto& emitter : m_emitters) {
        emitter.Shutdown();
    }
    m_emitters.clear();
    m_freeSlots.clear();
}

u32 ParticleManager::CreateEmitter(const ParticleEmitterDesc& desc) {
    u32 id;
    if (!m_freeSlots.empty()) {
        id = m_freeSlots.back();
        m_freeSlots.pop_back();
    } else {
        id = static_cast<u32>(m_emitters.size());
        m_emitters.emplace_back();
    }

    m_emitters[id].Init(m_device, desc);
    return id;
}

void ParticleManager::DestroyEmitter(u32 id) {
    if (id >= m_emitters.size()) return;
    m_emitters[id].Shutdown();
    m_freeSlots.push_back(id);
}

ParticleEmitter* ParticleManager::GetEmitter(u32 id) {
    if (id >= m_emitters.size()) return nullptr;
    return &m_emitters[id];
}

void ParticleManager::Update(rhi::ICommandList* cmd, f32 deltaTime,
                               const math::Vec3& cameraPos, const math::Mat4& viewProj) {
    for (auto& emitter : m_emitters) {
        emitter.Update(cmd, deltaTime, cameraPos, viewProj);
    }
}

void ParticleManager::Render(rhi::ICommandList* cmd, const math::Mat4& viewProj) {
    for (auto& emitter : m_emitters) {
        emitter.Render(cmd, viewProj);
    }
}

u32 ParticleManager::GetTotalAliveParticles() const {
    u32 total = 0;
    for (const auto& emitter : m_emitters) {
        total += emitter.GetAliveCount();
    }
    return total;
}

} // namespace nge::renderer
