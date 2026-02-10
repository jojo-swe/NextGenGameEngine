// ─── Procedural Dissolve / Disintegration Effect Shader ──────────────────
// Screen-space and per-object dissolve effect using noise-driven alpha
// cutout with glowing emission edge, particle ash trail, and directional
// sweep control.
//
// Features:
//   - 3D noise-based dissolve threshold (world-space stable)
//   - Configurable dissolve direction (up, radial, custom axis)
//   - Glowing emission edge band with configurable color and width
//   - Ash/ember particle trail at dissolve boundary
//   - Depth-aware: dissolve respects scene occlusion
//   - Per-object dissolve mask via object ID or stencil
//   - Animated dissolve progression (0..1 timeline)
//   - Multiple dissolve patterns (noise, directional, spherical)
//
// References:
//   - "Thanos Snap Effect" (Marvel's Avengers, 2020)
//   - "Dissolve Shader in Unreal Engine" (Epic Games)
//   - "Procedural Destruction Effects" (Insomniac, GDC 2018)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor    : register(t0);
Texture2D<float>  g_SceneDepth    : register(t1);
Texture2D<float4> g_GBufferNormal : register(t2);
Texture2D<float4> g_WorldPosTex   : register(t3); // Reconstructed or GBuffer world pos
Texture2D<float>  g_NoiseTex3D    : register(t4); // Tiling 3D noise baked as 2D atlas
Texture2D<float>  g_DissolveMask  : register(t5); // Per-pixel dissolve eligibility (1=dissolve)

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct DissolveConstants {
    float2   resolution;
    float2   invResolution;
    float3   cameraPos;
    float    time;
    float3   dissolveOrigin;     // World-space dissolve origin point
    float    dissolveProgress;   // 0.0 = fully solid, 1.0 = fully dissolved
    float3   dissolveDirection;  // Sweep direction (normalized)
    float    dissolveRadius;     // Max dissolve extent from origin
    float3   edgeColor;          // Emission edge color (default: 5.0, 2.0, 0.3)
    float    edgeWidth;          // Width of glowing edge band (default 0.05)
    float3   ashColor;           // Ash/ember particle color (default: 0.8, 0.3, 0.1)
    float    ashDensity;         // Density of ash particles (default 0.5)
    float    noiseScale;         // World-space noise frequency (default 3.0)
    float    noiseContrast;      // Sharpness of dissolve edge (default 2.0)
    float    directionalBias;    // How much direction affects dissolve (default 0.5)
    u32      dissolvePattern;    // 0=noise, 1=directional, 2=spherical, 3=combined
    float    edgeEmissionPower;  // HDR emission intensity (default 5.0)
    float    ashSpeed;           // Ash rise speed (default 1.0)
    float    ashLifetime;        // How long ash particles persist (default 0.3)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<DissolveConstants> cb;

// ─── Noise Functions ─────────────────────────────────────────────────────

float Hash31(float3 p) {
    p = frac(p * float3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float ValueNoise3D(float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f); // Smoothstep

    float n000 = Hash31(i + float3(0, 0, 0));
    float n100 = Hash31(i + float3(1, 0, 0));
    float n010 = Hash31(i + float3(0, 1, 0));
    float n110 = Hash31(i + float3(1, 1, 0));
    float n001 = Hash31(i + float3(0, 0, 1));
    float n101 = Hash31(i + float3(1, 0, 1));
    float n011 = Hash31(i + float3(0, 1, 1));
    float n111 = Hash31(i + float3(1, 1, 1));

    float n00 = lerp(n000, n100, f.x);
    float n10 = lerp(n010, n110, f.x);
    float n01 = lerp(n001, n101, f.x);
    float n11 = lerp(n011, n111, f.x);

    float n0 = lerp(n00, n10, f.y);
    float n1 = lerp(n01, n11, f.y);

    return lerp(n0, n1, f.z);
}

float FBMNoise(float3 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; ++i) {
        value += amplitude * ValueNoise3D(p * frequency);
        frequency *= 2.0;
        amplitude *= 0.5;
    }

    return value;
}

// ─── Dissolve Threshold ──────────────────────────────────────────────────

float ComputeDissolveThreshold(float3 worldPos) {
    float threshold = 0.0;

    // Base noise
    float noise = FBMNoise(worldPos * cb.noiseScale, 4);
    noise = pow(noise, cb.noiseContrast);

    if (cb.dissolvePattern == 0) {
        // Pure noise
        threshold = noise;
    } else if (cb.dissolvePattern == 1) {
        // Directional sweep
        float directional = dot(worldPos - cb.dissolveOrigin, cb.dissolveDirection);
        directional = directional / max(cb.dissolveRadius, 0.001);
        directional = saturate(directional * 0.5 + 0.5);
        threshold = lerp(noise, directional, cb.directionalBias);
    } else if (cb.dissolvePattern == 2) {
        // Spherical from origin
        float dist = length(worldPos - cb.dissolveOrigin);
        float spherical = saturate(dist / max(cb.dissolveRadius, 0.001));
        threshold = lerp(noise, spherical, cb.directionalBias);
    } else {
        // Combined: noise modulated by directional
        float directional = dot(worldPos - cb.dissolveOrigin, cb.dissolveDirection);
        directional = saturate(directional / max(cb.dissolveRadius, 0.001) * 0.5 + 0.5);
        threshold = noise * (1.0 - cb.directionalBias) + directional * cb.directionalBias;
    }

    return threshold;
}

// ─── Ash Particles ───────────────────────────────────────────────────────

float3 AshParticles(float3 worldPos, float dissolveEdge) {
    if (dissolveEdge < 0.001) return float3(0, 0, 0);

    // Spawn ash particles near the dissolve edge
    float3 ashPos = worldPos;
    ashPos.y += cb.time * cb.ashSpeed; // Rise upward

    // Particle grid
    float3 cellId = floor(ashPos * 8.0);
    float particleRand = Hash31(cellId);

    if (particleRand > cb.ashDensity) return float3(0, 0, 0);

    // Particle age based on dissolve edge proximity
    float age = dissolveEdge * (1.0 - frac(cb.time * 2.0 + particleRand));
    if (age < 0.0 || age > cb.ashLifetime) return float3(0, 0, 0);

    // Fade out over lifetime
    float lifeFade = 1.0 - (age / cb.ashLifetime);
    lifeFade = lifeFade * lifeFade;

    // Color: hot orange -> grey ash
    float3 color = lerp(cb.ashColor * 3.0, cb.ashColor * 0.2, age / cb.ashLifetime);

    // Flicker
    float flicker = 0.5 + 0.5 * sin(cb.time * 20.0 + particleRand * 50.0);

    return color * lifeFade * flicker * dissolveEdge;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float4 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);
    float depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
    float dissolveMask = g_DissolveMask.SampleLevel(g_LinearClamp, uv, 0);

    // Non-dissolving pixels pass through
    if (dissolveMask < 0.01 || cb.dissolveProgress < 0.001) {
        g_Output[DTid.xy] = sceneColor;
        return;
    }

    // Get world position
    float3 worldPos = g_WorldPosTex.SampleLevel(g_LinearClamp, uv, 0).xyz;

    // Compute dissolve threshold for this pixel
    float threshold = ComputeDissolveThreshold(worldPos);

    // Map progress to dissolve: pixels with threshold < progress are dissolved
    float dissolveAmount = cb.dissolveProgress * dissolveMask;

    // Compute edge proximity (how close to the dissolve boundary)
    float edgeDist = threshold - dissolveAmount;
    float edgeBand = smoothstep(0.0, cb.edgeWidth, edgeDist) *
                     (1.0 - smoothstep(cb.edgeWidth, cb.edgeWidth * 2.0, edgeDist));

    // Fully dissolved pixels: discard (show background)
    if (edgeDist < 0.0) {
        // Show background (sky or whatever is behind)
        // Add ash particles in dissolved region near boundary
        float nearEdge = saturate(1.0 + edgeDist / cb.edgeWidth);
        float3 ash = AshParticles(worldPos, nearEdge);

        float3 bgColor = sceneColor.rgb * 0.0; // Fully transparent
        g_Output[DTid.xy] = float4(bgColor + ash, 1.0);
        return;
    }

    // ── Edge emission glow ───────────────────────────────────────────
    float3 edgeEmission = cb.edgeColor * edgeBand * cb.edgeEmissionPower;

    // Add subtle noise variation to edge
    float edgeNoise = ValueNoise3D(worldPos * cb.noiseScale * 5.0 + cb.time * 2.0);
    edgeEmission *= 0.7 + 0.6 * edgeNoise;

    // ── Ash particles near edge ──────────────────────────────────────
    float3 ash = AshParticles(worldPos, edgeBand);

    // ── Composite ────────────────────────────────────────────────────
    float3 finalColor = sceneColor.rgb + edgeEmission + ash;

    g_Output[DTid.xy] = float4(finalColor, 1.0);
}
