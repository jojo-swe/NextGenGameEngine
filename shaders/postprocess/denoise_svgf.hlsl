// ─── SVGF Denoiser (Spatiotemporal Variance-Guided Filtering) ────────────
// Denoises 1-SPP path traced output for real-time use.
// Based on Schied et al., "Spatiotemporal Variance-Guided Filtering"
//
// Pipeline:
//   1. Temporal accumulation (reproject, validate, blend with history)
//   2. Variance estimation (spatial moments filtering)
//   3. A-trous wavelet filter (5 iterations, edge-stopping on normal/depth/luminance)

#include "../common/math.hlsl"

struct DenoiseConstants {
    float4x4 invViewProj;
    float4x4 prevViewProj;
    uint2    screenSize;
    float    temporalAlpha;   // Blend factor for temporal accumulation (0.05 = slow, 0.2 = fast)
    float    sigmaLuminance;  // Edge-stopping luminance sensitivity
    float    sigmaNormal;     // Edge-stopping normal sensitivity
    float    sigmaDepth;      // Edge-stopping depth sensitivity
    uint     atrousIteration; // Current a-trous pass (0-4)
    uint     frameIndex;
};

[[vk::push_constant]] ConstantBuffer<DenoiseConstants> pc;

// ─── Textures ────────────────────────────────────────────────────────────

Texture2D<float4>   g_NoisyInput    : register(t0, space24); // 1-SPP path traced color
Texture2D<float4>   g_GBuffer_Normal : register(t1, space24);
Texture2D<float>    g_DepthBuffer    : register(t2, space24);
Texture2D<float2>   g_MotionVectors  : register(t3, space24);
Texture2D<float4>   g_HistoryColor   : register(t4, space24); // Previous frame denoised
Texture2D<float2>   g_HistoryMoments : register(t5, space24); // Previous frame moments (mean, variance)

RWTexture2D<float4> g_OutputColor    : register(u0, space24);
RWTexture2D<float2> g_OutputMoments  : register(u1, space24);

SamplerState g_LinearClamp : register(s0, space24);
SamplerState g_PointClamp  : register(s1, space24);

// ─── Utility ─────────────────────────────────────────────────────────────

float Luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 ReconstructWorldPos(float2 uv, float depth) {
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 clip = float4(ndc, depth, 1.0);
    float4 world = mul(pc.invViewProj, clip);
    return world.xyz / world.w;
}

// ─── Pass 1: Temporal Accumulation ───────────────────────────────────────

[numthreads(8, 8, 1)]
void CSTemporalAccum(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.screenSize.x || DTid.y >= pc.screenSize.y) return;

    float2 texelSize = 1.0 / float2(pc.screenSize);
    float2 uv = (float2(DTid.xy) + 0.5) * texelSize;

    float4 currentColor = g_NoisyInput[DTid.xy];
    float3 currentNormal = normalize(g_GBuffer_Normal[DTid.xy].xyz * 2.0 - 1.0);
    float  currentDepth = g_DepthBuffer[DTid.xy];

    // Reproject to previous frame using motion vectors
    float2 motion = g_MotionVectors[DTid.xy];
    float2 prevUV = uv + motion;

    // Validate reprojection
    bool valid = (prevUV.x >= 0 && prevUV.x <= 1 && prevUV.y >= 0 && prevUV.y <= 1);

    float4 historyColor = float4(0, 0, 0, 0);
    float2 historyMoments = float2(0, 0);

    if (valid) {
        historyColor = g_HistoryColor.SampleLevel(g_LinearClamp, prevUV, 0);
        historyMoments = g_HistoryMoments.SampleLevel(g_LinearClamp, prevUV, 0);

        // Depth consistency check
        // (simplified — full SVGF would also check normal consistency)
        float reprojDepth = g_DepthBuffer.SampleLevel(g_PointClamp, prevUV, 0);
        float depthDiff = abs(currentDepth - reprojDepth) / max(currentDepth, 0.001);
        if (depthDiff > 0.1) valid = false;
    }

    float alpha = valid ? pc.temporalAlpha : 1.0; // Use full noisy input if invalid

    // Blend current with history
    float4 accumulated = lerp(historyColor, currentColor, alpha);

    // Update moments (for variance estimation)
    float lum = Luminance(currentColor.rgb);
    float2 moments;
    moments.x = lerp(historyMoments.x, lum, alpha);           // Mean
    moments.y = lerp(historyMoments.y, lum * lum, alpha);     // Second moment

    g_OutputColor[DTid.xy] = accumulated;
    g_OutputMoments[DTid.xy] = moments;
}

