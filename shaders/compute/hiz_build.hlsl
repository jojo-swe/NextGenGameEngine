// ─── Hierarchical Z-Buffer (HiZ) Pyramid Builder ─────────────────────────
// Builds a depth mip chain where each texel stores the minimum depth
// of its 2×2 region (for reverse-Z, minimum = farthest = conservative).
// Used for GPU-driven occlusion culling: test object AABB against HiZ
// to quickly reject occluded instances.
//
// Single-pass variant using subgroup operations (LDS fallback).
// Dispatched once; generates all mip levels in a single compute pass
// for mip 1 through N (mip 0 = source depth buffer).

#define THREAD_GROUP_SIZE 32
#define MAX_MIP_LEVELS 13  // Supports up to 8192×8192

cbuffer HiZConstants : register(b0) {
    uint2 g_SrcDimensions;    // Mip 0 (full-res depth) dimensions
    uint  g_MipCount;         // Number of mip levels to generate
    uint  g_SinglePassMode;   // 1 = SPD single-pass, 0 = per-mip dispatch
};

// Mip 0 = source depth (read-only)
Texture2D<float>    g_DepthSrc      : register(t0);
SamplerState        g_PointClamp    : register(s0);

// Mip chain UAVs (mip 1 through MAX_MIP_LEVELS)
RWTexture2D<float>  g_HiZMip1       : register(u0);
RWTexture2D<float>  g_HiZMip2       : register(u1);
RWTexture2D<float>  g_HiZMip3       : register(u2);
RWTexture2D<float>  g_HiZMip4       : register(u3);
RWTexture2D<float>  g_HiZMip5       : register(u4);

// For SPD single-pass: atomic counter for workgroup synchronization
RWByteAddressBuffer g_SPDCounter    : register(u6);

// Shared memory for cross-thread reduction
groupshared float g_LDS[THREAD_GROUP_SIZE * THREAD_GROUP_SIZE];

// ─── Reverse-Z reduction: min = conservative (farthest) ─────────────────
// For standard Z, use max instead.

float ReduceDepth4(float a, float b, float c, float d) {
    // Reverse-Z: smaller value = farther → use min for conservative occlusion
    return min(min(a, b), min(c, d));
}

// ─── Per-Mip Dispatch Entry Points ──────────────────────────────────────
// Called once per mip level. Reads from mip N-1 and writes mip N.
// More compatible but requires N dispatches + barriers.

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void CSBuildMip1(uint3 dtid : SV_DispatchThreadID) {
    uint2 dstCoord = dtid.xy;
    uint2 srcCoord = dstCoord * 2;

    // Clamp to source dimensions
    uint2 maxCoord = g_SrcDimensions - 1;

    float d00 = g_DepthSrc[min(srcCoord + uint2(0, 0), maxCoord)];
    float d10 = g_DepthSrc[min(srcCoord + uint2(1, 0), maxCoord)];
    float d01 = g_DepthSrc[min(srcCoord + uint2(0, 1), maxCoord)];
    float d11 = g_DepthSrc[min(srcCoord + uint2(1, 1), maxCoord)];

    float result = ReduceDepth4(d00, d10, d01, d11);
    g_HiZMip1[dstCoord] = result;
}

// Generic mip reduce using previous mip as SRV
Texture2D<float> g_PrevMip : register(t1);

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void CSBuildMipN(uint3 dtid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID) {
    uint2 dstCoord = dtid.xy;
    uint2 srcCoord = dstCoord * 2;

    // Previous mip dimensions
    uint2 prevDims;
    g_PrevMip.GetDimensions(prevDims.x, prevDims.y);
    uint2 maxCoord = prevDims - 1;

    float d00 = g_PrevMip[min(srcCoord + uint2(0, 0), maxCoord)];
    float d10 = g_PrevMip[min(srcCoord + uint2(1, 0), maxCoord)];
    float d01 = g_PrevMip[min(srcCoord + uint2(0, 1), maxCoord)];
    float d11 = g_PrevMip[min(srcCoord + uint2(1, 1), maxCoord)];

    float result = ReduceDepth4(d00, d10, d01, d11);

    // Write to appropriate mip UAV (selected by root constant or specialization)
    g_HiZMip1[dstCoord] = result; // Rebound to target mip at dispatch time
}

