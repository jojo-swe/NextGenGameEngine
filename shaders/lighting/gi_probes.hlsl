// ─── GI Probe Update Shaders ─────────────────────────────────────────────
// Two-pass compute: (1) Trace rays from probes, (2) Integrate into SH.
//
// Pass 1 (TraceProbeRays): For each probe, trace N rays into the scene
// via hardware RT or SDF. Store radiance + distance per ray.
//
// Pass 2 (UpdateProbeSH): For each updated probe, integrate ray results
// into L2 spherical harmonics with temporal hysteresis.

#include "../common/math.hlsl"

// ─── Probe Grid Info ─────────────────────────────────────────────────────

struct ProbeGridInfo {
    float3 origin;
    float  pad0;
    float3 spacing;
    float  pad1;
    uint   countX;
    uint   countY;
    uint   countZ;
    uint   totalProbes;
    float3 gridMin;
    float  pad2;
    float3 gridMax;
    float  pad3;
};

ConstantBuffer<ProbeGridInfo> g_Grid : register(b0, space6);

struct ProbeUpdateConstants {
    uint   probeOffset;      // First probe index to update this frame
    uint   probeCount;       // Number of probes to update
    uint   raysPerProbe;     // Rays traced per probe
    uint   frameIndex;
    float  hysteresis;       // Temporal blend (0.97)
    float  normalBias;       // Offset along normal to avoid self-hits
    float  maxRayDistance;
    float  pad;
};

[[vk::push_constant]] ConstantBuffer<ProbeUpdateConstants> pc;

// ─── SH Coefficients (L2 = 9 per channel) ───────────────────────────────

struct SH9Color {
    float shR[9];
    float shG[9];
    float shB[9];
    float pad[7]; // Align to 128 bytes
};

// SH basis evaluation
float SHBasis(uint i, float3 dir) {
    // Band 0
    if (i == 0) return 0.282095;
    // Band 1
    if (i == 1) return 0.488603 * dir.y;
    if (i == 2) return 0.488603 * dir.z;
    if (i == 3) return 0.488603 * dir.x;
    // Band 2
    if (i == 4) return 1.092548 * dir.x * dir.y;
    if (i == 5) return 1.092548 * dir.y * dir.z;
    if (i == 6) return 0.315392 * (3.0 * dir.z * dir.z - 1.0);
    if (i == 7) return 1.092548 * dir.x * dir.z;
    if (i == 8) return 0.546274 * (dir.x * dir.x - dir.y * dir.y);
    return 0;
}

// ─── Buffers ─────────────────────────────────────────────────────────────

RWStructuredBuffer<SH9Color> g_ProbeData : register(u0, space6);

struct RayResult {
    float3 radiance;
    float  distance;
};

RWStructuredBuffer<RayResult> g_RayResults : register(u1, space6);

// ─── Ray Direction Generation (Fibonacci sphere) ─────────────────────────

float3 FibonacciSphereDirection(uint index, uint totalRays, uint seed) {
    float goldenRatio = (1.0 + sqrt(5.0)) / 2.0;
    float theta = 2.0 * PI * frac(float(index) / goldenRatio + float(seed) * 0.618);
    float phi = acos(1.0 - 2.0 * (float(index) + 0.5) / float(totalRays));
    return float3(sin(phi) * cos(theta), cos(phi), sin(phi) * sin(theta));
}

// ─── Probe World Position ────────────────────────────────────────────────

float3 ProbeWorldPos(uint probeIndex) {
    uint ix = probeIndex % g_Grid.countX;
    uint iy = (probeIndex / g_Grid.countX) % g_Grid.countY;
    uint iz = probeIndex / (g_Grid.countX * g_Grid.countY);
    return g_Grid.origin + float3(float(ix), float(iy), float(iz)) * g_Grid.spacing;
}

// ─── Pass 1: Trace Rays ──────────────────────────────────────────────────
// One thread per ray. Traces into the scene and stores radiance + distance.

// RaytracingAccelerationStructure g_TLAS : register(t0, space6);

[numthreads(64, 1, 1)]
void TraceProbeRaysCS(uint3 DTid : SV_DispatchThreadID) {
    uint totalRays = pc.probeCount * pc.raysPerProbe;
    if (DTid.x >= totalRays) return;

    uint probeLocalIdx = DTid.x / pc.raysPerProbe;
    uint rayIdx = DTid.x % pc.raysPerProbe;
    uint probeGlobalIdx = pc.probeOffset + probeLocalIdx;

    if (probeGlobalIdx >= g_Grid.totalProbes) return;

    float3 probePos = ProbeWorldPos(probeGlobalIdx);
    float3 rayDir = FibonacciSphereDirection(rayIdx, pc.raysPerProbe, pc.frameIndex);

    // Trace ray
    // RayDesc ray;
    // ray.Origin = probePos;
    // ray.Direction = rayDir;
    // ray.TMin = pc.normalBias;
    // ray.TMax = pc.maxRayDistance;
    //
    // HitInfo hit = TraceRay(g_TLAS, ray, ...);

    // Stub: approximate with simple sky color based on direction
    float skyFactor = saturate(rayDir.y * 0.5 + 0.5);
    float3 skyColor = lerp(float3(0.15, 0.15, 0.2), float3(0.4, 0.6, 1.0), skyFactor);

    RayResult result;
    result.radiance = skyColor * 2.0; // Sun contribution approximation
    result.distance = pc.maxRayDistance;

    g_RayResults[DTid.x] = result;
}

