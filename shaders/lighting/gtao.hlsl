// ─── Ground Truth Ambient Occlusion (GTAO) ───────────────────────────────
// Compute shader implementing GTAO with spatial and temporal denoising.
// Based on "Practical Realtime Strategies for Accurate Indirect Occlusion"
// by Jorge Jimenez et al. (2016).
//
// Features:
//   - Horizon-based AO with cosine-weighted integration
//   - Multi-bounce approximation
//   - Bent normal output for indirect lighting
//   - Temporal accumulation with motion-vector reprojection
//   - Spatial filter (edge-preserving bilateral blur)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────
Texture2D<float>  g_DepthBuffer     : register(t0);
Texture2D<float4> g_NormalBuffer    : register(t1);
Texture2D<float2> g_MotionVectors   : register(t2);
Texture2D<float>  g_HistoryAO       : register(t3);
Texture2D<float>  g_NoiseTexture    : register(t4);

RWTexture2D<float>  g_OutputAO       : register(u0);
RWTexture2D<float4> g_OutputBentNorm : register(u1);

SamplerState g_PointClamp  : register(s0);
SamplerState g_LinearClamp : register(s1);

struct GTAOConstants {
    float4x4 projection;
    float4x4 invProjection;
    float4x4 prevViewProj;
    float2   resolution;
    float2   invResolution;
    float    nearPlane;
    float    farPlane;
    float    aoRadius;           // World-space AO radius
    float    aoIntensity;        // AO strength multiplier
    float    aoFalloff;          // Distance falloff exponent
    uint     sliceCount;         // Directional slices (default 3)
    uint     stepsPerSlice;      // Steps per slice (default 4)
    uint     frameIndex;
    float    temporalBlendFactor; // 0.9 = strong temporal
    float    depthThreshold;     // Edge-stop for bilateral blur
    float    normalThreshold;
    float    multiBounceIntensity;
};

[[vk::push_constant]] ConstantBuffer<GTAOConstants> cb;

// ─── Utility Functions ───────────────────────────────────────────────────

float LinearizeDepth(float rawDepth) {
    // Reverse-Z: depth 0 = far, depth 1 = near
    float z = rawDepth;
    return cb.nearPlane * cb.farPlane / (cb.farPlane - z * (cb.farPlane - cb.nearPlane));
}

float3 ReconstructViewPos(float2 uv, float depth) {
    float linearDepth = LinearizeDepth(depth);
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 viewPos = mul(cb.invProjection, float4(ndc, depth, 1.0));
    return viewPos.xyz / viewPos.w;
}

float3 DecodeNormal(float4 normalSample) {
    return normalize(normalSample.xyz * 2.0 - 1.0);
}

// Interleaved gradient noise for spatial jitter
float InterleavedGradientNoise(float2 position) {
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(position, magic.xy)));
}

// Multi-bounce AO approximation (Jimenez 2016)
float MultiBounceAO(float ao, float3 albedo) {
    float3 a = 2.0404 * albedo - 0.3324;
    float3 b = -4.7951 * albedo + 0.6417;
    float3 c = 2.7552 * albedo + 0.6903;
    float x = ao;
    float3 multiBounce = max(float3(x, x, x), ((x * a + b) * x + c) * x);
    return saturate(dot(multiBounce, float3(0.333, 0.333, 0.334)));
}

