// ─── Generic Temporal Reprojection Filter ────────────────────────────────
// Reusable temporal accumulation/reprojection shader for any history buffer.
// Supports:
//   - Motion vector-based reprojection
//   - Neighborhood color clamping (variance or AABB)
//   - Configurable blend factor with disocclusion detection
//   - Luminance-weighted blending for HDR stability
//   - Catmull-Rom history sampling for sub-pixel accuracy
//
// Used by: TAA, SSAO temporal, SSGI temporal, SSR temporal, denoiser
//
// References:
//   - "Temporal Reprojection Anti-Aliasing in INSIDE" (Pedersen, GDC 2016)
//   - "High Quality Temporal Supersampling" (Karis, SIGGRAPH 2014)

#include "../common/math.hlsl"

Texture2D<float4> g_CurrentFrame  : register(t0); // Current frame input
Texture2D<float4> g_HistoryFrame  : register(t1); // Previous frame accumulated
Texture2D<float2> g_MotionVectors : register(t2); // Screen-space motion vectors
Texture2D<float>  g_DepthCurrent  : register(t3); // Current depth
Texture2D<float>  g_DepthHistory  : register(t4); // Previous frame depth

RWTexture2D<float4> g_Output : register(u0); // Accumulated output

SamplerState g_PointClamp   : register(s0);
SamplerState g_LinearClamp  : register(s1);

struct TemporalReprojectConstants {
    float2 resolution;
    float2 invResolution;
    float  blendFactor;          // Base blend with history (default 0.95)
    float  varianceClipGamma;    // Variance clip aggressiveness (default 1.0)
    float  depthRejectionThreshold; // Depth difference for disocclusion (default 0.01)
    float  luminanceWeight;      // HDR luminance weighting (default 1.0)
    uint   clampMode;            // 0: AABB, 1: variance clip
    uint   useCatmullRom;        // 1: bicubic history sampling
    uint   frameIndex;
    float  pad0;
};

[[vk::push_constant]] ConstantBuffer<TemporalReprojectConstants> cb;

// ─── Utility ─────────────────────────────────────────────────────────────

float Luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 ToneMap(float3 color) {
    return color / (1.0 + Luminance(color));
}

float3 ToneUnmap(float3 color) {
    return color / max(1.0 - Luminance(color), 0.001);
}

// ─── Catmull-Rom Bicubic Sampling ────────────────────────────────────────
// 5-tap Catmull-Rom for sharper history sampling (reduces ghosting blur)

float4 SampleCatmullRom(Texture2D<float4> tex, SamplerState samp, float2 uv, float2 texSize) {
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5) + 0.5;
    float2 f = samplePos - texPos1;

    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);

    float2 w12 = w1 + w2;
    float2 offset12 = w2 / max(w12, 0.001);

    float2 texPos0 = (texPos1 - 1.0) / texSize;
    float2 texPos3 = (texPos1 + 2.0) / texSize;
    float2 texPos12 = (texPos1 + offset12) / texSize;

    float4 result = float4(0, 0, 0, 0);
    result += tex.SampleLevel(samp, float2(texPos0.x, texPos0.y), 0) * w0.x * w0.y;
    result += tex.SampleLevel(samp, float2(texPos12.x, texPos0.y), 0) * w12.x * w0.y;
    result += tex.SampleLevel(samp, float2(texPos3.x, texPos0.y), 0) * w3.x * w0.y;

    result += tex.SampleLevel(samp, float2(texPos0.x, texPos12.y), 0) * w0.x * w12.y;
    result += tex.SampleLevel(samp, float2(texPos12.x, texPos12.y), 0) * w12.x * w12.y;
    result += tex.SampleLevel(samp, float2(texPos3.x, texPos12.y), 0) * w3.x * w12.y;

    result += tex.SampleLevel(samp, float2(texPos0.x, texPos3.y), 0) * w0.x * w3.y;
    result += tex.SampleLevel(samp, float2(texPos12.x, texPos3.y), 0) * w12.x * w3.y;
    result += tex.SampleLevel(samp, float2(texPos3.x, texPos3.y), 0) * w3.x * w3.y;

    return max(result, float4(0, 0, 0, 0));
}

// ─── Neighborhood Clamping ───────────────────────────────────────────────

struct NeighborhoodData {
    float3 minColor;
    float3 maxColor;
    float3 mean;
    float3 variance;
};

