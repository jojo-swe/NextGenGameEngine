// ─── GPU-Driven Instance + Meshlet Culling ───────────────────────────────
// Compute shader that performs:
//   1. Frustum culling per instance (bounding sphere)
//   2. Occlusion culling via HZB (Hierarchical Z-Buffer)
//   3. Outputs visible instances to indirect draw buffer
//
// Phase 2 adds meshlet-level culling with backface cone test.

#include "../common/math.hlsl"
#include "../common/bindless.hlsl"

struct InstanceData {
    float4x4 transform;
    float4   boundingSphere; // xyz = center, w = radius
    uint     meshletOffset;
    uint     meshletCount;
    uint     materialId;
    uint     flags;
};

struct DrawCommand {
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
};

// Push constants contain view-projection, frustum planes, HZB dimensions
struct CullingPushConstants {
    float4x4 viewProj;
    float4   frustumPlanes[6]; // ax + by + cz + d = 0
    float4   cameraPos;
    uint     instanceCount;
    uint     hzbWidth;
    uint     hzbHeight;
    uint     hzbMips;
};

[[vk::push_constant]] ConstantBuffer<CullingPushConstants> cull_pc;

// Buffers
StructuredBuffer<InstanceData>  g_Instances     : register(t0, space3);
RWStructuredBuffer<uint>        g_VisibleList   : register(u0, space3);
RWStructuredBuffer<DrawCommand> g_DrawCommands  : register(u1, space3);
RWByteAddressBuffer             g_VisibleCount  : register(u2, space3);

Texture2D<float> g_HZB : register(t1, space3);
SamplerState g_HZBSampler : register(s0, space3);

// ─── Frustum Test ────────────────────────────────────────────────────────
bool FrustumCull(float3 center, float radius) {
    [unroll]
    for (uint i = 0; i < 6; ++i) {
        float dist = dot(cull_pc.frustumPlanes[i].xyz, center) + cull_pc.frustumPlanes[i].w;
        if (dist < -radius) return true; // Outside this plane
    }
    return false; // Inside all planes
}

// ─── HZB Occlusion Test ──────────────────────────────────────────────────
bool OcclusionCull(float3 center, float radius) {
    // Project bounding sphere to screen-space AABB
    float4 clipPos = mul(cull_pc.viewProj, float4(center, 1.0));
    if (clipPos.w <= 0.0) return false; // Behind camera, don't cull (might wrap)

    float3 ndc = clipPos.xyz / clipPos.w;
    float screenRadius = radius / clipPos.w; // Approximate screen-space radius

    // Compute screen-space AABB
    float2 minUV = saturate(float2(ndc.x - screenRadius, ndc.y - screenRadius) * 0.5 + 0.5);
    float2 maxUV = saturate(float2(ndc.x + screenRadius, ndc.y + screenRadius) * 0.5 + 0.5);

    // Select HZB mip level based on AABB size
    float2 aabbSize = (maxUV - minUV) * float2(cull_pc.hzbWidth, cull_pc.hzbHeight);
    float mipLevel = ceil(log2(max(aabbSize.x, aabbSize.y)));
    mipLevel = clamp(mipLevel, 0, cull_pc.hzbMips - 1);

    // Sample HZB at AABB center
    float2 centerUV = (minUV + maxUV) * 0.5;
    float hzbDepth = g_HZB.SampleLevel(g_HZBSampler, centerUV, mipLevel);

    // Compare: if the closest depth in the HZB is farther than our sphere's near depth
    float sphereNearDepth = ndc.z - screenRadius;
    return sphereNearDepth > hzbDepth; // Occluded
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint instanceIdx = DTid.x;
    if (instanceIdx >= cull_pc.instanceCount) return;

    InstanceData inst = g_Instances[instanceIdx];

    // Transform bounding sphere to world space
    float3 worldCenter = mul(inst.transform, float4(inst.boundingSphere.xyz, 1.0)).xyz;
    float worldRadius = inst.boundingSphere.w; // Assumes uniform scale

    // Test frustum
    if (FrustumCull(worldCenter, worldRadius)) return;

    // Test occlusion (skip if HZB not available)
    if (cull_pc.hzbWidth > 0 && OcclusionCull(worldCenter, worldRadius)) return;

    // Instance is visible — append to visible list
    uint outIdx;
    g_VisibleCount.InterlockedAdd(0, 1, outIdx);
    g_VisibleList[outIdx] = instanceIdx;
}
