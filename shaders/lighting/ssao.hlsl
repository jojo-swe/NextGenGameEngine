// ─── GTAO (Ground Truth Ambient Occlusion) ───────────────────────────────
// Based on "Practical Real-Time Strategies for Accurate Indirect Occlusion"
// (Jimenez et al., SIGGRAPH 2016).
//
// Improvement over HBAO+ with better accuracy and temporal stability.
// Operates in view space, uses the depth buffer for horizon angle estimation.
//
// Pipeline:
//   1. GTAO compute: estimate AO per pixel via horizon-based integration
//   2. Spatial denoise: bilateral blur to smooth noise
//   3. Temporal accumulation: blend with previous frame

#include "../common/math.hlsl"

struct GTAOConstants {
    float4x4 projMatrix;
    float4x4 invProjMatrix;
    uint2    screenSize;
    float    aoRadius;           // World-space AO radius (1.5m)
    float    aoIntensity;        // Strength multiplier (1.5)
    float    aoFalloff;          // Distance falloff exponent (2.0)
    float    aoThicknessBlend;   // Thin object heuristic (0.1)
    uint     sliceCount;         // Angular slices (4)
    uint     stepsPerSlice;      // Steps per direction (3)
    float    nearPlane;
    float    farPlane;
    uint     frameIndex;
    uint     pad;
};

[[vk::push_constant]] ConstantBuffer<GTAOConstants> pc;

Texture2D<float>    g_DepthBuffer   : register(t0, space14);
Texture2D<float4>   g_Normals       : register(t1, space14); // View-space normals
Texture2D<float>    g_PrevAO        : register(t2, space14); // Previous frame AO
Texture2D<float2>   g_MotionVectors : register(t3, space14);

RWTexture2D<float>  g_AOOutput      : register(u0, space14);
RWTexture2D<float>  g_AODenoised    : register(u1, space14);

SamplerState g_PointClamp : register(s0, space14);

// ─── Utility ─────────────────────────────────────────────────────────────

float LinearizeDepth(float rawDepth) {
    return pc.nearPlane / rawDepth; // Reverse-Z
}

float3 ReconstructViewPos(float2 uv, float depth) {
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 clip = float4(ndc, depth, 1.0);
    float4 view = mul(pc.invProjMatrix, clip);
    return view.xyz / view.w;
}

// Spatial hash for per-pixel rotation
float SpatialNoise(uint2 pixel) {
    // R2 sequence for low-discrepancy noise
    float2 alpha = float2(0.7548776662, 0.5698402910);
    return frac(0.5 + float(pixel.x) * alpha.x + float(pixel.y) * alpha.y);
}

float TemporalNoise(uint2 pixel, uint frame) {
    return frac(SpatialNoise(pixel) + float(frame % 64) * 0.6180339887);
}

// ─── GTAO Core ───────────────────────────────────────────────────────────

float IntegrateArc(float h1, float h2, float n) {
    // Integrate the cosine-weighted visibility over the arc [h1, h2]
    // given surface normal angle n
    float sinN = sin(n);
    float cosN = cos(n);

    return 0.25 * (-cos(2.0 * h1 - n) + cosN + 2.0 * h1 * sinN)
         + 0.25 * (-cos(2.0 * h2 - n) + cosN + 2.0 * h2 * sinN);
}

