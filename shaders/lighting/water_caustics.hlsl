// ─── Screen-Space Water/Ocean Caustics Shader ────────────────────────────
// Projects animated caustic light patterns onto underwater surfaces.
// Two approaches:
//   1. Screen-space: project caustics onto GBuffer based on depth/position
//   2. Texture projection: animated caustic texture via light-space projection
//
// Caustic pattern generation uses dual-layer Voronoi noise with
// chromatic dispersion for realistic light refraction patterns.
//
// References:
//   - "Real-Time Rendering of Water Caustics" (Wyman, GPU Gems 1)
//   - "Water Rendering in Far Cry 5" (Ubisoft, GDC 2018)

#include "../common/math.hlsl"

Texture2D<float>  g_DepthBuffer   : register(t0);
Texture2D<float4> g_GBuffer0      : register(t1); // Albedo
Texture2D<float4> g_GBuffer1      : register(t2); // Normal + roughness
Texture2D<float4> g_CausticTex    : register(t3); // Pre-baked caustic pattern (optional)

RWTexture2D<float4> g_Output : register(u0);

SamplerState g_PointClamp  : register(s0);
SamplerState g_LinearWrap  : register(s1);

struct CausticConstants {
    float4x4 invViewProj;
    float4x4 lightViewProj;       // Light-space projection for caustic mapping
    float3   cameraPos;
    float    waterHeight;          // World-space Y of water plane
    float3   lightDir;             // Directional light direction
    float    lightIntensity;
    float3   lightColor;
    float    causticsIntensity;    // Overall brightness (default 1.0)
    float    causticsScale;        // UV scale for procedural (default 0.5)
    float    causticsSpeed;        // Animation speed (default 0.3)
    float    time;                 // Elapsed time in seconds
    float    chromaticDispersion;  // RGB offset for dispersion (default 0.02)
    float    depthFade;            // Fade with water depth (default 0.1)
    float    maxDepth;             // Max depth for caustics (default 20.0)
    float    edgeSoftness;         // Soft edge at water surface (default 0.5)
    uint     useProcedural;        // 1: procedural Voronoi, 0: texture projection
    float2   resolution;
    float2   invResolution;
};

[[vk::push_constant]] ConstantBuffer<CausticConstants> cb;

// ─── Hash / Noise Functions ──────────────────────────────────────────────

float2 Hash2(float2 p) {
    p = float2(dot(p, float2(127.1, 311.7)),
               dot(p, float2(269.5, 183.3)));
    return frac(sin(p) * 43758.5453);
}

// ─── Voronoi Noise (Cellular) ────────────────────────────────────────────
// Returns distance to nearest cell edge — creates caustic-like patterns

float VoronoiCaustic(float2 uv, float time) {
    float2 cell = floor(uv);
    float2 frac_uv = frac(uv);

    float minDist1 = 1e10;
    float minDist2 = 1e10;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float2 neighbor = float2(x, y);
            float2 point = Hash2(cell + neighbor);

            // Animate points
            point = 0.5 + 0.5 * sin(time * 0.8 + 6.2831 * point);

            float2 diff = neighbor + point - frac_uv;
            float dist = dot(diff, diff);

            if (dist < minDist1) {
                minDist2 = minDist1;
                minDist1 = dist;
            } else if (dist < minDist2) {
                minDist2 = dist;
            }
        }
    }

    // Edge distance creates caustic lines
    return sqrt(minDist2) - sqrt(minDist1);
}

// ─── Dual-Layer Caustic Pattern ──────────────────────────────────────────
// Two layers at different scales and speeds for complex patterns

float CausticPattern(float2 uv, float time) {
    float layer1 = VoronoiCaustic(uv * 1.0, time * 1.0);
    float layer2 = VoronoiCaustic(uv * 1.4 + float2(0.3, 0.7), time * 0.7 + 1.3);

    // Combine: min creates sharp caustic lines
    float caustic = min(layer1, layer2);

    // Sharpen and brighten
    caustic = pow(saturate(1.0 - caustic), 8.0);

    return caustic;
}

// ─── Chromatic Caustics ──────────────────────────────────────────────────
// Offset UV per color channel to simulate light dispersion through water

float3 ChromaticCaustic(float2 uv, float time, float dispersion) {
    float r = CausticPattern(uv + float2(dispersion, 0), time);
    float g = CausticPattern(uv, time);
    float b = CausticPattern(uv - float2(dispersion, 0), time);

    return float3(r, g, b);
}

// ─── Utility ─────────────────────────────────────────────────────────────