// ─── Pass 2: Update Probe SH ─────────────────────────────────────────────
// One thread per probe. Integrates ray results into SH coefficients.

[numthreads(64, 1, 1)]
void UpdateProbeSHCS(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.probeCount) return;

    uint probeGlobalIdx = pc.probeOffset + DTid.x;
    if (probeGlobalIdx >= g_Grid.totalProbes) return;

    // Load current SH
    SH9Color currentSH = g_ProbeData[probeGlobalIdx];

    // Accumulate new SH from ray results
    float newR[9], newG[9], newB[9];
    for (uint i = 0; i < 9; ++i) {
        newR[i] = 0;
        newG[i] = 0;
        newB[i] = 0;
    }

    uint rayBase = DTid.x * pc.raysPerProbe;
    float weight = 4.0 * PI / float(pc.raysPerProbe); // Monte Carlo weight

    for (uint r = 0; r < pc.raysPerProbe; ++r) {
        RayResult ray = g_RayResults[rayBase + r];
        float3 dir = FibonacciSphereDirection(r, pc.raysPerProbe, pc.frameIndex);

        for (uint i = 0; i < 9; ++i) {
            float basis = SHBasis(i, dir);
            newR[i] += ray.radiance.r * basis * weight;
            newG[i] += ray.radiance.g * basis * weight;
            newB[i] += ray.radiance.b * basis * weight;
        }
    }

    // Temporal blend with hysteresis
    SH9Color result;
    for (uint i = 0; i < 9; ++i) {
        result.shR[i] = lerp(newR[i], currentSH.shR[i], pc.hysteresis);
        result.shG[i] = lerp(newG[i], currentSH.shG[i], pc.hysteresis);
        result.shB[i] = lerp(newB[i], currentSH.shB[i], pc.hysteresis);
    }
    for (uint i = 0; i < 7; ++i) result.pad[i] = 0;

    g_ProbeData[probeGlobalIdx] = result;
}

// ─── Probe Sampling (used by material resolve) ───────────────────────────
// Trilinear interpolation of 8 nearest probes.

float3 SampleGIProbes(float3 worldPos, float3 normal,
                       StructuredBuffer<SH9Color> probeData,
                       ProbeGridInfo grid) {
    // Compute grid coordinates
    float3 gridPos = (worldPos - grid.origin) / grid.spacing;
    int3 baseCoord = int3(floor(gridPos));
    float3 frac3 = gridPos - float3(baseCoord);

    // Clamp to grid bounds
    baseCoord = clamp(baseCoord, int3(0, 0, 0),
                      int3(grid.countX - 2, grid.countY - 2, grid.countZ - 2));

    float3 totalIrradiance = float3(0, 0, 0);
    float totalWeight = 0;

    // Trilinear interpolation over 8 neighbors
    for (uint i = 0; i < 8; ++i) {
        int3 offset = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        int3 coord = baseCoord + offset;

        uint probeIdx = coord.z * grid.countY * grid.countX +
                        coord.y * grid.countX + coord.x;

        // Trilinear weight
        float3 w3 = lerp(1.0 - frac3, frac3, float3(offset));
        float weight = w3.x * w3.y * w3.z;

        // Normal bias: reduce weight for probes behind the surface
        float3 probePos = grid.origin + float3(coord) * grid.spacing;
        float3 toProbe = normalize(probePos - worldPos);
        float normalWeight = saturate(dot(toProbe, normal) * 0.5 + 0.5);
        weight *= max(normalWeight, 0.05);

        if (weight < 0.001) continue;

        // Decode SH irradiance
        SH9Color sh = probeData[probeIdx];
        float3 irradiance;
        irradiance.r = 0;
        irradiance.g = 0;
        irradiance.b = 0;
        for (uint j = 0; j < 9; ++j) {
            float basis = SHBasis(j, normal);
            irradiance.r += sh.shR[j] * basis;
            irradiance.g += sh.shG[j] * basis;
            irradiance.b += sh.shB[j] * basis;
        }
        irradiance = max(irradiance, float3(0, 0, 0));

        totalIrradiance += irradiance * weight;
        totalWeight += weight;
    }

    if (totalWeight > 0.001) {
        totalIrradiance /= totalWeight;
    }

    return totalIrradiance;
}
