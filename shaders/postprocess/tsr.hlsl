// ─── Temporal Super Resolution (Custom TSR) ──────────────────────────────
// Renders at lower resolution (50-67% of target), upscales to native.
//
// Pipeline:
//   1. Render at reduced resolution with Halton jitter
//   2. Reproject history using motion vectors
//   3. Neighborhood clamping to prevent ghosting
//   4. Blend current + history with adaptive weight
//   5. Sharpening pass (CAS or RCAS)

#include "../common/math.hlsl"

Texture2D<float4>   g_CurrentFrame  : register(t0); // Low-res jittered render
Texture2D<float4>   g_HistoryFrame  : register(t1); // Previous upscaled frame
Texture2D<float2>   g_MotionVectors : register(t2); // Screen-space motion
Texture2D<float>    g_Depth         : register(t3); // Current depth

RWTexture2D<float4> g_Output        : register(u0); // Upscaled output (also becomes next history)

SamplerState g_LinearClamp  : register(s0);
SamplerState g_PointClamp   : register(s1);

struct TSRPushConstants {
    float2 texelSize;        // 1.0 / targetResolution
    float2 renderTexelSize;  // 1.0 / renderResolution
    float2 renderScale;      // renderRes / targetRes (e.g. 0.667)
    float  blendFactor;      // Base temporal blend weight (0.95)
    float  sharpness;        // Sharpening strength
    uint   frameIndex;
    uint   screenWidth;
    uint   screenHeight;
    uint   pad;
};

[[vk::push_constant]] ConstantBuffer<TSRPushConstants> tsrPC;

// ─── Catmull-Rom Bicubic Sample ──────────────────────────────────────────
// Higher quality history sampling to reduce shimmer

float4 SampleBicubicCatmullRom(Texture2D<float4> tex, SamplerState samp, float2 uv, float2 texelSize) {
    float2 samplePos = uv / texelSize - 0.5;
    float2 f = frac(samplePos);
    float2 iPos = floor(samplePos);

    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);

    float2 w12 = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);

    float2 tc0 = (iPos - 0.5) * texelSize;
    float2 tc3 = (iPos + 2.5) * texelSize;
    float2 tc12 = (iPos + 0.5 + offset12) * texelSize;

    float4 result = float4(0, 0, 0, 0);
    result += tex.SampleLevel(samp, float2(tc0.x,  tc0.y),  0) * w0.x  * w0.y;
    result += tex.SampleLevel(samp, float2(tc12.x, tc0.y),  0) * w12.x * w0.y;
    result += tex.SampleLevel(samp, float2(tc3.x,  tc0.y),  0) * w3.x  * w0.y;
    result += tex.SampleLevel(samp, float2(tc0.x,  tc12.y), 0) * w0.x  * w12.y;
    result += tex.SampleLevel(samp, float2(tc12.x, tc12.y), 0) * w12.x * w12.y;
    result += tex.SampleLevel(samp, float2(tc3.x,  tc12.y), 0) * w3.x  * w12.y;
    result += tex.SampleLevel(samp, float2(tc0.x,  tc3.y),  0) * w0.x  * w3.y;
    result += tex.SampleLevel(samp, float2(tc12.x, tc3.y),  0) * w12.x * w3.y;
    result += tex.SampleLevel(samp, float2(tc3.x,  tc3.y),  0) * w3.x  * w3.y;

    return max(result, 0.0);
}

// ─── Neighborhood Clamping ───────────────────────────────────────────────
// Prevents ghosting by clamping history to the min/max of the current frame's
// 3x3 neighborhood. Uses YCoCg color space for tighter bounds.

float3 RGBToYCoCg(float3 rgb) {
    float Y  = dot(rgb, float3(0.25, 0.5, 0.25));
    float Co = dot(rgb, float3(0.5, 0.0, -0.5));
    float Cg = dot(rgb, float3(-0.25, 0.5, -0.25));
    return float3(Y, Co, Cg);
}

float3 YCoCgToRGB(float3 ycocg) {
    float Y = ycocg.x, Co = ycocg.y, Cg = ycocg.z;
    return float3(Y + Co - Cg, Y + Cg, Y - Co - Cg);
}

float3 ClipAABB(float3 aabbMin, float3 aabbMax, float3 color) {
    float3 center = 0.5 * (aabbMin + aabbMax);
    float3 extents = 0.5 * (aabbMax - aabbMin) + EPSILON;
    float3 offset = color - center;
    float3 ts = abs(extents / (offset + EPSILON));
    float t = saturate(min(ts.x, min(ts.y, ts.z)));
    return center + offset * t;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= tsrPC.screenWidth || DTid.y >= tsrPC.screenHeight) return;

    float2 uv = (float2(DTid.xy) + 0.5) * tsrPC.texelSize;

    // Get motion vector for this pixel
    float2 motion = g_MotionVectors.SampleLevel(g_LinearClamp, uv, 0);
    float2 historyUV = uv - motion;

    // Sample current frame (from lower resolution, upsampled)
    float3 currentColor = g_CurrentFrame.SampleLevel(g_LinearClamp, uv * tsrPC.renderScale, 0).rgb;

    // Build neighborhood AABB in YCoCg space
    float3 neighborMin = float3(FLT_MAX, FLT_MAX, FLT_MAX);
    float3 neighborMax = float3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    float3 neighborSum = float3(0, 0, 0);

    [unroll]
    for (int dy = -1; dy <= 1; ++dy) {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx) {
            float2 sampleUV = uv + float2(dx, dy) * tsrPC.renderTexelSize;
            float3 s = g_CurrentFrame.SampleLevel(g_PointClamp, sampleUV * tsrPC.renderScale, 0).rgb;
            float3 sYCoCg = RGBToYCoCg(s);
            neighborMin = min(neighborMin, sYCoCg);
            neighborMax = max(neighborMax, sYCoCg);
            neighborSum += sYCoCg;
        }
    }

    // Variance clipping: tighten AABB using mean + stddev
    float3 neighborMean = neighborSum / 9.0;
    float3 neighborRange = (neighborMax - neighborMin) * 0.5;
    neighborMin = neighborMean - neighborRange;
    neighborMax = neighborMean + neighborRange;

    // Sample history with bicubic filter
    float3 historyColor;
    if (all(historyUV >= 0.0) && all(historyUV <= 1.0)) {
        historyColor = SampleBicubicCatmullRom(g_HistoryFrame, g_LinearClamp, historyUV, tsrPC.texelSize).rgb;
    } else {
        historyColor = currentColor; // No valid history
    }

    // Clip history to neighborhood AABB
    float3 historyYCoCg = RGBToYCoCg(historyColor);
    float3 clippedYCoCg = ClipAABB(neighborMin, neighborMax, historyYCoCg);
    historyColor = YCoCgToRGB(clippedYCoCg);

    // Adaptive blend weight based on motion magnitude
    float motionLength = length(motion * float2(tsrPC.screenWidth, tsrPC.screenHeight));
    float blendWeight = lerp(tsrPC.blendFactor, 0.5, saturate(motionLength * 0.1));

    // Temporal blend
    float3 result = lerp(currentColor, historyColor, blendWeight);

    g_Output[DTid.xy] = float4(result, 1.0);
}