[numthreads(8, 8, 1)]
void GTAOMain(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.screenSize.x || DTid.y >= pc.screenSize.y) return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(pc.screenSize);
    float depth = g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0);

    if (depth <= 0.0) {
        g_AOOutput[DTid.xy] = 1.0;
        return;
    }

    float3 viewPos = ReconstructViewPos(uv, depth);
    float3 viewNormal = normalize(g_Normals.SampleLevel(g_PointClamp, uv, 0).xyz * 2.0 - 1.0);

    // Project AO radius to screen space
    float projScale = pc.projMatrix[0][0]; // Focal length X
    float screenRadius = pc.aoRadius * projScale / abs(viewPos.z);
    screenRadius = clamp(screenRadius, 2.0, 256.0); // Min 2 pixels, max 256

    float noise = TemporalNoise(DTid.xy, pc.frameIndex);
    float totalAO = 0;

    for (uint slice = 0; slice < pc.sliceCount; ++slice) {
        // Rotate direction per slice with golden angle + per-pixel noise
        float phi = (PI / float(pc.sliceCount)) * (float(slice) + noise);
        float2 dir = float2(cos(phi), sin(phi));

        // Find horizon angles in both directions along this slice
        float horizonCos1 = -1.0; // cos(horizon angle), positive direction
        float horizonCos2 = -1.0; // negative direction

        for (uint step = 1; step <= pc.stepsPerSlice; ++step) {
            float t = (float(step) + noise * 0.5) / float(pc.stepsPerSlice);
            float2 offset = dir * t * screenRadius / float2(pc.screenSize);

            // Positive direction
            float2 sampleUV1 = uv + offset;
            if (sampleUV1.x >= 0 && sampleUV1.x <= 1 && sampleUV1.y >= 0 && sampleUV1.y <= 1) {
                float sampleDepth1 = g_DepthBuffer.SampleLevel(g_PointClamp, sampleUV1, 0);
                float3 samplePos1 = ReconstructViewPos(sampleUV1, sampleDepth1);
                float3 horizonVec1 = samplePos1 - viewPos;
                float horizonDist1 = length(horizonVec1);

                if (horizonDist1 > 0.001 && horizonDist1 < pc.aoRadius) {
                    float cosH = dot(normalize(horizonVec1), viewNormal);
                    // Distance falloff
                    float falloff = saturate(1.0 - pow(horizonDist1 / pc.aoRadius, pc.aoFalloff));
                    cosH = lerp(-1.0, cosH, falloff);
                    horizonCos1 = max(horizonCos1, cosH);
                }
            }

            // Negative direction
            float2 sampleUV2 = uv - offset;
            if (sampleUV2.x >= 0 && sampleUV2.x <= 1 && sampleUV2.y >= 0 && sampleUV2.y <= 1) {
                float sampleDepth2 = g_DepthBuffer.SampleLevel(g_PointClamp, sampleUV2, 0);
                float3 samplePos2 = ReconstructViewPos(sampleUV2, sampleDepth2);
                float3 horizonVec2 = samplePos2 - viewPos;
                float horizonDist2 = length(horizonVec2);

                if (horizonDist2 > 0.001 && horizonDist2 < pc.aoRadius) {
                    float cosH = dot(normalize(horizonVec2), viewNormal);
                    float falloff = saturate(1.0 - pow(horizonDist2 / pc.aoRadius, pc.aoFalloff));
                    cosH = lerp(-1.0, cosH, falloff);
                    horizonCos2 = max(horizonCos2, cosH);
                }
            }
        }

        // Convert cos to angles
        float h1 = acos(clamp(horizonCos1, -1.0, 1.0));
        float h2 = -acos(clamp(horizonCos2, -1.0, 1.0));

        // Project normal onto slice plane
        float3 sliceDir3 = float3(dir.x, dir.y, 0);
        float3 projNormal = viewNormal - sliceDir3 * dot(viewNormal, sliceDir3);
        float normalAngle = atan2(projNormal.z, length(projNormal.xy));

        // Clamp horizons to hemisphere
        h1 = max(h1, normalAngle);
        h2 = min(h2, -normalAngle);

        // Integrate visibility
        float visibility = IntegrateArc(h1, h2, normalAngle);
        totalAO += visibility;
    }

    totalAO /= float(pc.sliceCount);
    totalAO = saturate(totalAO);

    // Apply intensity
    float ao = pow(totalAO, pc.aoIntensity);

    g_AOOutput[DTid.xy] = ao;
}

// ─── Spatial Denoise (Bilateral Blur) ────────────────────────────────────

[numthreads(8, 8, 1)]
void DenoiseCS(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.screenSize.x || DTid.y >= pc.screenSize.y) return;

    float2 texelSize = 1.0 / float2(pc.screenSize);
    float2 uv = (float2(DTid.xy) + 0.5) * texelSize;

    float centerAO = g_AOOutput[DTid.xy];
    float centerDepth = g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0);
    float3 centerNormal = g_Normals.SampleLevel(g_PointClamp, uv, 0).xyz * 2.0 - 1.0;

    float totalAO = 0;
    float totalWeight = 0;

    // 5×5 bilateral blur
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            float2 sampleUV = uv + float2(x, y) * texelSize;
            sampleUV = clamp(sampleUV, texelSize, 1.0 - texelSize);

            uint2 samplePixel = uint2(sampleUV * float2(pc.screenSize));

            float sampleAO = g_AOOutput[samplePixel];
            float sampleDepth = g_DepthBuffer.SampleLevel(g_PointClamp, sampleUV, 0);
            float3 sampleNormal = g_Normals.SampleLevel(g_PointClamp, sampleUV, 0).xyz * 2.0 - 1.0;

            // Depth weight
            float depthDiff = abs(centerDepth - sampleDepth);
            float depthWeight = exp(-depthDiff * 1000.0);

            // Normal weight
            float normalDot = max(dot(centerNormal, sampleNormal), 0.0);
            float normalWeight = pow(normalDot, 32.0);

            // Spatial weight (Gaussian)
            float spatialWeight = exp(-0.5 * float(x * x + y * y) / 4.0);

            float weight = depthWeight * normalWeight * spatialWeight;
            totalAO += sampleAO * weight;
            totalWeight += weight;
        }
    }

    float denoised = totalWeight > 0.001 ? totalAO / totalWeight : centerAO;

    // Temporal accumulation
    float2 motion = g_MotionVectors.SampleLevel(g_PointClamp, uv, 0);
    float2 prevUV = uv - motion;

    if (prevUV.x >= 0 && prevUV.x <= 1 && prevUV.y >= 0 && prevUV.y <= 1) {
        float prevAO = g_PrevAO.SampleLevel(g_PointClamp, prevUV, 0);
        denoised = lerp(denoised, prevAO, 0.9); // 90% temporal blend
    }

    g_AODenoised[DTid.xy] = denoised;
}
