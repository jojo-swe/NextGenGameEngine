// ─── Screen-Space Contact Shadows ─────────────────────────────────────────
// Ray-marches in screen space from each pixel toward the light source
// to detect small-scale shadowing that shadow maps miss (e.g., contact
// shadows under objects, fine geometry detail).
//
// Runs as a compute shader on the depth buffer after the main shadow pass.
// Output is a single-channel mask: 0 = shadowed, 1 = lit.

#include "../common/math.hlsl"

struct ContactShadowConstants {
    float4x4 viewProj;
    float4x4 invViewProj;
    float4   cameraPos;
    float4   lightDirection;   // Normalized, points toward light
    float2   screenSize;
    float    maxRayLength;     // Screen-space ray length (pixels)
    float    thickness;        // Depth comparison threshold
    float    stepSize;         // Ray step in pixels
    uint     maxSteps;
    float    fadeStart;        // Distance from camera where shadows start fading
    float    fadeEnd;          // Distance from camera where shadows fully fade
};

[[vk::push_constant]] ConstantBuffer<ContactShadowConstants> pc;

Texture2D<float>    g_DepthBuffer : register(t0, space48);
RWTexture2D<float>  g_ShadowMask : register(u0, space48);
SamplerState        g_PointClamp : register(s0, space48);

// ─── Depth to View-Space Position ────────────────────────────────────────

float3 ReconstructWorldPos(float2 uv, float depth) {
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;
    float4 worldPos = mul(pc.invViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}

float2 WorldToScreen(float3 worldPos) {
    float4 clipPos = mul(pc.viewProj, float4(worldPos, 1.0));
    float2 ndc = clipPos.xy / clipPos.w;
    ndc.y = -ndc.y;
    return (ndc * 0.5 + 0.5) * pc.screenSize;
}

// ─── Main ────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= uint(pc.screenSize.x) || DTid.y >= uint(pc.screenSize.y)) {
        return;
    }

    float2 pixelCoord = float2(DTid.xy) + 0.5;
    float2 uv = pixelCoord / pc.screenSize;

    float depth = g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0);

    // Skip sky pixels (reverse-Z: sky = 0)
    if (depth <= 0.0001) {
        g_ShadowMask[DTid.xy] = 1.0;
        return;
    }

    float3 worldPos = ReconstructWorldPos(uv, depth);

    // Distance fade
    float cameraDist = length(worldPos - pc.cameraPos.xyz);
    float fadeFactor = 1.0 - saturate((cameraDist - pc.fadeStart) / (pc.fadeEnd - pc.fadeStart));
    if (fadeFactor <= 0.0) {
        g_ShadowMask[DTid.xy] = 1.0;
        return;
    }

    // Ray march toward light in screen space
    float3 lightDir = normalize(pc.lightDirection.xyz);
    float3 rayEnd = worldPos + lightDir * pc.maxRayLength;

    float2 startScreen = pixelCoord;
    float2 endScreen = WorldToScreen(rayEnd);

    float2 rayDir = endScreen - startScreen;
    float rayLength = length(rayDir);

    if (rayLength < 1.0) {
        g_ShadowMask[DTid.xy] = 1.0;
        return;
    }

    float2 stepDir = normalize(rayDir) * pc.stepSize;
    uint stepCount = min(uint(rayLength / pc.stepSize), pc.maxSteps);

    float shadow = 1.0;
    float2 samplePos = startScreen;

    // Dithered start offset to reduce banding
    float dither = frac(sin(dot(float2(DTid.xy), float2(12.9898, 78.233))) * 43758.5453);
    samplePos += stepDir * dither;

    for (uint i = 0; i < stepCount; ++i) {
        samplePos += stepDir;

        float2 sampleUV = samplePos / pc.screenSize;
        if (sampleUV.x < 0 || sampleUV.x > 1 || sampleUV.y < 0 || sampleUV.y > 1) break;

        float sampleDepth = g_DepthBuffer.SampleLevel(g_PointClamp, sampleUV, 0);
        float3 sampleWorldPos = ReconstructWorldPos(sampleUV, sampleDepth);

        // Interpolate expected position along the ray
        float t = float(i + 1) / float(stepCount);
        float3 expectedPos = lerp(worldPos, rayEnd, t);

        // Compare depths: is the sample closer than expected? (occluder found)
        float depthDiff = length(expectedPos - pc.cameraPos.xyz) - length(sampleWorldPos - pc.cameraPos.xyz);

        if (depthDiff > 0.0 && depthDiff < pc.thickness) {
            // Fade shadow at ray extremes for smooth edges
            float edgeFade = 1.0 - abs(2.0 * t - 1.0);
            shadow = min(shadow, 1.0 - edgeFade);
        }
    }

    // Apply distance fade
    shadow = lerp(1.0, shadow, fadeFactor);

    g_ShadowMask[DTid.xy] = shadow;
}
