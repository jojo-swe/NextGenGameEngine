// ─── Clustered Light Culling ──────────────────────────────────────────────
// Assigns lights to 3D clusters (froxels) for efficient forward+ shading.
// Each cluster is a frustum-aligned volume in view space.
//
// Pipeline:
//   1. Build cluster AABBs from depth range per tile
//   2. Cull lights against each cluster (sphere-AABB test)
//   3. Output per-cluster light list for shading passes

#include "../common/math.hlsl"

struct ClusterConstants {
    float4x4 invProj;
    float4x4 view;
    uint2    screenSize;
    uint2    clusterDims;    // xy = tile count, z encoded in clusterDims.y upper bits
    uint     clusterDepthSlices; // Z slices (e.g., 16-32)
    uint     lightCount;
    float    nearPlane;
    float    farPlane;
    float    logFarNear;     // log2(far/near) for exponential Z slicing
    uint     maxLightsPerCluster;
    uint     pad0;
    uint     pad1;
};

[[vk::push_constant]] ConstantBuffer<ClusterConstants> pc;

// ─── Light Data ──────────────────────────────────────────────────────────

struct GPULight {
    float3 position;     // View space
    float  radius;
    float3 color;
    float  intensity;
    float3 direction;    // For spot lights
    float  spotAngle;    // Cosine of half-angle
    uint   type;         // 0 = point, 1 = spot
    uint   pad0;
    uint   pad1;
    uint   pad2;
};

StructuredBuffer<GPULight> g_Lights : register(t0, space27);

// ─── Cluster AABB ────────────────────────────────────────────────────────

struct ClusterAABB {
    float3 minPoint;
    float3 maxPoint;
};

RWStructuredBuffer<ClusterAABB> g_ClusterAABBs : register(u0, space27);

// ─── Light Grid ──────────────────────────────────────────────────────────
// Per-cluster: offset into global light index list + count

struct LightGrid {
    uint offset;
    uint count;
};

RWStructuredBuffer<LightGrid> g_LightGrid      : register(u1, space27);
RWByteAddressBuffer           g_GlobalLightList : register(u2, space27); // Flat array of light indices
RWByteAddressBuffer           g_GlobalCounter   : register(u3, space27); // [0] = next offset

// ─── Utility ─────────────────────────────────────────────────────────────

float3 ScreenToView(float2 screenPos) {
    float2 ndc = screenPos / float2(pc.screenSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 viewPos = mul(pc.invProj, float4(ndc, 0.0, 1.0));
    return viewPos.xyz / viewPos.w;
}

float ViewDepthFromSlice(uint slice) {
    // Exponential depth slicing: depth = near * (far/near)^(slice/numSlices)
    float t = float(slice) / float(pc.clusterDepthSlices);
    return pc.nearPlane * exp2(t * pc.logFarNear);
}

uint3 GetClusterIndex3D(uint flatIndex) {
    uint tilesX = pc.clusterDims.x;
    uint tilesY = pc.clusterDims.y;
    uint z = flatIndex / (tilesX * tilesY);
    uint rem = flatIndex % (tilesX * tilesY);
    uint y = rem / tilesX;
    uint x = rem % tilesX;
    return uint3(x, y, z);
}

uint FlattenClusterIndex(uint3 idx) {
    return idx.z * pc.clusterDims.x * pc.clusterDims.y + idx.y * pc.clusterDims.x + idx.x;
}

// ─── Pass 1: Build Cluster AABBs ─────────────────────────────────────────

[numthreads(64, 1, 1)]
void CSBuildClusters(uint3 DTid : SV_DispatchThreadID) {
    uint totalClusters = pc.clusterDims.x * pc.clusterDims.y * pc.clusterDepthSlices;
    if (DTid.x >= totalClusters) return;

    uint3 idx = GetClusterIndex3D(DTid.x);

    // Tile screen bounds
    float tileW = float(pc.screenSize.x) / float(pc.clusterDims.x);
    float tileH = float(pc.screenSize.y) / float(pc.clusterDims.y);

    float2 screenMin = float2(float(idx.x) * tileW, float(idx.y) * tileH);
    float2 screenMax = float2(float(idx.x + 1) * tileW, float(idx.y + 1) * tileH);

    // View-space corners at near depth of this slice
    float nearZ = ViewDepthFromSlice(idx.z);
    float farZ = ViewDepthFromSlice(idx.z + 1);

    // Unproject screen corners to view space at near and far Z
    float3 viewMin = float3(1e10, 1e10, 1e10);
    float3 viewMax = float3(-1e10, -1e10, -1e10);

    // Sample 4 screen corners at both near and far Z
    float2 corners[4] = {screenMin, float2(screenMax.x, screenMin.y),
                          screenMax, float2(screenMin.x, screenMax.y)};

    for (uint i = 0; i < 4; ++i) {
        float3 rayDir = normalize(ScreenToView(corners[i]));

        // Points along the ray at near and far Z
        float3 pNear = rayDir * (nearZ / max(-rayDir.z, 0.0001));
        float3 pFar = rayDir * (farZ / max(-rayDir.z, 0.0001));

        viewMin = min(viewMin, min(pNear, pFar));
        viewMax = max(viewMax, max(pNear, pFar));
    }

    ClusterAABB aabb;
    aabb.minPoint = viewMin;
    aabb.maxPoint = viewMax;
    g_ClusterAABBs[DTid.x] = aabb;
}

// ─── Pass 2: Cull Lights Into Clusters ───────────────────────────────────

groupshared uint gs_LightCount;
groupshared uint gs_LightIndices[256]; // Max lights per cluster in shared mem

bool SphereAABBIntersect(float3 center, float radius, float3 aabbMin, float3 aabbMax) {
    float3 closest = clamp(center, aabbMin, aabbMax);
    float3 diff = center - closest;
    return dot(diff, diff) <= radius * radius;
}

[numthreads(64, 1, 1)]
void CSCullLights(uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex) {
    uint clusterIdx = Gid.x;
    uint totalClusters = pc.clusterDims.x * pc.clusterDims.y * pc.clusterDepthSlices;
    if (clusterIdx >= totalClusters) return;

    // Initialize shared memory
    if (GI == 0) gs_LightCount = 0;
    GroupMemoryBarrierWithGroupSync();

    ClusterAABB aabb = g_ClusterAABBs[clusterIdx];

    // Each thread tests a batch of lights
    uint lightsPerThread = (pc.lightCount + 63) / 64;
    uint lightStart = GI * lightsPerThread;
    uint lightEnd = min(lightStart + lightsPerThread, pc.lightCount);

    for (uint i = lightStart; i < lightEnd; ++i) {
        GPULight light = g_Lights[i];

        // Transform light to view space (already in view space in our data)
        bool intersects = SphereAABBIntersect(light.position, light.radius,
                                               aabb.minPoint, aabb.maxPoint);

        if (intersects) {
            uint idx;
            InterlockedAdd(gs_LightCount, 1, idx);
            if (idx < 256) {
                gs_LightIndices[idx] = i;
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Thread 0 writes results to global memory
    if (GI == 0) {
        uint count = min(gs_LightCount, pc.maxLightsPerCluster);

        // Allocate space in global light index list
        uint globalOffset;
        g_GlobalCounter.InterlockedAdd(0, count, globalOffset);

        LightGrid grid;
        grid.offset = globalOffset;
        grid.count = count;
        g_LightGrid[clusterIdx] = grid;

        // Copy light indices to global list
        for (uint i = 0; i < count; ++i) {
            g_GlobalLightList.Store((globalOffset + i) * 4, gs_LightIndices[i]);
        }
    }
}
