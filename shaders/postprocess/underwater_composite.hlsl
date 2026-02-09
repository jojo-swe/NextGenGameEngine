// ─── Underwater Caustics + God Rays Composite ────────────────────────────
// Full-screen post-process combining underwater caustics projection
// with volumetric light shafts (god rays) for submerged camera views.
//
// Features:
//   - Projected caustics from animated Voronoi pattern
//   - Volumetric god rays via radial blur from sun screen position
//   - Depth-based fog with exponential absorption (Beer-Lambert)
//   - Color absorption: red attenuates first, then green, blue last
//   - Surface refraction distortion at water-air boundary
//   - Underwater particle scattering approximation
//
// References:
//   - "Real-Time Caustics" (Wyman, I3D 2005)
//   - "GPU Gems 3: Volumetric Light Scattering" (Hoffman, 2008)
//   - "Assassin's Creed IV: Underwater Rendering" (Ubisoft, 2013)
//   - "Subnautica Rendering Techniques" (Unknown Worlds, GDC 2018)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor   : register(t0);
Texture2D<float>  g_SceneDepth   : register(t1);
Texture2D<float4> g_SceneNormal  : register(t2);
Texture2D<float>  g_NoiseTex     : register(t3);

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct UnderwaterConstants {
    float4x4 invViewProj;
    float4x4 viewProj;
    float2   resolution;
    float2   invResolution;
    float3   cameraPos;
    float    time;
    float3   sunDirection;       // World-space sun direction
    float    sunIntensity;       // God ray intensity (default 0.8)
    float2   sunScreenPos;       // Sun position in UV space for radial blur
    float    waterSurfaceY;      // World Y of water surface
    float    waterDepthScale;    // Depth fog scale (default 0.05)
    float3   shallowColor;      // Shallow water tint (default: teal)
    float    causticsScale;     // Caustics UV scale (default 2.0)
    float3   deepColor;         // Deep water tint (default: dark blue)
    float    causticsIntensity; // Caustics brightness (default 0.5)
    float3   absorptionCoeffs;  // RGB absorption (default: 0.4, 0.08, 0.02)
    float    godRayDecay;       // Radial blur falloff (default 0.96)
    float    godRayDensity;     // Light shaft density (default 0.5)
    float    godRaySamples;     // Radial blur steps (default 64)
    float    godRayWeight;      // Per-sample weight (default 0.01)
    float    scatterAmount;     // Underwater scatter (default 0.1)
    float    refractionStrength;// Surface refraction (default 0.03)
    float    maxVisibility;     // Max visible distance (default 50.0)
    float    pad0;
    float    pad1;
};

[[vk::push_constant]] ConstantBuffer<UnderwaterConstants> cb;

// ─── World Position Reconstruction ───────────────────────────────────────

float3 ReconstructWorldPos(float2 uv, float depth) {
    float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    clip.y = -clip.y;
    float4 world = mul(cb.invViewProj, clip);
    return world.xyz / world.w;
}

// ─── Caustics Pattern ────────────────────────────────────────────────────
// Animated Voronoi-based caustics projected onto underwater surfaces.

float2 CausticsHash(float2 p) {
    p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
    return frac(sin(p) * 43758.5453);
}

float CausticsVoronoi(float2 uv, float t) {
    float2 p = uv * cb.causticsScale;
    float2 ip = floor(p);
    float2 fp = frac(p);

    float d1 = 100.0;
    float d2 = 100.0;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float2 neighbor = float2(x, y);
            float2 point = CausticsHash(ip + neighbor);

            // Animate caustics: circular motion
            point = 0.5 + 0.4 * sin(t * 0.8 + 6.2831 * point);

            float2 diff = neighbor + point - fp;
            float dist = length(diff);

            if (dist < d1) { d2 = d1; d1 = dist; }
            else if (dist < d2) { d2 = dist; }
        }
    }

    // Edge pattern (brighter at cell boundaries)
    float caustic = d2 - d1;
    caustic = pow(saturate(caustic * 3.0), 0.5);

    return caustic;
}

float CausticsPattern(float3 worldPos) {
    // Project caustics from above onto horizontal surfaces
    float2 projUV = worldPos.xz * 0.1;

    // Two layers with different speeds for shimmer
    float c1 = CausticsVoronoi(projUV, cb.time);
    float c2 = CausticsVoronoi(projUV * 1.3 + 0.7, cb.time * 1.3 + 2.0);

    // Blend layers
    float caustics = min(c1, c2);

    // Attenuate with depth below surface
    float depthBelowSurface = cb.waterSurfaceY - worldPos.y;
    float depthAtten = exp(-depthBelowSurface * 0.15);

    return caustics * depthAtten * cb.causticsIntensity;
}

// ─── God Rays (Volumetric Light Shafts) ──────────────────────────────────
// Radial blur from sun screen position (Crepuscular rays).

