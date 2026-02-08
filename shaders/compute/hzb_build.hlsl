// ─── Hierarchical Z-Buffer (HZB) Build ───────────────────────────────────
// Downsamples the depth buffer into a mip chain where each texel stores
// the MAXIMUM depth of the 2×2 region from the previous level.
// Used for GPU-driven occlusion culling.
//
// Dispatch: one pass per mip level, or single-pass with wave intrinsics.

#include "../common/math.hlsl"

struct HZBConstants {
    uint2 srcResolution;    // Previous mip resolution
    uint2 dstResolution;    // Current mip resolution
    uint  srcMipLevel;
    uint  dstMipLevel;
    uint  isFirstMip;       // 1 = reading from depth buffer, 0 = reading from HZB
    uint  pad;
};

[[vk::push_constant]] ConstantBuffer<HZBConstants> pc;

Texture2D<float>   g_DepthBuffer : register(t0, space4);
Texture2D<float>   g_HZBInput   : register(t1, space4);
RWTexture2D<float> g_HZBOutput  : register(u0, space4);
SamplerState       g_PointClamp : register(s0, space4);

// Single-mip downsample: take max of 2×2 region
[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.dstResolution.x || DTid.y >= pc.dstResolution.y) return;

    float2 texelSize;
    float maxDepth;

    if (pc.isFirstMip) {
        // First mip: read from depth buffer
        texelSize = 1.0 / float2(pc.srcResolution);
        float2 uv = (float2(DTid.xy) * 2.0 + 0.5) * texelSize;

        // Sample 4 depth values
        float d00 = g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0);
        float d10 = g_DepthBuffer.SampleLevel(g_PointClamp, uv + float2(texelSize.x, 0), 0);
        float d01 = g_DepthBuffer.SampleLevel(g_PointClamp, uv + float2(0, texelSize.y), 0);
        float d11 = g_DepthBuffer.SampleLevel(g_PointClamp, uv + texelSize, 0);

        // Reverse-Z: closer = larger values, so MAX finds the closest depth
        // For occlusion culling with reverse-Z, we want the closest (max) depth
        maxDepth = max(max(d00, d10), max(d01, d11));
    } else {
        // Subsequent mips: read from previous HZB mip
        texelSize = 1.0 / float2(pc.srcResolution);
        float2 uv = (float2(DTid.xy) * 2.0 + 0.5) * texelSize;

        float d00 = g_HZBInput.SampleLevel(g_PointClamp, uv, 0);
        float d10 = g_HZBInput.SampleLevel(g_PointClamp, uv + float2(texelSize.x, 0), 0);
        float d01 = g_HZBInput.SampleLevel(g_PointClamp, uv + float2(0, texelSize.y), 0);
        float d11 = g_HZBInput.SampleLevel(g_PointClamp, uv + texelSize, 0);

        maxDepth = max(max(d00, d10), max(d01, d11));
    }

    g_HZBOutput[DTid.xy] = maxDepth;
}

// ─── Single-Pass HZB with Wave Intrinsics (SM 6.0+) ─────────────────────
// Builds multiple mip levels in a single dispatch using subgroup operations.
// More efficient but requires SM 6.0+ / Vulkan subgroup support.

// groupshared float gs_Depth[64]; // 8×8 tile

// [numthreads(8, 8, 1)]
// void CSSinglePass(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex) {
//     // Load 4 texels per thread (covers 16×16 region)
//     // Reduce in shared memory to build mips 0-3 in one pass
//     // Write each mip level to corresponding UAV slice
// }