// ─── GTAO Main Pass ──────────────────────────────────────────────────────
[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;
    float depth = g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0);

    // Skip sky pixels
    if (depth <= 0.0) {
        g_OutputAO[DTid.xy] = 1.0;
        g_OutputBentNorm[DTid.xy] = float4(0, 1, 0, 1);
        return;
    }

    float3 viewPos = ReconstructViewPos(uv, depth);
    float3 viewNormal = DecodeNormal(g_NormalBuffer.SampleLevel(g_PointClamp, uv, 0));

    // Jitter rotation angle per pixel
    float noiseAngle = InterleavedGradientNoise(float2(DTid.xy) + float2(cb.frameIndex * 7, cb.frameIndex * 3));
    float rotationAngle = noiseAngle * PI * 2.0;

    // Screen-space radius based on world-space AO radius
    float screenRadius = cb.aoRadius / max(abs(viewPos.z), 0.001) *
                         cb.projection[0][0] * cb.resolution.x * 0.5;
    screenRadius = clamp(screenRadius, 2.0, 256.0);

    float occlusion = 0.0;
    float3 bentNormal = float3(0, 0, 0);
    float totalWeight = 0.0;

    // Slice-based horizon search
    for (uint slice = 0; slice < cb.sliceCount; ++slice) {
        float sliceAngle = (PI / float(cb.sliceCount)) * (float(slice) + noiseAngle);
        float2 sliceDir = float2(cos(sliceAngle + rotationAngle), sin(sliceAngle + rotationAngle));

        // Search both sides of the slice direction
        float2 horizonAngles = float2(-1.0, -1.0); // cos(horizon angle) for +/- direction

        for (uint step = 1; step <= cb.stepsPerSlice; ++step) {
            float stepRadius = (float(step) / float(cb.stepsPerSlice)) * screenRadius;

            // Positive direction
            {
                float2 sampleUV = uv + sliceDir * stepRadius * cb.invResolution;
                if (all(sampleUV > 0.0) && all(sampleUV < 1.0)) {
                    float sampleDepth = g_DepthBuffer.SampleLevel(g_PointClamp, sampleUV, 0);
                    float3 samplePos = ReconstructViewPos(sampleUV, sampleDepth);
                    float3 diff = samplePos - viewPos;
                    float len = length(diff);

                    if (len > 0.001 && len < cb.aoRadius * 2.0) {
                        float cosAngle = dot(diff / len, viewNormal);
                        float falloff = saturate(1.0 - pow(len / (cb.aoRadius * 2.0), cb.aoFalloff));
                        cosAngle = lerp(cosAngle, -1.0, 1.0 - falloff);
                        horizonAngles.x = max(horizonAngles.x, cosAngle);
                    }
                }
            }

            // Negative direction
            {
                float2 sampleUV = uv - sliceDir * stepRadius * cb.invResolution;
                if (all(sampleUV > 0.0) && all(sampleUV < 1.0)) {
                    float sampleDepth = g_DepthBuffer.SampleLevel(g_PointClamp, sampleUV, 0);
                    float3 samplePos = ReconstructViewPos(sampleUV, sampleDepth);
                    float3 diff = samplePos - viewPos;
                    float len = length(diff);

                    if (len > 0.001 && len < cb.aoRadius * 2.0) {
                        float cosAngle = dot(diff / len, viewNormal);
                        float falloff = saturate(1.0 - pow(len / (cb.aoRadius * 2.0), cb.aoFalloff));
                        cosAngle = lerp(cosAngle, -1.0, 1.0 - falloff);
                        horizonAngles.y = max(horizonAngles.y, cosAngle);
                    }
                }
            }
        }

        // Cosine-weighted integration of visible hemisphere
        float h0 = acos(clamp(horizonAngles.x, -1.0, 1.0));
        float h1 = acos(clamp(horizonAngles.y, -1.0, 1.0));

        // Inner integral: visible fraction of hemisphere for this slice
        float sliceAO = 0.25 * (-cos(2.0 * h0) + 2.0 * h0 + cos(0.0)) +
                         0.25 * (-cos(2.0 * h1) + 2.0 * h1 + cos(0.0));
        sliceAO /= PI;

        occlusion += sliceAO;

        // Accumulate bent normal
        float avgAngle = (h0 + h1) * 0.5;
        float3 sliceBent = viewNormal * cos(avgAngle) +
                           float3(sliceDir * sin(avgAngle), 0) * 0.5;
        bentNormal += sliceBent;

        totalWeight += 1.0;
    }

    occlusion = saturate(occlusion / max(totalWeight, 1.0));
    bentNormal = normalize(bentNormal / max(totalWeight, 1.0));

    // Apply intensity
    occlusion = pow(occlusion, cb.aoIntensity);

    // Multi-bounce approximation (assume 0.5 albedo)
    if (cb.multiBounceIntensity > 0.0) {
        float multiBounce = MultiBounceAO(occlusion, float3(0.5, 0.5, 0.5));
        occlusion = lerp(occlusion, multiBounce, cb.multiBounceIntensity);
    }

    // Temporal accumulation with motion vector reprojection
    float2 motion = g_MotionVectors.SampleLevel(g_PointClamp, uv, 0);
    float2 prevUV = uv + motion;

    if (all(prevUV > 0.0) && all(prevUV < 1.0)) {
        float historyAO = g_HistoryAO.SampleLevel(g_LinearClamp, prevUV, 0);
        occlusion = lerp(occlusion, historyAO, cb.temporalBlendFactor);
    }

    g_OutputAO[DTid.xy] = occlusion;
    g_OutputBentNorm[DTid.xy] = float4(bentNormal * 0.5 + 0.5, 1.0);
}

// ─── Spatial Denoise (Bilateral Blur) ────────────────────────────────────
Texture2D<float> g_InputAO : register(t5);
RWTexture2D<float> g_BlurredAO : register(u2);

[numthreads(8, 8, 1)]
void CSSpatialDenoise(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;
    float centerDepth = LinearizeDepth(g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0));
    float3 centerNormal = DecodeNormal(g_NormalBuffer.SampleLevel(g_PointClamp, uv, 0));
    float centerAO = g_InputAO.SampleLevel(g_PointClamp, uv, 0);

    float totalAO = centerAO;
    float totalWeight = 1.0;

    // 3x3 bilateral blur
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0) continue;

            float2 sampleUV = uv + float2(x, y) * cb.invResolution;
            float sampleDepth = LinearizeDepth(g_DepthBuffer.SampleLevel(g_PointClamp, sampleUV, 0));
            float3 sampleNormal = DecodeNormal(g_NormalBuffer.SampleLevel(g_PointClamp, sampleUV, 0));
            float sampleAO = g_InputAO.SampleLevel(g_PointClamp, sampleUV, 0);

            // Depth edge-stop
            float depthWeight = exp(-abs(centerDepth - sampleDepth) / max(cb.depthThreshold * centerDepth, 0.001));
            // Normal edge-stop
            float normalWeight = pow(max(dot(centerNormal, sampleNormal), 0.0), cb.normalThreshold);

            float w = depthWeight * normalWeight;
            totalAO += sampleAO * w;
            totalWeight += w;
        }
    }

    g_BlurredAO[DTid.xy] = totalAO / max(totalWeight, 0.001);
}
