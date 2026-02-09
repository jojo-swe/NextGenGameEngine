// ─── Screen-Space Ice/Frost Shader ───────────────────────────────────────
// Procedural frost crystallization effect for windows, surfaces, and
// camera lens frost. Simulates dendritic ice crystal growth patterns.
//
// Features:
//   - Voronoi-based crystal seed placement
//   - Dendritic branching via fractal noise
//   - Edge frost accumulation (cold surface boundaries)
//   - Refraction distortion through ice layer
//   - Frost thickness with specular highlights
//   - Temperature-driven growth animation
//   - Breath fog interaction (radial thaw)
//
// References:
//   - "Real-Time Procedural Frost" (Kim, GPU Gems 3)
//   - "Simulating Frost Formation" (Habel & Wimmer, 2009)
//   - "Frostbite Engine Frost Effects" (DICE, GDC 2018)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor   : register(t0);
Texture2D<float>  g_SceneDepth   : register(t1);
Texture2D<float4> g_SceneNormal  : register(t2);
Texture2D<float>  g_NoiseTex     : register(t3); // Tileable Perlin/simplex noise
Texture2D<float4> g_FrostPattern : register(t4); // Pre-baked frost crystal texture

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct FrostConstants {
    float2 resolution;
    float2 invResolution;
    float  time;
    float  temperature;       // -1=frozen, 0=thaw point, 1=warm (default -0.5)
    float  frostCoverage;     // 0=none, 1=full (driven by temperature)
    float  crystalScale;      // Voronoi cell size (default 0.05)
    float  branchingDetail;   // Dendritic fractal octaves (default 4)
    float  edgeFrostWidth;    // Edge accumulation width (default 0.02)
    float  refractionStrength;// Ice refraction amount (default 0.01)
    float  frostOpacity;      // Max frost opacity (default 0.85)
    float  specularPower;     // Ice specular highlight (default 64.0)
    float  specularIntensity; // Specular brightness (default 0.3)
    float3 frostColor;        // Frost tint (default: pale blue-white)
    float  thawCenterX;       // Breath/heat thaw center X (UV space)
    float  thawCenterY;       // Breath/heat thaw center Y
    float  thawRadius;        // Thaw radius (default 0.15)
    float  thawSoftness;      // Thaw edge softness (default 0.1)
    float  pad0;
};

[[vk::push_constant]] ConstantBuffer<FrostConstants> cb;

// ─── Voronoi Crystal Seeds ───────────────────────────────────────────────

float2 VoronoiHash(float2 p) {
    p = float2(dot(p, float2(127.1, 311.7)),
               dot(p, float2(269.5, 183.3)));
    return frac(sin(p) * 43758.5453);
}

struct VoronoiResult {
    float dist1;   // Distance to closest cell
    float dist2;   // Distance to second closest
    float2 cellId; // ID of closest cell
};

VoronoiResult Voronoi(float2 uv, float scale) {
    float2 p = uv * scale;
    float2 ip = floor(p);
    float2 fp = frac(p);

    VoronoiResult result;
    result.dist1 = 100.0;
    result.dist2 = 100.0;
    result.cellId = float2(0, 0);

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float2 neighbor = float2(x, y);
            float2 point = VoronoiHash(ip + neighbor);

            // Animate crystal seeds slightly
            point = 0.5 + 0.5 * sin(cb.time * 0.1 + 6.2831 * point);

            float2 diff = neighbor + point - fp;
            float dist = length(diff);

            if (dist < result.dist1) {
                result.dist2 = result.dist1;
                result.dist1 = dist;
                result.cellId = ip + neighbor;
            } else if (dist < result.dist2) {
                result.dist2 = dist;
            }
        }
    }

    return result;
}

// ─── Dendritic Branching Pattern ─────────────────────────────────────────
// Fractal noise that creates tree-like branching crystal structures.

float FractalNoise(float2 uv, float octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (float i = 0; i < octaves; i += 1.0) {
        float n = g_NoiseTex.SampleLevel(g_LinearWrap, uv * frequency, 0);
        value += n * amplitude;
        amplitude *= 0.5;
        frequency *= 2.17; // Non-power-of-2 to reduce grid artifacts
    }

    return value;
}

float DendriticPattern(float2 uv) {
    VoronoiResult vor = Voronoi(uv, 1.0 / cb.crystalScale);

    // Cell edge pattern (crystal boundaries)
    float edge = vor.dist2 - vor.dist1;

    // Branching noise along crystal growth direction
    float branch = FractalNoise(uv * 15.0 + vor.cellId * 3.7, cb.branchingDetail);

    // Combine: thin crystals radiating from cell centers
    float crystal = smoothstep(0.0, 0.15, edge) * branch;

    // Add fine dendritic detail
    float fineDetail = FractalNoise(uv * 40.0, 2.0);
    crystal += fineDetail * 0.3 * smoothstep(0.0, 0.1, edge);

    return saturate(crystal);
}

