// ─── Lens Flare Compute Shader (Sprite-Based + Starburst) ───────────────
// Generates lens flare effects from bright light sources in the scene.
//
// Pipeline:
//   1. Bright pass: threshold scene color to find flare sources
//   2. Ghost pass: generate flare ghosts by sampling along flare line
//   3. Halo pass: radial halo around bright sources
//   4. Starburst: diffraction starburst pattern from aperture blades
//   5. Composite: blend all flare elements with scene
//
// Based on "Pseudo Lens Flare" (John Chapman 2013) and
// "Practical Real-Time Lens Flare Rendering" (NVIDIA)

#include "../common/math.hlsl"

Texture2D<float4>   g_SceneColor   : register(t0);
Texture2D<float4>   g_BrightPass   : register(t1); // Pre-thresholded bright pixels
Texture1D<float3>   g_ChromaLUT    : register(t2); // Chromatic aberration color LUT
Texture2D<float>    g_StarburstTex : register(t3); // Starburst pattern texture
Texture2D<float>    g_DirtMask     : register(t4); // Lens dirt texture

RWTexture2D<float4> g_Output       : register(u0);

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

struct LensFlareConstants {
    float2 resolution;
    float2 invResolution;
    float  ghostCount;        // Number of ghost reflections (default 8)
    float  ghostSpacing;      // Spacing between ghosts (default 0.3)
    float  ghostThreshold;    // Brightness threshold for ghosts (default 5.0)
    float  haloRadius;        // Halo ring radius (default 0.6)
    float  haloThickness;     // Halo ring thickness (default 0.1)
    float  haloIntensity;     // Halo brightness multiplier
    float  starburstIntensity;// Starburst brightness
    float  chromaticStrength; // Chromatic distortion strength (default 0.5)
    float  dirtIntensity;     // Lens dirt brightness
    float  globalIntensity;   // Overall flare intensity
    float  cameraRotation;    // Camera roll angle (for starburst rotation)
    float  pad0;
};

[[vk::push_constant]] ConstantBuffer<LensFlareConstants> cb;

// ─── Utility Functions ───────────────────────────────────────────────────

// Flip UV around center for ghost generation
float2 FlipUV(float2 uv) {
    return float2(1.0, 1.0) - uv;
}

// Chromatic distortion sampling
float3 ChromaticSample(Texture2D<float4> tex, SamplerState samp, float2 uv, float2 direction, float strength) {
    float3 color;
    color.r = tex.SampleLevel(samp, uv + direction * strength, 0).r;
    color.g = tex.SampleLevel(samp, uv, 0).g;
    color.b = tex.SampleLevel(samp, uv - direction * strength, 0).b;
    return color;
}

// Smooth vignette for ghost edges
float GhostWeight(float2 uv) {
    float2 centered = uv * 2.0 - 1.0;
    float d = length(centered);
    return 1.0 - smoothstep(0.7, 1.0, d);
}

// ─── Ghost Pass ──────────────────────────────────────────────────────────
// Generates lens ghosts by sampling bright pixels reflected through center.

[numthreads(8, 8, 1)]
void CSGhosts(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;
    float2 flippedUV = FlipUV(uv);

    // Direction from center to flipped UV (ghost line)
    float2 ghostDir = normalize(flippedUV - float2(0.5, 0.5));
    float2 chromaDir = ghostDir * cb.chromaticStrength * cb.invResolution;

    float3 totalColor = float3(0, 0, 0);

    // Generate ghosts along the flare line
    for (float i = 0; i < cb.ghostCount; i += 1.0) {
        float offset = i * cb.ghostSpacing;
        float2 sampleUV = lerp(flippedUV, float2(0.5, 0.5), offset);

        // Bounds check
        if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) continue;

        // Sample with chromatic aberration
        float3 ghostColor = ChromaticSample(g_BrightPass, g_LinearClamp, sampleUV, chromaDir, offset);

        // Apply ghost weight (fade toward edges)
        float weight = GhostWeight(sampleUV);

        // Color from chromatic LUT based on ghost index
        float lutCoord = i / max(cb.ghostCount - 1.0, 1.0);
        float3 chromaTint = g_ChromaLUT.SampleLevel(g_LinearClamp, lutCoord, 0);

        totalColor += ghostColor * weight * chromaTint;
    }

    g_Output[DTid.xy] = float4(totalColor, 1.0);
}

