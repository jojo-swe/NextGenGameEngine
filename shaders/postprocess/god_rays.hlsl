// ─── Volumetric Light Scattering (God Rays) ──────────────────────────────
// Screen-space radial blur from light source position to simulate
// volumetric scattering through participating media (dust, fog, haze).
//
// Algorithm (GPU Gems 3, Chapter 13):
//   1. Render occluders to a mask (light source visible = white, occluded = black)
//   2. For each pixel, march toward the light source in screen space
//   3. Accumulate and attenuate samples along the ray
//   4. Composite with scene color using additive blend
//
// This shader performs step 2-3 as a compute pass.

#include "../common/math.hlsl"

Texture2D<float4> g_OcclusionMask : register(t0); // Light source visibility mask
Texture2D<float>  g_DepthBuffer   : register(t1);
RWTexture2D<float4> g_Output      : register(u0);

SamplerState g_LinearClamp : register(s0);

struct GodRayConstants {
    float2 lightScreenPos;    // Light source position in UV space [0,1]
    float2 resolution;
    float2 invResolution;
    float  density;           // Ray density (default 1.0)
    float  weight;            // Sample weight (default 0.01)
    float  decay;             // Per-step decay (default 0.97)
    float  exposure;          // Final exposure multiplier (default 1.0)
    uint   numSamples;        // Ray march samples (default 64)
    float  maxRayLength;      // Maximum ray length in UV space (default 1.0)
    float3 lightColor;        // Color tint for the rays
    float  intensityThreshold; // Skip pixels below this intensity
};

[[vk::push_constant]] ConstantBuffer<GodRayConstants> cb;

// ─── Main God Ray Pass ───────────────────────────────────────────────────
[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    // Direction from pixel toward light source
    float2 deltaUV = cb.lightScreenPos - uv;
    float rayLength = length(deltaUV);

    // Clamp ray length
    rayLength = min(rayLength, cb.maxRayLength);
    deltaUV = normalize(deltaUV) * rayLength;

    // Step size along the ray
    float2 stepUV = deltaUV * (1.0 / float(cb.numSamples)) * cb.density;

    float2 sampleUV = uv;
    float3 accumColor = float3(0, 0, 0);
    float illuminationDecay = 1.0;

    // March toward light source
    for (uint i = 0; i < cb.numSamples; ++i) {
        sampleUV += stepUV;

        // Clamp to screen bounds
        float2 clampedUV = saturate(sampleUV);

        // Sample the occlusion mask
        float4 occSample = g_OcclusionMask.SampleLevel(g_LinearClamp, clampedUV, 0);
        float lightIntensity = max(occSample.r, max(occSample.g, occSample.b));

        // Skip low-intensity samples for performance
        if (lightIntensity > cb.intensityThreshold) {
            accumColor += occSample.rgb * illuminationDecay * cb.weight;
        }

        // Decay the intensity along the ray
        illuminationDecay *= cb.decay;

        // Early out if decay is negligible
        if (illuminationDecay < 0.001) break;
    }

    // Apply exposure and light color tint
    accumColor *= cb.exposure * cb.lightColor;

    g_Output[DTid.xy] = float4(accumColor, 1.0);
}

// ─── Radial Blur Quality Pass (Higher Quality) ──────────────────────────
// Uses dithered sampling offset for less banding at lower sample counts.

[numthreads(8, 8, 1)]
void CSMainDithered(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float2 deltaUV = cb.lightScreenPos - uv;
    float rayLength = min(length(deltaUV), cb.maxRayLength);
    deltaUV = normalize(deltaUV) * rayLength;

    float2 stepUV = deltaUV * (1.0 / float(cb.numSamples)) * cb.density;

    // Interleaved gradient noise for dithered offset
    float2 noiseInput = float2(DTid.xy);
    float noise = frac(52.9829189 * frac(dot(noiseInput, float2(0.06711056, 0.00583715))));
    float2 sampleUV = uv + stepUV * noise;

    float3 accumColor = float3(0, 0, 0);
    float illuminationDecay = 1.0;

    for (uint i = 0; i < cb.numSamples; ++i) {
        sampleUV += stepUV;
        float2 clampedUV = saturate(sampleUV);

        float4 occSample = g_OcclusionMask.SampleLevel(g_LinearClamp, clampedUV, 0);
        accumColor += occSample.rgb * illuminationDecay * cb.weight;
        illuminationDecay *= cb.decay;

        if (illuminationDecay < 0.001) break;
    }

    accumColor *= cb.exposure * cb.lightColor;
    g_Output[DTid.xy] = float4(accumColor, 1.0);
}

// ─── Composite Pass ──────────────────────────────────────────────────────
// Additively blends god rays with the scene color.

Texture2D<float4> g_SceneColor   : register(t2);
Texture2D<float4> g_GodRays      : register(t3);
RWTexture2D<float4> g_Composited : register(u1);

[numthreads(8, 8, 1)]
void CSComposite(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float4 scene = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);
    float4 rays = g_GodRays.SampleLevel(g_LinearClamp, uv, 0);

    // Additive blend
    g_Composited[DTid.xy] = float4(scene.rgb + rays.rgb, scene.a);
}
