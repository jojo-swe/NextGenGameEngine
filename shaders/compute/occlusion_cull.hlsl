// ─── Two-Pass GPU Occlusion Culling ──────────────────────────────────────
// Pass 1 (Early): Cull instances against previous frame's HZB.
//   - Tests bounding sphere/AABB against HZB mip pyramid.
//   - Outputs visible instance list + indirect draw args.
//
// Pass 2 (Late): After rasterizing Pass 1 survivors, rebuild HZB from
//   new depth, then re-test instances that were culled in Pass 1.
//   - Catches newly revealed geometry (disoccluded objects).
//
// This two-pass approach handles temporal disocclusion without
// pop-in artifacts that single-pass HZB culling exhibits.

#include "../common/math.hlsl"

struct CullConstants {
    float4x4 viewProj;
    float4x4 prevViewProj;
    float4   frustumPlanes[6];  // View frustum planes (nx, ny, nz, d)
    float4   cameraPos;
    uint2    hzbSize;           // HZB mip 0 dimensions
    uint     hzbMipCount;
    uint     instanceCount;
    uint     isLatePass;        // 0 = early (prev HZB), 1 = late (current HZB)
    float    nearPlane;
    uint     pad0;
    uint     pad1;
};

[[vk::push_constant]] ConstantBuffer<CullConstants> pc;

// ─── Instance Data ───────────────────────────────────────────────────────

struct InstanceData {
    float4x4 worldMatrix;
    float4   boundingSphere; // xyz = center, w = radius
    float4   aabbMin;        // World-space AABB min
    float4   aabbMax;        // World-space AABB max
    uint     meshletOffset;
    uint     meshletCount;
    uint     materialIndex;
    uint     flags;          // Bit 0: was visible last frame
};

struct DrawCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

StructuredBuffer<InstanceData>  g_Instances       : register(t0, space20);
Texture2D<float>                g_HZB             : register(t1, space20);

RWStructuredBuffer<uint>        g_VisibleList     : register(u0, space20); // Output: visible instance indices
RWByteAddressBuffer             g_Counters        : register(u1, space20); // [0] = visible count, [1] = draw count
RWStructuredBuffer<DrawCommand> g_DrawCommands    : register(u2, space20);
RWStructuredBuffer<uint>        g_OccludedList    : register(u3, space20); // Pass 1 → Pass 2: deferred re-test
RWByteAddressBuffer             g_OccludedCounter : register(u4, space20);

SamplerState g_PointClamp : register(s0, space20);

// ─── Frustum Culling ─────────────────────────────────────────────────────

bool FrustumCullSphere(float3 center, float radius) {
    [unroll]
    for (uint i = 0; i < 6; ++i) {
        float dist = dot(pc.frustumPlanes[i].xyz, center) + pc.frustumPlanes[i].w;
        if (dist < -radius) return true; // Outside
    }
    return false; // Inside or intersecting
}

// ─── HZB Occlusion Test ─────────────────────────────────────────────────