NeighborhoodData GatherNeighborhood(float2 uv) {
    NeighborhoodData data;
    float3 m1 = float3(0, 0, 0);
    float3 m2 = float3(0, 0, 0);
    float3 minC = float3(1e10, 1e10, 1e10);
    float3 maxC = float3(-1e10, -1e10, -1e10);

    // 3x3 neighborhood (cross pattern for performance, or full 3x3)
    const int2 offsets[9] = {
        int2(-1, -1), int2(0, -1), int2(1, -1),
        int2(-1,  0), int2(0,  0), int2(1,  0),
        int2(-1,  1), int2(0,  1), int2(1,  1)
    };

    for (uint i = 0; i < 9; ++i) {
        float2 sampleUV = uv + float2(offsets[i]) * cb.invResolution;
        float3 c = ToneMap(g_CurrentFrame.SampleLevel(g_PointClamp, sampleUV, 0).rgb);

        m1 += c;
        m2 += c * c;
        minC = min(minC, c);
        maxC = max(maxC, c);
    }

    data.mean = m1 / 9.0;
    data.variance = sqrt(max(m2 / 9.0 - data.mean * data.mean, float3(0, 0, 0)));
    data.minColor = minC;
    data.maxColor = maxC;

    return data;
}

float3 ClipAABB(float3 color, float3 minC, float3 maxC) {
    float3 center = (minC + maxC) * 0.5;
    float3 extents = (maxC - minC) * 0.5 + 0.001;

    float3 offset = color - center;
    float3 ts = abs(extents / max(abs(offset), float3(0.0001, 0.0001, 0.0001)));
    float t = saturate(min(ts.x, min(ts.y, ts.z)));

    return center + offset * t;
}

float3 VarianceClip(float3 color, float3 mean, float3 variance, float gamma) {
    float3 minC = mean - variance * gamma;
    float3 maxC = mean + variance * gamma;
    return clamp(color, minC, maxC);
}

// ─── Disocclusion Detection ──────────────────────────────────────────────

float ComputeDisocclusion(float2 uv, float2 historyUV) {
    float currentDepth = g_DepthCurrent.SampleLevel(g_PointClamp, uv, 0);
    float historyDepth = g_DepthHistory.SampleLevel(g_PointClamp, historyUV, 0);

    float depthDiff = abs(currentDepth - historyDepth);
    return saturate(depthDiff / cb.depthRejectionThreshold);
}

// ─── Main Temporal Reprojection ──────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSTemporalReproject(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    // Current frame color (tone-mapped for stable clamping)
    float3 currentColor = g_CurrentFrame.SampleLevel(g_PointClamp, uv, 0).rgb;
    float3 currentTM = ToneMap(currentColor);

    // Reproject using motion vectors
    float2 motion = g_MotionVectors.SampleLevel(g_PointClamp, uv, 0);
    float2 historyUV = uv - motion;

    // Check if reprojected UV is valid
    bool validHistory = all(historyUV >= 0.0) && all(historyUV <= 1.0);

    float3 historyColor;
    if (validHistory) {
        if (cb.useCatmullRom) {
            historyColor = SampleCatmullRom(g_HistoryFrame, g_LinearClamp, historyUV, cb.resolution).rgb;
        } else {
            historyColor = g_HistoryFrame.SampleLevel(g_LinearClamp, historyUV, 0).rgb;
        }
    } else {
        // No valid history — use current frame
        g_Output[DTid.xy] = float4(currentColor, 1.0);
        return;
    }

    float3 historyTM = ToneMap(historyColor);

    // Neighborhood clamping to reduce ghosting
    NeighborhoodData neighborhood = GatherNeighborhood(uv);

    float3 clampedHistory;
    if (cb.clampMode == 0) {
        // AABB clamp
        clampedHistory = ClipAABB(historyTM, neighborhood.minColor, neighborhood.maxColor);
    } else {
        // Variance clip (tighter, less flickering)
        clampedHistory = VarianceClip(historyTM, neighborhood.mean,
                                       neighborhood.variance, cb.varianceClipGamma);
    }

    // Disocclusion detection — reduce blend factor at depth discontinuities
    float disocclusion = ComputeDisocclusion(uv, historyUV);
    float blendFactor = lerp(cb.blendFactor, 0.0, disocclusion);

    // Luminance-weighted blending for HDR stability
    if (cb.luminanceWeight > 0.0) {
        float lumCurrent = Luminance(currentTM);
        float lumHistory = Luminance(clampedHistory);
        float weightCurrent = 1.0 / (1.0 + lumCurrent * cb.luminanceWeight);
        float weightHistory = 1.0 / (1.0 + lumHistory * cb.luminanceWeight);

        float3 blended = (clampedHistory * weightHistory * blendFactor +
                           currentTM * weightCurrent * (1.0 - blendFactor)) /
                          max(weightHistory * blendFactor + weightCurrent * (1.0 - blendFactor), 0.001);

        g_Output[DTid.xy] = float4(ToneUnmap(blended), 1.0);
    } else {
        float3 blended = lerp(currentTM, clampedHistory, blendFactor);
        g_Output[DTid.xy] = float4(ToneUnmap(blended), 1.0);
    }
}

// ─── History Copy Pass (first frame / reset) ─────────────────────────────

[numthreads(8, 8, 1)]
void CSHistoryCopy(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;
    g_Output[DTid.xy] = g_CurrentFrame.SampleLevel(g_PointClamp, uv, 0);
}
