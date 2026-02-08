// ─── GPU Particle Simulation Compute Shader ──────────────────────────────
// Advances particle state: applies forces, integrates velocity,
// ages particles, kills expired ones, and computes color/size over lifetime.
//
// One thread per alive particle.

#include "../common/math.hlsl"

struct SimulateConstants {
    float3 gravity;
    float  deltaTime;
    float  drag;
    float  turbulenceStrength;
    float  turbulenceFreq;
    uint   maxParticles;
    uint   aliveCount;
    uint   frameIndex;
    float  pad0;
    float  pad1;
};

[[vk::push_constant]] ConstantBuffer<SimulateConstants> pc;

struct Particle {
    float3 position;
    float  age;
    float3 velocity;
    float  lifetime;
    float4 color;
    float  size;
    float  rotation;
    float  angularVelocity;
    uint   alive;
};

// Color-over-lifetime gradient (uploaded as structured buffer)
struct GradientStop {
    float  time;
    float4 color;
};

struct SizeCurveKey {
    float time;
    float value;
};

RWStructuredBuffer<Particle>    g_Particles  : register(u0, space18);
RWByteAddressBuffer             g_AliveList  : register(u1, space18);
RWByteAddressBuffer             g_DeadList   : register(u2, space18);
RWByteAddressBuffer             g_Counters   : register(u3, space18);
StructuredBuffer<GradientStop>  g_ColorGrad  : register(t0, space18);
StructuredBuffer<SizeCurveKey>  g_SizeCurve  : register(t1, space18);

// ─── 3D Curl Noise (for turbulence) ──────────────────────────────────────

float Hash31(float3 p) {
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float ValueNoise(float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);

    float v000 = Hash31(i + float3(0, 0, 0));
    float v100 = Hash31(i + float3(1, 0, 0));
    float v010 = Hash31(i + float3(0, 1, 0));
    float v110 = Hash31(i + float3(1, 1, 0));
    float v001 = Hash31(i + float3(0, 0, 1));
    float v101 = Hash31(i + float3(1, 0, 1));
    float v011 = Hash31(i + float3(0, 1, 1));
    float v111 = Hash31(i + float3(1, 1, 1));

    float v00 = lerp(v000, v100, f.x);
    float v10 = lerp(v010, v110, f.x);
    float v01 = lerp(v001, v101, f.x);
    float v11 = lerp(v011, v111, f.x);

    float v0 = lerp(v00, v10, f.y);
    float v1 = lerp(v01, v11, f.y);

    return lerp(v0, v1, f.z);
}

float3 CurlNoise(float3 p) {
    float eps = 0.01;
    float3 dx = float3(eps, 0, 0);
    float3 dy = float3(0, eps, 0);
    float3 dz = float3(0, 0, eps);

    float dnx_dy = ValueNoise(p + dy) - ValueNoise(p - dy);
    float dnx_dz = ValueNoise(p + dz) - ValueNoise(p - dz);
    float dny_dx = ValueNoise(p + dx) - ValueNoise(p - dx);
    float dny_dz = ValueNoise(p + dz) - ValueNoise(p - dz);
    float dnz_dx = ValueNoise(p + dx) - ValueNoise(p - dx);
    float dnz_dy = ValueNoise(p + dy) - ValueNoise(p - dy);

    return float3(dnx_dy - dnx_dz, dny_dz - dny_dx, dnz_dx - dnz_dy) / (2.0 * eps);
}

// ─── Main Simulation Kernel ──────────────────────────────────────────────

[numthreads(64, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.aliveCount) return;

    uint particleIdx = g_AliveList.Load(DTid.x * 4);
    if (particleIdx >= pc.maxParticles) return;

    Particle p = g_Particles[particleIdx];
    if (p.alive == 0) return;

    // Age
    p.age += pc.deltaTime;

    // Kill if expired
    if (p.age >= p.lifetime) {
        p.alive = 0;
        g_Particles[particleIdx] = p;

        // Push back to dead list
        uint deadIdx;
        g_Counters.InterlockedAdd(4, 1, deadIdx);
        g_DeadList.Store(deadIdx * 4, particleIdx);
        return;
    }

    float normalizedAge = p.age / p.lifetime; // 0..1

    // Apply gravity
    p.velocity += pc.gravity * pc.deltaTime;

    // Apply drag (exponential decay)
    p.velocity *= exp(-pc.drag * pc.deltaTime);

    // Apply turbulence (curl noise)
    if (pc.turbulenceStrength > 0.0) {
        float3 noisePos = p.position * pc.turbulenceFreq + float(pc.frameIndex) * 0.01;
        float3 turbForce = CurlNoise(noisePos) * pc.turbulenceStrength;
        p.velocity += turbForce * pc.deltaTime;
    }

    // Integrate position
    p.position += p.velocity * pc.deltaTime;

    // Rotation
    p.rotation += p.angularVelocity * pc.deltaTime;

    // Color over lifetime (sample gradient)
    // Simple 2-stop linear interpolation for now
    // TODO: Use g_ColorGrad structured buffer for arbitrary gradients
    float4 startColor = float4(1, 1, 1, 1);
    float4 endColor = float4(1, 1, 1, 0); // Fade out alpha
    p.color = lerp(startColor, endColor, normalizedAge);

    // Size over lifetime
    // TODO: Use g_SizeCurve structured buffer
    float sizeScale = 1.0 - normalizedAge * 0.5; // Shrink to 50% at death
    // p.size is initial size, modulated by sizeScale (stored separately or baked in)

    g_Particles[particleIdx] = p;
}

// ─── Compaction Pass ─────────────────────────────────────────────────────
// Rebuilds the alive list after simulation to remove dead particles.
// Also builds indirect draw arguments.

// RWByteAddressBuffer g_NewAliveList : register(u4, space18);
// RWByteAddressBuffer g_DrawArgs    : register(u5, space18);
//
// [numthreads(64, 1, 1)]
// void CompactCS(uint3 DTid : SV_DispatchThreadID) {
//     if (DTid.x >= pc.aliveCount) return;
//     uint idx = g_AliveList.Load(DTid.x * 4);
//     Particle p = g_Particles[idx];
//     if (p.alive) {
//         uint newIdx;
//         g_Counters.InterlockedAdd(8, 1, newIdx); // [2] = new alive count
//         g_NewAliveList.Store(newIdx * 4, idx);
//     }
// }
