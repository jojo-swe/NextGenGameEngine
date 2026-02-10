// ─── Procedural Volumetric Light Shaft (God Ray) Shader ──────────────────
// Screen-space radial blur post-process for volumetric light shafts
// emanating from bright light sources. Simulates atmospheric scattering
// with configurable density, decay, and color.
//
// Features:
//   - Radial blur from light screen-space position
//   - Multi-sample ray marching with configurable quality
//   - Exponential decay along ray distance
//   - Occlusion-aware: uses depth buffer to mask occluders
//   - Multiple light source support (up to 4)
//   - Chromatic light shaft tinting per source
//   - Animated dust mote noise overlay
//   - Density modulation by depth (thicker near camera)
//   - Configurable exposure and weight controls
//   - Additive blending with scene color
//
// References:
//   - "Volumetric Light Scattering as a Post-Process" (GPU Gems 3, Ch. 13)
//   - "Crepuscular Rays" (Mitchell, SIGGRAPH 2007)
//   - "God Rays in CryEngine" (Crytek, GDC 2010)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor : register(t0);
Texture2D<float>  g_SceneDepth : register(t1);
Texture2D<float>  g_NoiseTex   : register(t2);

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct LightShaftSource {
    float2 screenPos;     // Light position in screen UV space
    float  intensity;     // Light brightness
    float  radius;        // Influence radius in UV space
    float3 color;         // Light shaft color tint
    float  pad;
};

struct VolumetricLightShaftConstants {
    float2   resolution;
    float2   invResolution;
    float    time;
    u32      sampleCount;          // Ray march samples (default 64)
    float    density;              // Atmospheric density (default 1.0)
    float    decay;                // Exponential decay per sample (default 0.97)
    float    weight;               // Per-sample contribution weight (default 0.02)
    float    exposure;             // Final exposure multiplier (default 1.0)
    float    depthThreshold;       // Depth cutoff for occluder masking (default 0.99)
    float    noiseAmount;          // Dust mote noise intensity (default 0.1)
    float    noiseScale;           // Noise texture tiling (default 4.0)
    float    noiseSpeed;           // Noise animation speed (default 0.5)
    float    depthDensityScale;    // Density increase near camera (default 0.5)
    u32      lightCount;           // Active light sources (1-4)
    float    pad0;
    float    pad1;
};

[[vk::push_constant]] ConstantBuffer<VolumetricLightShaftConstants> cb;

StructuredBuffer<LightShaftSource> g_Lights : register(t3);

// ─── Noise ───────────────────────────────────────────────────────────────

float SampleNoise(float2 uv) {
    float2 noiseUV = uv * cb.noiseScale + float2(cb.time * cb.noiseSpeed, cb.time * cb.noiseSpeed * 0.7);
    return g_NoiseTex.SampleLevel(g_LinearWrap, noiseUV, 0);
}

// ─── Radial Blur Ray March ───────────────────────────────────────────────

float3 ComputeLightShaft(float2 uv, float2 lightScreenPos, float3 lightColor, float lightIntensity, float lightRadius) {
    float2 deltaUV = uv - lightScreenPos;
    float dist = length(deltaUV);

    // Early out if outside influence radius
    if (dist > lightRadius) return float3(0, 0, 0);

    // Radial falloff
    float radialFalloff = 1.0 - smoothstep(lightRadius * 0.5, lightRadius, dist);

    // Ray direction from pixel toward light
    float2 rayDir = lightScreenPos - uv;
    float2 rayStep = rayDir / float(cb.sampleCount);

    float3 accumColor = float3(0, 0, 0);
    float illuminationDecay = 1.0;

    float2 sampleUV = uv;

    for (u32 i = 0; i < cb.sampleCount; ++i) {
        sampleUV += rayStep;

        // Clamp to screen
        float2 clampedUV = clamp(sampleUV, 0.001, 0.999);

        // Sample scene color at ray position
        float3 sampleColor = g_SceneColor.SampleLevel(g_LinearClamp, clampedUV, 0).rgb;

        // Depth-based occlusion: only accumulate from unoccluded (sky/bright) regions
        float sampleDepth = g_SceneDepth.SampleLevel(g_LinearClamp, clampedUV, 0);
        float occlusionMask = step(cb.depthThreshold, sampleDepth); // 1.0 for sky/far

        // Depth-based density: thicker near camera
        float depthDensity = 1.0 + (1.0 - sampleDepth) * cb.depthDensityScale;

        // Accumulate
        sampleColor *= occlusionMask * cb.weight * illuminationDecay * depthDensity;
        accumColor += sampleColor;

        // Decay
        illuminationDecay *= cb.decay;
    }

    // Apply light color and intensity
    accumColor *= lightColor * lightIntensity * cb.density * radialFalloff;

    return accumColor;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float4 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);

    // ── Accumulate light shafts from all sources ─────────────────────
    float3 totalShafts = float3(0, 0, 0);

    u32 lightCount = min(cb.lightCount, 4u);
    for (u32 i = 0; i < lightCount; ++i) {
        LightShaftSource light = g_Lights[i];
        totalShafts += ComputeLightShaft(uv, light.screenPos, light.color, light.intensity, light.radius);
    }

    // ── Dust mote noise overlay ──────────────────────────────────────
    if (cb.noiseAmount > 0.0) {
        float noise = SampleNoise(uv);
        float noiseMod = lerp(1.0, noise, cb.noiseAmount);
        totalShafts *= noiseMod;
    }

    // ── Exposure ─────────────────────────────────────────────────────
    totalShafts *= cb.exposure;

    // ── Additive blend with scene ────────────────────────────────────
    float3 result = sceneColor.rgb + totalShafts;

    g_Output[DTid.xy] = float4(result, 1.0);
}