// ─── Halo Pass ───────────────────────────────────────────────────────────
// Generates a radial halo ring around bright areas.

RWTexture2D<float4> g_HaloOutput : register(u1);

[numthreads(8, 8, 1)]
void CSHalo(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;
    float2 centered = uv - float2(0.5, 0.5);
    float dist = length(centered);

    // Ring mask
    float ring = 1.0 - abs(dist - cb.haloRadius) / cb.haloThickness;
    ring = saturate(ring);
    ring = ring * ring; // Smooth falloff

    // Sample bright pass at the halo position
    float2 haloUV = float2(0.5, 0.5) + normalize(centered) * cb.haloRadius;
    haloUV = saturate(haloUV);

    float3 haloColor = g_BrightPass.SampleLevel(g_LinearClamp, haloUV, 0).rgb;
    haloColor *= ring * cb.haloIntensity;

    // Apply chromatic aberration to halo
    float2 chromaDir = normalize(centered) * cb.chromaticStrength * cb.invResolution;
    float3 chromaHalo;
    chromaHalo.r = g_BrightPass.SampleLevel(g_LinearClamp, haloUV + chromaDir, 0).r;
    chromaHalo.g = g_BrightPass.SampleLevel(g_LinearClamp, haloUV, 0).g;
    chromaHalo.b = g_BrightPass.SampleLevel(g_LinearClamp, haloUV - chromaDir, 0).b;

    haloColor = chromaHalo * ring * cb.haloIntensity;

    g_HaloOutput[DTid.xy] = float4(haloColor, 1.0);
}

// ─── Starburst Pass ─────────────────────────────────────────────────────
// Generates a diffraction starburst pattern from aperture blades.

RWTexture2D<float4> g_StarburstOutput : register(u2);

[numthreads(8, 8, 1)]
void CSStarburst(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;
    float2 centered = uv - float2(0.5, 0.5);

    // Rotate starburst with camera
    float cosR = cos(cb.cameraRotation);
    float sinR = sin(cb.cameraRotation);
    float2 rotated = float2(
        centered.x * cosR - centered.y * sinR,
        centered.x * sinR + centered.y * cosR
    );

    // Sample starburst texture
    float2 starUV = rotated * 0.5 + 0.5;
    float starburst = g_StarburstTex.SampleLevel(g_LinearWrap, starUV, 0);
    starburst = pow(starburst, 2.0); // Sharpen the pattern

    // Modulate by bright pass
    float3 brightColor = g_BrightPass.SampleLevel(g_LinearClamp, uv, 0).rgb;
    float brightness = dot(brightColor, float3(0.2126, 0.7152, 0.0722));

    float3 starColor = brightColor * starburst * cb.starburstIntensity * brightness;

    g_StarburstOutput[DTid.xy] = float4(starColor, 1.0);
}

// ─── Composite Pass ─────────────────────────────────────────────────────
// Combines all flare elements with the scene color.

Texture2D<float4> g_GhostResult     : register(t5);
Texture2D<float4> g_HaloResult      : register(t6);
Texture2D<float4> g_StarburstResult : register(t7);
RWTexture2D<float4> g_FinalOutput   : register(u3);

[numthreads(8, 8, 1)]
void CSComposite(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float4 scene = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);
    float3 ghosts = g_GhostResult.SampleLevel(g_LinearClamp, uv, 0).rgb;
    float3 halo = g_HaloResult.SampleLevel(g_LinearClamp, uv, 0).rgb;
    float3 starburst = g_StarburstResult.SampleLevel(g_LinearClamp, uv, 0).rgb;

    // Combine all flare elements
    float3 flare = ghosts + halo + starburst;

    // Apply lens dirt
    float dirt = g_DirtMask.SampleLevel(g_LinearClamp, uv, 0);
    flare *= (1.0 + dirt * cb.dirtIntensity);

    // Apply global intensity
    flare *= cb.globalIntensity;

    // Additive blend with scene
    g_FinalOutput[DTid.xy] = float4(scene.rgb + flare, scene.a);
}