float3 ReconstructWorldPos(float2 uv, float depth) {
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;
    float4 worldPos = mul(cb.invViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}

float3 DecodeNormal(float3 encoded) {
    return normalize(encoded * 2.0 - 1.0);
}

// ─── Main Caustics Pass ──────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSCaustics(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float depth = g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0);
    if (depth >= 1.0) return; // Sky

    float3 worldPos = ReconstructWorldPos(uv, depth);
    float3 worldNormal = DecodeNormal(g_GBuffer1.SampleLevel(g_PointClamp, uv, 0).rgb);

    // Only apply caustics below water surface
    float waterDepth = cb.waterHeight - worldPos.y;
    if (waterDepth <= 0.0) return; // Above water

    // Depth-based fade
    float depthFade = exp(-waterDepth * cb.depthFade);
    if (depthFade < 0.001) return;

    // Max depth cutoff
    if (waterDepth > cb.maxDepth) return;

    // Soft edge at water surface
    float surfaceFade = saturate(waterDepth / cb.edgeSoftness);

    // Normal-based attenuation: caustics are stronger on upward-facing surfaces
    float normalAtten = saturate(dot(worldNormal, float3(0, 1, 0)));
    normalAtten = normalAtten * 0.8 + 0.2; // Keep some caustics on walls

    // Light angle attenuation
    float lightAtten = saturate(dot(-cb.lightDir, float3(0, 1, 0)));

    float3 causticColor;

    if (cb.useProcedural) {
        // Procedural Voronoi caustics
        float2 causticUV = worldPos.xz * cb.causticsScale;

        causticColor = ChromaticCaustic(causticUV, cb.time * cb.causticsSpeed,
                                         cb.chromaticDispersion);
    } else {
        // Texture-projected caustics
        float4 lightSpacePos = mul(cb.lightViewProj, float4(worldPos, 1.0));
        float2 lightUV = lightSpacePos.xy / lightSpacePos.w * 0.5 + 0.5;

        // Animate UV
        float2 offset1 = float2(cb.time * cb.causticsSpeed * 0.3, cb.time * cb.causticsSpeed * 0.2);
        float2 offset2 = float2(-cb.time * cb.causticsSpeed * 0.2, cb.time * cb.causticsSpeed * 0.35);

        // Dual-layer texture sampling with chromatic offset
        float r = g_CausticTex.SampleLevel(g_LinearWrap, lightUV * cb.causticsScale + offset1 + float2(cb.chromaticDispersion, 0), 0).r;
        float g = g_CausticTex.SampleLevel(g_LinearWrap, lightUV * cb.causticsScale + offset1, 0).g;
        float b = g_CausticTex.SampleLevel(g_LinearWrap, lightUV * cb.causticsScale + offset1 - float2(cb.chromaticDispersion, 0), 0).b;

        float r2 = g_CausticTex.SampleLevel(g_LinearWrap, lightUV * cb.causticsScale * 0.8 + offset2 + float2(cb.chromaticDispersion, 0), 0).r;
        float g2 = g_CausticTex.SampleLevel(g_LinearWrap, lightUV * cb.causticsScale * 0.8 + offset2, 0).g;
        float b2 = g_CausticTex.SampleLevel(g_LinearWrap, lightUV * cb.causticsScale * 0.8 + offset2 - float2(cb.chromaticDispersion, 0), 0).b;

        causticColor = min(float3(r, g, b), float3(r2, g2, b2));
        causticColor = pow(causticColor, 2.0); // Sharpen
    }

    // Final attenuation
    float totalAtten = depthFade * surfaceFade * normalAtten * lightAtten * cb.causticsIntensity;

    // Read current color and add caustics as additive light
    float4 currentColor = g_Output[DTid.xy];
    float3 causticContribution = causticColor * cb.lightColor * cb.lightIntensity * totalAtten;

    g_Output[DTid.xy] = float4(currentColor.rgb + causticContribution, currentColor.a);
}

// ─── Caustic Texture Generator ───────────────────────────────────────────
// Bakes a tileable caustic pattern texture (512x512) for the
// texture-projection path.

RWTexture2D<float4> g_GeneratedCaustic : register(u1);

[numthreads(8, 8, 1)]
void CSGenerateCausticTexture(uint3 DTid : SV_DispatchThreadID) {
    float2 uv = float2(DTid.xy) / 512.0;

    float3 caustic = ChromaticCaustic(uv * 4.0, cb.time, 0.015);

    g_GeneratedCaustic[DTid.xy] = float4(caustic, 1.0);
}