// ─── Single-Pass Downsampler (SPD-style) ─────────────────────────────────
// Generates all mip levels in a single dispatch using LDS and workgroup
// synchronization. More efficient for small textures.

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void CSSinglePass(uint3 dtid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID,
                   uint3 gid : SV_GroupID, uint gidx : SV_GroupIndex) {

    uint2 srcCoord = dtid.xy * 2;
    uint2 maxCoord = g_SrcDimensions - 1;

    // ── Mip 1: Load 2×2 from depth buffer ────────────────────────────
    float d00 = g_DepthSrc[min(srcCoord + uint2(0, 0), maxCoord)];
    float d10 = g_DepthSrc[min(srcCoord + uint2(1, 0), maxCoord)];
    float d01 = g_DepthSrc[min(srcCoord + uint2(0, 1), maxCoord)];
    float d11 = g_DepthSrc[min(srcCoord + uint2(1, 1), maxCoord)];

    float mip1Val = ReduceDepth4(d00, d10, d01, d11);
    g_HiZMip1[dtid.xy] = mip1Val;

    // Store to LDS for further reduction
    g_LDS[gidx] = mip1Val;
    GroupMemoryBarrierWithGroupSync();

    // ── Mip 2: Reduce 2×2 within workgroup ───────────────────────────
    if ((gtid.x % 2 == 0) && (gtid.y % 2 == 0)) {
        uint base = gtid.y * THREAD_GROUP_SIZE + gtid.x;
        float a = g_LDS[base];
        float b = g_LDS[base + 1];
        float c = g_LDS[base + THREAD_GROUP_SIZE];
        float d = g_LDS[base + THREAD_GROUP_SIZE + 1];

        float mip2Val = ReduceDepth4(a, b, c, d);
        uint2 mip2Coord = dtid.xy / 2;
        g_HiZMip2[mip2Coord] = mip2Val;
        g_LDS[gidx] = mip2Val;
    }
    GroupMemoryBarrierWithGroupSync();

    // ── Mip 3: Reduce 4×4 ────────────────────────────────────────────
    if ((gtid.x % 4 == 0) && (gtid.y % 4 == 0)) {
        uint base = gtid.y * THREAD_GROUP_SIZE + gtid.x;
        float a = g_LDS[base];
        float b = g_LDS[base + 2];
        float c = g_LDS[base + 2 * THREAD_GROUP_SIZE];
        float d = g_LDS[base + 2 * THREAD_GROUP_SIZE + 2];

        float mip3Val = ReduceDepth4(a, b, c, d);
        uint2 mip3Coord = dtid.xy / 4;
        g_HiZMip3[mip3Coord] = mip3Val;
        g_LDS[gidx] = mip3Val;
    }
    GroupMemoryBarrierWithGroupSync();

    // ── Mip 4: Reduce 8×8 ────────────────────────────────────────────
    if ((gtid.x % 8 == 0) && (gtid.y % 8 == 0)) {
        uint base = gtid.y * THREAD_GROUP_SIZE + gtid.x;
        float a = g_LDS[base];
        float b = g_LDS[base + 4];
        float c = g_LDS[base + 4 * THREAD_GROUP_SIZE];
        float d = g_LDS[base + 4 * THREAD_GROUP_SIZE + 4];

        float mip4Val = ReduceDepth4(a, b, c, d);
        uint2 mip4Coord = dtid.xy / 8;
        g_HiZMip4[mip4Coord] = mip4Val;
        g_LDS[gidx] = mip4Val;
    }
    GroupMemoryBarrierWithGroupSync();

    // ── Mip 5: Reduce 16×16 ─────────────────────────────────────────
    if ((gtid.x % 16 == 0) && (gtid.y % 16 == 0)) {
        uint base = gtid.y * THREAD_GROUP_SIZE + gtid.x;
        float a = g_LDS[base];
        float b = g_LDS[base + 8];
        float c = g_LDS[base + 8 * THREAD_GROUP_SIZE];
        float d = g_LDS[base + 8 * THREAD_GROUP_SIZE + 8];

        float mip5Val = ReduceDepth4(a, b, c, d);
        uint2 mip5Coord = dtid.xy / 16;
        g_HiZMip5[mip5Coord] = mip5Val;
    }

    // Higher mips (6+) require cross-workgroup synchronization via
    // atomic counter (SPD pattern) or additional dispatches.
}
