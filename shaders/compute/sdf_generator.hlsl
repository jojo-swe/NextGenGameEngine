// ─── Signed Distance Field Generator ─────────────────────────────────────
// GPU compute shader for generating 3D SDF volumes from triangle meshes.
// Used for soft shadows, ambient occlusion, global illumination probes,
// and real-time font rendering.
//
// Approaches:
//   1. Brute-force: per-voxel closest triangle distance (accurate, slow)
//   2. Jump Flooding Algorithm (JFA): fast approximate SDF from seed points
//   3. Sweep-based: propagate distances along axes
//
// This implementation uses JFA for speed with optional refinement pass.
//
// References:
//   - "Jump Flooding in GPU" (Rong & Tan, I3D 2006)
//   - "Generating SDF Volumes" (Quilez, 2015)
//   - "Real-Time SDF Soft Shadows" (Wright, GDC 2019)

// ─── Buffers ─────────────────────────────────────────────────────────────

RWTexture3D<float>  g_SDFVolume    : register(u0); // Output SDF (negative = inside)
RWTexture3D<float4> g_SeedBuffer   : register(u1); // JFA seed positions (xyz=pos, w=dist)
Texture3D<float4>   g_SeedInput    : register(t0); // Previous JFA iteration
StructuredBuffer<float3> g_Vertices : register(t1); // Mesh vertices
StructuredBuffer<uint3>  g_Indices  : register(t2); // Mesh triangle indices

SamplerState g_PointClamp : register(s0);

struct SDFConstants {
    float3 volumeMin;       // AABB min of SDF volume (world space)
    float  voxelSize;       // Size of each voxel
    float3 volumeMax;       // AABB max
    uint   volumeResolution;// Typically 64, 128, or 256
    uint   triangleCount;
    uint   jfaStep;         // Current JFA step size (starts at resolution/2)
    uint   passType;        // 0=seed, 1=JFA step, 2=finalize, 3=sign determination
    float  padding;
};

[[vk::push_constant]] ConstantBuffer<SDFConstants> cb;

// ─── Utility ─────────────────────────────────────────────────────────────

float3 VoxelToWorld(uint3 voxel) {
    float3 t = (float3(voxel) + 0.5) / float(cb.volumeResolution);
    return lerp(cb.volumeMin, cb.volumeMax, t);
}

uint3 WorldToVoxel(float3 worldPos) {
    float3 t = (worldPos - cb.volumeMin) / (cb.volumeMax - cb.volumeMin);
    return uint3(clamp(t * float(cb.volumeResolution), 0, float(cb.volumeResolution - 1)));
}

// ─── Triangle Distance ───────────────────────────────────────────────────
// Closest distance from point to triangle (Quilez method).

float TriangleDistance(float3 p, float3 a, float3 b, float3 c) {
    float3 ba = b - a; float3 pa = p - a;
    float3 cb = c - b; float3 pb = p - b;
    float3 ac = a - c; float3 pc = p - c;
    float3 nor = cross(ba, ac);

    float sign_check = sign(dot(cross(ba, nor), pa)) +
                       sign(dot(cross(cb, nor), pb)) +
                       sign(dot(cross(ac, nor), pc));

    if (sign_check < 2.0) {
        // Inside triangle projection
        return sqrt(dot(nor, pa) * dot(nor, pa) / dot(nor, nor));
    }

    // Closest to edge
    float3 d1 = ba * clamp(dot(ba, pa) / dot(ba, ba), 0.0, 1.0) - pa;
    float3 d2 = cb * clamp(dot(cb, pb) / dot(cb, cb), 0.0, 1.0) - pb;
    float3 d3 = ac * clamp(dot(ac, pc) / dot(ac, ac), 0.0, 1.0) - pc;

    return sqrt(min(min(dot(d1, d1), dot(d2, d2)), dot(d3, d3)));
}

// ─── Pass 0: Seed Initialization ─────────────────────────────────────────
// For each triangle, rasterize it into the 3D grid as seed points.

[numthreads(64, 1, 1)]
void CSSeedInit(uint3 DTid : SV_DispatchThreadID) {
    uint triIdx = DTid.x;
    if (triIdx >= cb.triangleCount) return;

    uint3 idx = g_Indices[triIdx];
    float3 v0 = g_Vertices[idx.x];
    float3 v1 = g_Vertices[idx.y];
    float3 v2 = g_Vertices[idx.z];

    // Rasterize triangle center into voxel grid
    float3 center = (v0 + v1 + v2) / 3.0;
    uint3 voxel = WorldToVoxel(center);

    if (all(voxel < uint3(cb.volumeResolution, cb.volumeResolution, cb.volumeResolution))) {
        g_SeedBuffer[voxel] = float4(center, 0.0); // Distance 0 at seed
    }

    // Also rasterize edge midpoints for better coverage
    float3 mid01 = (v0 + v1) * 0.5;
    float3 mid12 = (v1 + v2) * 0.5;
    float3 mid20 = (v2 + v0) * 0.5;

    uint3 vm01 = WorldToVoxel(mid01);
    uint3 vm12 = WorldToVoxel(mid12);
    uint3 vm20 = WorldToVoxel(mid20);

    uint res = cb.volumeResolution;
    if (all(vm01 < uint3(res, res, res))) g_SeedBuffer[vm01] = float4(mid01, 0.0);
    if (all(vm12 < uint3(res, res, res))) g_SeedBuffer[vm12] = float4(mid12, 0.0);
    if (all(vm20 < uint3(res, res, res))) g_SeedBuffer[vm20] = float4(mid20, 0.0);
}

