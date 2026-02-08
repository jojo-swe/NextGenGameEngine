// ─── GPU Frustum Culling Compute Shader ───────────────────────────────────
// Per-instance AABB frustum test with optional distance culling.
// Outputs a compacted visible instance list for indirect drawing.
//
// Input:  InstanceData[] — world AABB min/max per instance
// Output: VisibleIndices[] — compacted list of visible instance IDs
//         DrawCount — atomic counter of visible instances

#include "../common/math.hlsl"

struct CullConstants {
    float4   frustumPlanes[6]; // xyz = normal, w = distance
    float4   cameraPos;
    float    maxDrawDistance;   // Squared distance culling threshold
    uint     instanceCount;
    uint     pad0;
    uint     pad1;
};

[[vk::push_constant]] ConstantBuffer<CullConstants> pc;

struct InstanceData {
    float3 aabbMin;
    uint   meshIndex;
    float3 aabbMax;
    uint   materialIndex;
    float4x4 worldMatrix;
};

StructuredBuffer<InstanceData>  g_Instances      : register(t0, space50);
RWStructuredBuffer<uint>        g_VisibleIndices : register(u0, space50);
RWByteAddressBuffer             g_DrawCount      : register(u1, space50);

// ─── AABB vs Plane Test ──────────────────────────────────────────────────

bool AABBOutsidePlane(float3 aabbMin, float3 aabbMax, float4 plane) {
    // Find the positive vertex (furthest along plane normal)
    float3 pVertex;
    pVertex.x = (plane.x >= 0) ? aabbMax.x : aabbMin.x;
    pVertex.y = (plane.y >= 0) ? aabbMax.y : aabbMin.y;
    pVertex.z = (plane.z >= 0) ? aabbMax.z : aabbMin.z;

    return dot(plane.xyz, pVertex) + plane.w < 0;
}

// ─── Distance Culling ────────────────────────────────────────────────────

bool BeyondDrawDistance(float3 aabbMin, float3 aabbMax) {
    if (pc.maxDrawDistance <= 0) return false;

    float3 center = (aabbMin + aabbMax) * 0.5;
    float3 diff = center - pc.cameraPos.xyz;
    float distSq = dot(diff, diff);
    return distSq > pc.maxDrawDistance * pc.maxDrawDistance;
}

// ─── Main ────────────────────────────────────────────────────────────────

[numthreads(64, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    uint instanceIdx = DTid.x;
    if (instanceIdx >= pc.instanceCount) return;

    InstanceData inst = g_Instances[instanceIdx];

    // Transform AABB to world space (conservative — use world matrix rows)
    // For axis-aligned world transforms, we can use the AABB directly.
    // For rotated transforms, compute world AABB from OBB corners.
    float3 worldMin = inst.aabbMin;
    float3 worldMax = inst.aabbMax;

    // If instance has a non-identity world matrix, compute world AABB
    float3 center = (inst.aabbMin + inst.aabbMax) * 0.5;
    float3 extent = (inst.aabbMax - inst.aabbMin) * 0.5;

    float3 worldCenter = mul(inst.worldMatrix, float4(center, 1.0)).xyz;

    // Compute world-space extents from absolute values of rotation columns
    float3 worldExtent;
    worldExtent.x = abs(inst.worldMatrix[0][0]) * extent.x +
                    abs(inst.worldMatrix[1][0]) * extent.y +
                    abs(inst.worldMatrix[2][0]) * extent.z;
    worldExtent.y = abs(inst.worldMatrix[0][1]) * extent.x +
                    abs(inst.worldMatrix[1][1]) * extent.y +
                    abs(inst.worldMatrix[2][1]) * extent.z;
    worldExtent.z = abs(inst.worldMatrix[0][2]) * extent.x +
                    abs(inst.worldMatrix[1][2]) * extent.y +
                    abs(inst.worldMatrix[2][2]) * extent.z;

    worldMin = worldCenter - worldExtent;
    worldMax = worldCenter + worldExtent;

    // Frustum culling — test against all 6 planes
    bool visible = true;
    [unroll]
    for (uint p = 0; p < 6; ++p) {
        if (AABBOutsidePlane(worldMin, worldMax, pc.frustumPlanes[p])) {
            visible = false;
            break;
        }
    }

    // Distance culling
    if (visible && BeyondDrawDistance(worldMin, worldMax)) {
        visible = false;
    }

    // Compact visible instances
    if (visible) {
        uint slot;
        g_DrawCount.InterlockedAdd(0, 1, slot);
        g_VisibleIndices[slot] = instanceIdx;
    }
}

// ─── Reset Counter ───────────────────────────────────────────────────────

[numthreads(1, 1, 1)]
void CSResetCounter(uint3 DTid : SV_DispatchThreadID) {
    g_DrawCount.Store(0, 0);
}