// ─── Edge Frost Accumulation ─────────────────────────────────────────────
// Frost accumulates at surface edges (depth discontinuities, normals).

float EdgeFrostMask(float2 uv) {
    float2 texel = cb.invResolution;

    // Depth edge detection (Sobel)
    float d00 = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2(-1, -1) * texel, 0);
    float d10 = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2( 0, -1) * texel, 0);
    float d20 = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2( 1, -1) * texel, 0);
    float d01 = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2(-1,  0) * texel, 0);
    float d21 = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2( 1,  0) * texel, 0);
    float d02 = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2(-1,  1) * texel, 0);
    float d12 = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2( 0,  1) * texel, 0);
    float d22 = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2( 1,  1) * texel, 0);

    float gx = -d00 - 2.0*d01 - d02 + d20 + 2.0*d21 + d22;
    float gy = -d00 - 2.0*d10 - d20 + d02 + 2.0*d12 + d22;
    float depthEdge = sqrt(gx*gx + gy*gy);

    // Normal edge detection
    float3 n0 = g_SceneNormal.SampleLevel(g_LinearClamp, uv, 0).rgb * 2.0 - 1.0;
    float3 nR = g_SceneNormal.SampleLevel(g_LinearClamp, uv + float2(texel.x, 0), 0).rgb * 2.0 - 1.0;
    float3 nU = g_SceneNormal.SampleLevel(g_LinearClamp, uv + float2(0, texel.y), 0).rgb * 2.0 - 1.0;
    float normalEdge = 1.0 - min(dot(n0, nR), dot(n0, nU));

    float edge = saturate(depthEdge * 50.0 + normalEdge * 2.0);
    return smoothstep(0.0, cb.edgeFrostWidth, edge);
}

// ─── Thaw Zone ───────────────────────────────────────────────────────────
// Radial warm zone (breath, heat source) that melts frost.

float ThawMask(float2 uv) {
    float2 center = float2(cb.thawCenterX, cb.thawCenterY);
    float dist = length(uv - center);
    return smoothstep(cb.thawRadius, cb.thawRadius + cb.thawSoftness, dist);
}

// ─── Frost Specular ──────────────────────────────────────────────────────

float FrostSpecular(float2 uv, float frostHeight) {
    // Approximate light direction (top-right)
    float2 lightDir = normalize(float2(0.7, -0.5));

    // Gradient of frost height for micro-normal
    float2 texel = cb.invResolution;
    float hR = DendriticPattern(uv + float2(texel.x, 0));
    float hU = DendriticPattern(uv + float2(0, texel.y));

    float2 grad = float2(hR - frostHeight, hU - frostHeight) / texel;
    float NdotL = saturate(dot(normalize(float3(-grad, 1.0)), normalize(float3(lightDir, 1.0))));

    return pow(NdotL, cb.specularPower) * cb.specularIntensity;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    // Coverage driven by temperature
    float coverage = saturate(cb.frostCoverage + (-cb.temperature) * 0.5);
    if (coverage < 0.01) {
        g_Output[DTid.xy] = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);
        return;
    }

    // Crystal pattern
    float crystal = DendriticPattern(uv);

    // Edge accumulation
    float edgeFrost = EdgeFrostMask(uv);

    // Pre-baked frost texture for extra detail
    float4 bakedFrost = g_FrostPattern.SampleLevel(g_LinearWrap, uv * 8.0, 0);

    // Combined frost mask
    float frostMask = saturate(crystal + edgeFrost * 0.5 + bakedFrost.r * 0.3);
    frostMask *= coverage;

    // Apply thaw zone
    float thaw = ThawMask(uv);
    frostMask *= thaw;

    frostMask = saturate(frostMask);

    // Refraction offset through ice
    float2 refrOffset = float2(
        DendriticPattern(uv + float2(0.01, 0)) - DendriticPattern(uv - float2(0.01, 0)),
        DendriticPattern(uv + float2(0, 0.01)) - DendriticPattern(uv - float2(0, 0.01))
    ) * cb.refractionStrength * frostMask;

    float2 refractedUV = uv + refrOffset;
    refractedUV = clamp(refractedUV, 0.0, 1.0);

    // Sample scene with refraction
    float3 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, refractedUV, 0).rgb;

    // Frost color with slight blue tint
    float3 frostTint = cb.frostColor;

    // Specular highlight on frost surface
    float spec = FrostSpecular(uv, crystal) * frostMask;

    // Blend frost over scene
    float opacity = frostMask * cb.frostOpacity;
    float3 finalColor = lerp(sceneColor, frostTint, opacity);
    finalColor += float3(1, 1, 1) * spec;

    // Slight desaturation in frosted areas
    float luma = dot(finalColor, float3(0.299, 0.587, 0.114));
    finalColor = lerp(finalColor, float3(luma, luma, luma), opacity * 0.3);

    g_Output[DTid.xy] = float4(finalColor, 1.0);
}
