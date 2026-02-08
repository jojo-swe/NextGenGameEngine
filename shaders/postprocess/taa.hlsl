// ─── Temporal Anti-Aliasing (TAA) ─────────────────────────────────────────
// Resolves jittered sub-pixel samples across frames for smooth edges.
// Uses motion vectors for reprojection and neighborhood clamping to
// reduce ghosting artifacts.
//
// Features:
//   - YCoCg color space for better clamping
//   - Variance-based neighborhood clipping (AABB clamp)
//   - Velocity rejection for disoccluded regions
//   - Luminance-weighted blending (reduces flickering on specular)

#include "../common/math.hlsl"

struct TAAConstants {
    uint2    screenSize;
    float2   jitterOffset;    // Sub-pixel jitter for current frame
    float    feedbackMin;     // Min blend factor (0.88 = aggressive, 0.95 = stable)
    float    feedbackMax;     // Max blend factor (0.97)
    float    velocityRejection; // Velocity threshold for history rejection
    uint     frameIndex;
};

[[vk::push_constant]] ConstantBuffer<TAAConstants> pc;

Texture2D<float4>   g_CurrentColor  : register(t0, space28);
Texture2D<float4>   g_HistoryColor  : register(t1, space28);
Texture2D<float2>   g_MotionVectors : register(t2, space28);
Texture2D<float>    g_DepthBuffer   : register(t3, space28);

RWTexture2D<float4> g_OutputColor   : register(u0, space28);

SamplerState g_LinearClamp : register(s0, space28);
SamplerState g_PointClamp  : register(s1, space28);

// ─── Color Space Conversion ──────────────────────────────────────────────

float3 RGBToYCoCg(float3 rgb) {
    float Y  = dot(rgb, float3(0.25, 0.5, 0.25));
    float Co = dot(rgb, float3(0.5, 0.0, -0.5));
    float Cg = dot(rgb, float3(-0.25, 0.5, -0.25));
    return float3(Y, Co, Cg);
}

float3 YCoCgToRGB(float3 ycocg) {
    float Y  = ycocg.x;
    float Co = ycocg.y;
    float Cg = ycocg.z;
    return float3(Y + Co - Cg, Y + Cg, Y - Co - Cg);
}

float Luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// ─── Neighborhood Sampling ───────────────────────────────────────────────

float3 SampleColor(int2 pos) {
    pos = clamp(pos, int2(0, 0), int2(pc.screenSize) - 1);
    return g_CurrentColor[pos].rgb;
}

// Find closest depth in 3×3 neighborhood (for better motion vector selection)
float2 GetClosestMotionVector(int2 center) {
    float closestDepth = 0; // Reverse-Z: larger = closer
    int2 closestPos = center;

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int2 pos = center + int2(dx, dy);
            pos = clamp(pos, int2(0, 0), int2(pc.screenSize) - 1);
            float d = g_DepthBuffer[pos];
            if (d > closestDepth) {
                closestDepth = d;
                closestPos = pos;
            }
        }
    }

    return g_MotionVectors[closestPos];
}

// ─── Main TAA Resolve ────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.screenSize.x || DTid.y >= pc.screenSize.y) return;

    int2 pixelPos = int2(DTid.xy);
    float2 texelSize = 1.0 / float2(pc.screenSize);
    float2 uv = (float2(pixelPos) + 0.5) * texelSize;

    // ── Sample 3×3 neighborhood in YCoCg ─────────────────────────────
    float3 samples[9];
    int idx = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            samples[idx] = RGBToYCoCg(SampleColor(pixelPos + int2(dx, dy)));
            idx++;
        }
    }

    float3 centerYCoCg = samples[4]; // Center sample

    // Compute neighborhood AABB (min/max) for clamping
    float3 neighborMin = samples[0];
    float3 neighborMax = samples[0];
    float3 moment1 = float3(0, 0, 0);
    float3 moment2 = float3(0, 0, 0);

    for (int i = 0; i < 9; ++i) {
        neighborMin = min(neighborMin, samples[i]);
        neighborMax = max(neighborMax, samples[i]);
        moment1 += samples[i];
        moment2 += samples[i] * samples[i];
    }

    // Variance-based clipping: tighter AABB using mean ± gamma * stddev
    moment1 /= 9.0;
    moment2 /= 9.0;
    float3 stddev = sqrt(abs(moment2 - moment1 * moment1));
    float gamma = 1.25; // Tighter = less ghosting, more flickering

    float3 clipMin = moment1 - gamma * stddev;
    float3 clipMax = moment1 + gamma * stddev;

    // Use tighter variance-based AABB
    neighborMin = max(neighborMin, clipMin);
    neighborMax = min(neighborMax, clipMax);

    // ── Reproject history ────────────────────────────────────────────
    float2 motion = GetClosestMotionVector(pixelPos);
    float2 historyUV = uv + motion;

    // Unjitter the current frame's UV
    float2 unjitteredUV = uv - pc.jitterOffset * texelSize;

    bool historyValid = (historyUV.x >= 0 && historyUV.x <= 1 &&
                          historyUV.y >= 0 && historyUV.y <= 1);

    float3 historyYCoCg;
    if (historyValid) {
        float3 historyRGB = g_HistoryColor.SampleLevel(g_LinearClamp, historyUV, 0).rgb;
        historyYCoCg = RGBToYCoCg(historyRGB);

        // Clamp history to neighborhood AABB
        historyYCoCg = clamp(historyYCoCg, neighborMin, neighborMax);
    } else {
        historyYCoCg = centerYCoCg;
    }

    // ── Blend ────────────────────────────────────────────────────────
    // Adaptive blend factor based on velocity and luminance difference
    float velocityLength = length(motion * float2(pc.screenSize));
    float velocityFactor = saturate(velocityLength * pc.velocityRejection);

    // Higher velocity → more current frame (less ghosting)
    float feedback = lerp(pc.feedbackMax, pc.feedbackMin, velocityFactor);

    // Luminance difference weighting (reduce flickering on specular)
    float lumCurrent = centerYCoCg.x;
    float lumHistory = historyYCoCg.x;
    float lumDiff = abs(lumCurrent - lumHistory) / max(lumCurrent + lumHistory, 0.001);
    feedback = lerp(feedback, pc.feedbackMin, saturate(lumDiff * 2.0));

    if (!historyValid) feedback = 0; // No history → use current frame entirely

    // Blend in YCoCg space
    float3 resolvedYCoCg = lerp(centerYCoCg, historyYCoCg, feedback);

    // Convert back to RGB
    float3 resolvedRGB = YCoCgToRGB(resolvedYCoCg);
    resolvedRGB = max(resolvedRGB, 0.0);

    g_OutputColor[DTid.xy] = float4(resolvedRGB, 1.0);
}