// ─── Pass 1: Jump Flooding Algorithm Step ────────────────────────────────
// Propagate closest seed information with decreasing step sizes.

[numthreads(4, 4, 4)]
void CSJFAStep(uint3 DTid : SV_DispatchThreadID) {
    uint res = cb.volumeResolution;
    if (any(DTid >= uint3(res, res, res))) return;

    float3 worldPos = VoxelToWorld(DTid);
    float4 bestSeed = g_SeedInput.Load(int4(DTid, 0));
    float bestDist = bestSeed.w;

    if (bestSeed.x == 0 && bestSeed.y == 0 && bestSeed.z == 0 && bestSeed.w == 0) {
        bestDist = 1e10;
    }

    int step = int(cb.jfaStep);

    // Check 26 neighbors at current step distance
    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                if (x == 0 && y == 0 && z == 0) continue;

                int3 neighbor = int3(DTid) + int3(x, y, z) * step;
                if (any(neighbor < 0) || any(neighbor >= int3(res, res, res))) continue;

                float4 neighborSeed = g_SeedInput.Load(int4(neighbor, 0));
                if (neighborSeed.x == 0 && neighborSeed.y == 0 &&
                    neighborSeed.z == 0 && neighborSeed.w == 0) continue;

                float dist = length(worldPos - neighborSeed.xyz);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestSeed = float4(neighborSeed.xyz, dist);
                }
            }
        }
    }

    g_SeedBuffer[DTid] = bestSeed;
}

// ─── Pass 2: Finalize Distance ───────────────────────────────────────────
// Write final unsigned distance from JFA result.

[numthreads(4, 4, 4)]
void CSFinalize(uint3 DTid : SV_DispatchThreadID) {
    uint res = cb.volumeResolution;
    if (any(DTid >= uint3(res, res, res))) return;

    float4 seed = g_SeedInput.Load(int4(DTid, 0));
    float3 worldPos = VoxelToWorld(DTid);

    float dist = length(worldPos - seed.xyz);
    if (seed.x == 0 && seed.y == 0 && seed.z == 0 && seed.w == 0) {
        dist = cb.voxelSize * float(res); // Max distance
    }

    g_SDFVolume[DTid] = dist;
}

// ─── Pass 3: Sign Determination ──────────────────────────────────────────
// Determine inside/outside using ray casting (parity test).
// Casts a ray along +X axis and counts triangle intersections.

[numthreads(4, 4, 4)]
void CSSignDetermination(uint3 DTid : SV_DispatchThreadID) {
    uint res = cb.volumeResolution;
    if (any(DTid >= uint3(res, res, res))) return;

    float3 worldPos = VoxelToWorld(DTid);
    uint intersections = 0;

    // Cast ray along +X
    float3 rayOrigin = worldPos;
    float3 rayDir = float3(1, 0, 0);

    for (uint i = 0; i < cb.triangleCount; ++i) {
        uint3 idx = g_Indices[i];
        float3 v0 = g_Vertices[idx.x];
        float3 v1 = g_Vertices[idx.y];
        float3 v2 = g_Vertices[idx.z];

        // Moller-Trumbore ray-triangle intersection
        float3 e1 = v1 - v0;
        float3 e2 = v2 - v0;
        float3 h = cross(rayDir, e2);
        float a = dot(e1, h);

        if (abs(a) < 1e-8) continue;

        float f = 1.0 / a;
        float3 s = rayOrigin - v0;
        float u = f * dot(s, h);
        if (u < 0.0 || u > 1.0) continue;

        float3 q = cross(s, e1);
        float v = f * dot(rayDir, q);
        if (v < 0.0 || u + v > 1.0) continue;

        float t = f * dot(e2, q);
        if (t > 0.001) intersections++;
    }

    float dist = g_SDFVolume[DTid];

    // Odd intersections = inside (negative distance)
    if (intersections % 2 == 1) {
        dist = -dist;
    }

    g_SDFVolume[DTid] = dist;
}

// ─── Brute Force Distance (Quality Pass) ─────────────────────────────────
// Optional refinement: compute exact distance to closest triangle.
// Only used for small meshes or final quality pass.

[numthreads(4, 4, 4)]
void CSBruteForceDistance(uint3 DTid : SV_DispatchThreadID) {
    uint res = cb.volumeResolution;
    if (any(DTid >= uint3(res, res, res))) return;

    float3 worldPos = VoxelToWorld(DTid);
    float minDist = 1e10;

    for (uint i = 0; i < cb.triangleCount; ++i) {
        uint3 idx = g_Indices[i];
        float3 v0 = g_Vertices[idx.x];
        float3 v1 = g_Vertices[idx.y];
        float3 v2 = g_Vertices[idx.z];

        float d = TriangleDistance(worldPos, v0, v1, v2);
        minDist = min(minDist, d);
    }

    // Preserve sign from previous pass
    float currentSign = sign(g_SDFVolume[DTid]);
    if (currentSign == 0) currentSign = 1.0;

    g_SDFVolume[DTid] = minDist * currentSign;
}