bool HZBOcclusionTest(float3 aabbMin, float3 aabbMax) {
    // Project AABB corners to screen space
    float2 screenMin = float2(1e10, 1e10);
    float2 screenMax = float2(-1e10, -1e10);
    float minDepth = 1e10;
    bool behindCamera = false;

    [unroll]
    for (uint i = 0; i < 8; ++i) {
        float3 corner = float3(
            (i & 1) ? aabbMax.x : aabbMin.x,
            (i & 2) ? aabbMax.y : aabbMin.y,
            (i & 4) ? aabbMax.z : aabbMin.z
        );

        float4 clip = mul(pc.viewProj, float4(corner, 1.0));

        if (clip.w <= 0.0) {
            behindCamera = true;
            continue;
        }

        float3 ndc = clip.xyz / clip.w;
        float2 screen = ndc.xy * 0.5 + 0.5;
        screen.y = 1.0 - screen.y; // Flip Y for UV space

        screenMin = min(screenMin, screen);
        screenMax = max(screenMax, screen);
        minDepth = min(minDepth, ndc.z);
    }

    // If any corner is behind camera, conservatively mark as visible
    if (behindCamera) return false;

    // Clamp to screen
    screenMin = clamp(screenMin, 0.0, 1.0);
    screenMax = clamp(screenMax, 0.0, 1.0);

    // Degenerate projection
    if (screenMin.x >= screenMax.x || screenMin.y >= screenMax.y) return true;

    // Choose HZB mip level based on projected screen area
    float2 screenExtent = (screenMax - screenMin) * float2(pc.hzbSize);
    float maxExtent = max(screenExtent.x, screenExtent.y);
    uint mipLevel = uint(ceil(log2(max(maxExtent, 1.0))));
    mipLevel = min(mipLevel, pc.hzbMipCount - 1);

    // Sample HZB at the chosen mip (conservative: sample 4 corners)
    float2 mipSize = float2(pc.hzbSize) / float(1u << mipLevel);
    float2 uvMin = screenMin;
    float2 uvMax = screenMax;

    float hzbDepth = 0;
    hzbDepth = max(hzbDepth, g_HZB.SampleLevel(g_PointClamp, float2(uvMin.x, uvMin.y), float(mipLevel)));
    hzbDepth = max(hzbDepth, g_HZB.SampleLevel(g_PointClamp, float2(uvMax.x, uvMin.y), float(mipLevel)));
    hzbDepth = max(hzbDepth, g_HZB.SampleLevel(g_PointClamp, float2(uvMin.x, uvMax.y), float(mipLevel)));
    hzbDepth = max(hzbDepth, g_HZB.SampleLevel(g_PointClamp, float2(uvMax.x, uvMax.y), float(mipLevel)));

    // Reverse-Z: object is occluded if its closest depth is LESS than the HZB max depth
    // (In reverse-Z, closer objects have larger depth values)
    return minDepth < hzbDepth;
}

// ─── Main Culling Kernel ─────────────────────────────────────────────────

[numthreads(64, 1, 1)]
void CSEarlyCull(uint3 DTid : SV_DispatchThreadID) {
    uint instanceIdx = DTid.x;
    if (instanceIdx >= pc.instanceCount) return;

    InstanceData inst = g_Instances[instanceIdx];

    // Frustum cull
    if (FrustumCullSphere(inst.boundingSphere.xyz, inst.boundingSphere.w)) {
        return; // Outside frustum — definitively culled
    }

    // HZB occlusion test against previous frame's HZB
    bool occluded = HZBOcclusionTest(inst.aabbMin.xyz, inst.aabbMax.xyz);

    if (!occluded) {
        // Visible: add to visible list
        uint visIdx;
        g_Counters.InterlockedAdd(0, 1, visIdx);
        g_VisibleList[visIdx] = instanceIdx;
    } else {
        // Occluded: defer to late pass for re-test with updated HZB
        uint occIdx;
        g_OccludedCounter.InterlockedAdd(0, 1, occIdx);
        g_OccludedList[occIdx] = instanceIdx;
    }
}

// ─── Late Pass: Re-test Occluded Instances ───────────────────────────────
// Uses the current frame's HZB (built from Pass 1 survivors).

[numthreads(64, 1, 1)]
void CSLateCull(uint3 DTid : SV_DispatchThreadID) {
    uint occludedCount = g_OccludedCounter.Load(0);
    if (DTid.x >= occludedCount) return;

    uint instanceIdx = g_OccludedList[DTid.x];
    InstanceData inst = g_Instances[instanceIdx];

    // Re-test against current frame HZB
    bool stillOccluded = HZBOcclusionTest(inst.aabbMin.xyz, inst.aabbMax.xyz);

    if (!stillOccluded) {
        // Now visible — add to visible list for late rasterization
        uint visIdx;
        g_Counters.InterlockedAdd(0, 1, visIdx);
        g_VisibleList[visIdx] = instanceIdx;
    }
}

// ─── Build Indirect Draw Arguments ───────────────────────────────────────

[numthreads(1, 1, 1)]
void CSBuildDrawArgs(uint3 DTid : SV_DispatchThreadID) {
    uint visibleCount = g_Counters.Load(0);

    DrawCommand cmd;
    cmd.indexCount = 0;     // Filled per-meshlet or per-mesh
    cmd.instanceCount = visibleCount;
    cmd.firstIndex = 0;
    cmd.vertexOffset = 0;
    cmd.firstInstance = 0;
    g_DrawCommands[0] = cmd;
}