float3 GodRays(float2 uv) {
    float2 deltaUV = (uv - cb.sunScreenPos);
    float dist = length(deltaUV);
    deltaUV = deltaUV / max(dist, 0.001) * (1.0 / cb.godRaySamples);

    float2 sampleUV = uv;
    float illumination = 0.0;
    float decay = 1.0;

    uint numSamples = uint(cb.godRaySamples);
    for (uint i = 0; i < numSamples; ++i) {
        sampleUV -= deltaUV;
        float2 clampedUV = clamp(sampleUV, 0.0, 1.0);

        // Sample scene brightness at this point
        float3 sampleColor = g_SceneColor.SampleLevel(g_LinearClamp, clampedUV, 0).rgb;
        float brightness = dot(sampleColor, float3(0.299, 0.587, 0.114));

        // Depth test: only include samples that can "see" the sun
        float sampleDepth = g_SceneDepth.SampleLevel(g_LinearClamp, clampedUV, 0);
        float depthMask = sampleDepth > 0.999 ? 1.0 : 0.0; // Sky pixels

        illumination += brightness * depthMask * decay * cb.godRayWeight;
        decay *= cb.godRayDecay;
    }

    // Tint god rays with sun direction color (warm)
    float3 rayColor = float3(0.6, 0.8, 1.0) * illumination * cb.sunIntensity;

    // Fade rays with distance from sun
    float fadeDist = saturate(1.0 - dist * 0.5);
    rayColor *= fadeDist;

    return rayColor * cb.godRayDensity;
}

// ─── Beer-Lambert Absorption ─────────────────────────────────────────────

float3 WaterAbsorption(float3 color, float distance) {
    // Wavelength-dependent absorption: red > green > blue
    float3 absorption = exp(-cb.absorptionCoeffs * distance);
    return color * absorption;
}

// ─── Depth Fog ───────────────────────────────────────────────────────────

float3 UnderwaterFog(float3 color, float distance, float depthBelowSurface) {
    // Exponential fog
    float fogFactor = 1.0 - exp(-distance * cb.waterDepthScale);
    fogFactor = saturate(fogFactor);

    // Depth-dependent color (shallow = teal, deep = dark blue)
    float depthNorm = saturate(depthBelowSurface / cb.maxVisibility);
    float3 fogColor = lerp(cb.shallowColor, cb.deepColor, depthNorm);

    return lerp(color, fogColor, fogFactor);
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
    float3 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;

    // Reconstruct world position
    float3 worldPos = ReconstructWorldPos(uv, depth);

    // Check if pixel is underwater
    float depthBelowSurface = cb.waterSurfaceY - worldPos.y;
    bool isUnderwater = depthBelowSurface > 0.0 || cb.cameraPos.y < cb.waterSurfaceY;

    if (!isUnderwater) {
        g_Output[DTid.xy] = float4(sceneColor, 1.0);
        return;
    }

    float viewDistance = length(worldPos - cb.cameraPos);

    // ── Refraction distortion ────────────────────────────────────────
    float noise1 = g_NoiseTex.SampleLevel(g_LinearWrap, uv * 5.0 + cb.time * 0.1, 0);
    float noise2 = g_NoiseTex.SampleLevel(g_LinearWrap, uv * 7.0 - cb.time * 0.07, 0);
    float2 refrOffset = float2(noise1 - 0.5, noise2 - 0.5) * cb.refractionStrength;
    refrOffset *= saturate(1.0 - viewDistance / cb.maxVisibility); // Less distortion far away

    float2 refractedUV = clamp(uv + refrOffset, 0.0, 1.0);
    float3 refractedColor = g_SceneColor.SampleLevel(g_LinearClamp, refractedUV, 0).rgb;

    // ── Caustics ─────────────────────────────────────────────────────
    float caustics = CausticsPattern(worldPos);
    refractedColor += float3(1, 1, 1) * caustics;

    // ── Absorption ───────────────────────────────────────────────────
    float3 absorbed = WaterAbsorption(refractedColor, viewDistance);

    // ── Depth fog ────────────────────────────────────────────────────
    float3 fogged = UnderwaterFog(absorbed, viewDistance, depthBelowSurface);

    // ── God rays ─────────────────────────────────────────────────────
    float3 rays = GodRays(uv);
    // Attenuate rays with water depth
    float rayDepthAtten = exp(-depthBelowSurface * 0.1);
    rays *= rayDepthAtten;

    // ── Scatter ──────────────────────────────────────────────────────
    float scatter = cb.scatterAmount * saturate(viewDistance / cb.maxVisibility);
    float3 scatterColor = cb.shallowColor * scatter;

    // ── Final composite ──────────────────────────────────────────────
    float3 finalColor = fogged + rays + scatterColor;

    g_Output[DTid.xy] = float4(finalColor, 1.0);
}