// ─── Pass 2: A-Trous Wavelet Filter ─────────────────────────────────────
// Edge-preserving spatial filter with increasing step size per iteration.
// Uses a 5×5 kernel with Gaussian weights.

static const float g_Kernel[3] = {1.0, 2.0 / 3.0, 1.0 / 6.0};

float EdgeStopNormal(float3 centerNormal, float3 sampleNormal) {
    return pow(max(dot(centerNormal, sampleNormal), 0.0), pc.sigmaNormal);
}

float EdgeStopDepth(float centerDepth, float sampleDepth, float2 gradient) {
    return exp(-abs(centerDepth - sampleDepth) / (pc.sigmaDepth * abs(dot(gradient, float2(1, 1))) + 0.0001));
}

float EdgeStopLuminance(float centerLum, float sampleLum, float variance) {
    float sigma = pc.sigmaLuminance * sqrt(max(variance, 0.0)) + 0.0001;
    return exp(-abs(centerLum - sampleLum) / sigma);
}

[numthreads(8, 8, 1)]
void CSATrous(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.screenSize.x || DTid.y >= pc.screenSize.y) return;

    float2 texelSize = 1.0 / float2(pc.screenSize);
    int2 center = int2(DTid.xy);

    float4 centerColor = g_OutputColor[center];
    float3 centerNormal = normalize(g_GBuffer_Normal[center].xyz * 2.0 - 1.0);
    float  centerDepth = g_DepthBuffer[center];
    float2 centerMoments = g_OutputMoments[center];
    float  centerLum = Luminance(centerColor.rgb);
    float  variance = max(centerMoments.y - centerMoments.x * centerMoments.x, 0.0);

    // Depth gradient for edge-stopping
    float depthRight = g_DepthBuffer[min(center + int2(1, 0), int2(pc.screenSize) - 1)];
    float depthDown  = g_DepthBuffer[min(center + int2(0, 1), int2(pc.screenSize) - 1)];
    float2 depthGrad = float2(depthRight - centerDepth, depthDown - centerDepth);

    int stepSize = 1 << pc.atrousIteration; // 1, 2, 4, 8, 16

    float4 colorSum = centerColor * g_Kernel[0];
    float weightSum = g_Kernel[0];

    // 5×5 kernel (offsets: -2, -1, 0, 1, 2)
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (dx == 0 && dy == 0) continue;

            int2 offset = int2(dx, dy) * stepSize;
            int2 samplePos = center + offset;

            // Bounds check
            if (samplePos.x < 0 || samplePos.x >= int(pc.screenSize.x) ||
                samplePos.y < 0 || samplePos.y >= int(pc.screenSize.y)) continue;

            float4 sampleColor = g_OutputColor[samplePos];
            float3 sampleNormal = normalize(g_GBuffer_Normal[samplePos].xyz * 2.0 - 1.0);
            float  sampleDepth = g_DepthBuffer[samplePos];
            float  sampleLum = Luminance(sampleColor.rgb);

            // Gaussian kernel weight
            float kernelW = g_Kernel[abs(dx)] * g_Kernel[abs(dy)];

            // Edge-stopping weights
            float wNormal = EdgeStopNormal(centerNormal, sampleNormal);
            float wDepth = EdgeStopDepth(centerDepth, sampleDepth, depthGrad);
            float wLum = EdgeStopLuminance(centerLum, sampleLum, variance);

            float weight = kernelW * wNormal * wDepth * wLum;

            colorSum += sampleColor * weight;
            weightSum += weight;
        }
    }

    g_OutputColor[DTid.xy] = colorSum / max(weightSum, 0.0001);
}
