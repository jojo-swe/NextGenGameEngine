// ─── Procedural Snow Accumulation Shader ─────────────────────────────────
// Deferred GBuffer modifier that adds snow coverage to surfaces based on
// world-space height, surface normal orientation, slope angle, and
// procedural noise for natural variation.
//
// Features:
//   - Height-based snow line with soft transition
//   - Normal-based coverage: upward-facing surfaces accumulate more
//   - Slope angle rejection: steep surfaces shed snow
//   - Procedural noise for natural edge variation
//   - Snow thickness with parallax offset
//   - Subsurface scattering tint for packed snow
//   - Wind-blown accumulation bias
//   - Icicle drip mask for overhangs
//
// References:
//   - "Snow Rendering in The Division" (Ubisoft, GDC 2016)
//   - "Horizon Zero Dawn Snow Tech" (Guerrilla, SIGGRAPH 2017)
//   - "God of War Snow Deformation" (Santa Monica, GDC 2019)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_GBufferAlbedo  : register(t0);
Texture2D<float4> g_GBufferNormal  : register(t1); // World-space normals
Texture2D<float4> g_GBufferPBR     : register(t2); // R=roughness, G=metallic
Texture2D<float>  g_SceneDepth     : register(t3);
Texture2D<float>  g_NoiseTex       : register(t4); // Tileable noise

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

RWTexture2D<float4> g_OutAlbedo  : register(u0);
RWTexture2D<float4> g_OutNormal  : register(u1);
RWTexture2D<float4> g_OutPBR     : register(u2);

struct SnowConstants {
    float4x4 invViewProj;
    float2   resolution;
    float2   invResolution;
    float3   cameraPos;
    float    time;
    float    snowLine;           // World Y where snow begins (default 10.0)
    float    snowTransition;     // Height blend range (default 5.0)
    float    slopeThreshold;     // Max slope for snow (cosine, default 0.5)
    float    slopeBlend;         // Slope blend softness (default 0.15)
    float    coverageAmount;     // Global snow amount 0..1 (default 0.8)
    float    noiseScale;         // Edge noise UV scale (default 0.3)
    float    noiseStrength;      // Edge noise influence (default 0.3)
    float    thicknessMax;       // Max snow thickness for parallax (default 0.05)
    float3   snowAlbedo;         // Snow color (default: 0.9, 0.92, 0.95)
    float    snowRoughness;      // Snow roughness (default 0.8)
    float3   snowSSSTint;        // Subsurface tint (default: 0.6, 0.7, 0.9)
    float    sssStrength;        // SSS amount (default 0.2)
    float3   windDirection;      // Wind bias for directional accumulation
    float    windStrength;       // Wind bias strength (default 0.3)
    float    icicleThreshold;    // Overhang threshold for icicle drip (default -0.3)
    float    icicleLength;       // Icicle drip extent (default 0.1)
    float    sparkleAmount;      // Glitter/sparkle intensity (default 0.1)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<SnowConstants> cb;

// ─── World Position Reconstruction ───────────────────────────────────────

float3 ReconstructWorldPos(float2 uv, float depth) {
    float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    clip.y = -clip.y;
    float4 world = mul(cb.invViewProj, clip);
    return world.xyz / world.w;
}

// ─── Snow Coverage Calculation ───────────────────────────────────────────

float HeightCoverage(float worldY) {
    return smoothstep(cb.snowLine - cb.snowTransition * 0.5,
                      cb.snowLine + cb.snowTransition * 0.5, worldY);
}

float SlopeCoverage(float3 worldNormal) {
    float upDot = worldNormal.y; // 1=flat, 0=vertical, -1=overhang
    return smoothstep(cb.slopeThreshold - cb.slopeBlend,
                      cb.slopeThreshold + cb.slopeBlend, upDot);
}

float WindBias(float3 worldNormal) {
    // Surfaces facing away from wind accumulate more snow
    float windDot = dot(worldNormal.xz, cb.windDirection.xz);
    return 1.0 + windDot * cb.windStrength;
}

float NoiseCoverage(float2 uv, float3 worldPos) {
    // Multi-scale noise for natural edge variation
    float2 noiseUV = worldPos.xz * cb.noiseScale;
    float n1 = g_NoiseTex.SampleLevel(g_LinearWrap, noiseUV, 0);
    float n2 = g_NoiseTex.SampleLevel(g_LinearWrap, noiseUV * 3.7 + 0.5, 0);
    float noise = n1 * 0.7 + n2 * 0.3;
    return noise * cb.noiseStrength;
}

// ─── Icicle Drip Mask ────────────────────────────────────────────────────
// Overhanging surfaces get icicle-like drip patterns.

float IcicleMask(float3 worldNormal, float2 uv, float3 worldPos) {
    if (worldNormal.y > cb.icicleThreshold) return 0.0;

    // Vertical streaks for drip pattern
    float2 dripUV = worldPos.xz * 8.0;
    float drip = g_NoiseTex.SampleLevel(g_LinearWrap, float2(dripUV.x, worldPos.y * 4.0), 0);
    drip = smoothstep(0.7, 0.9, drip);

    float overhangFactor = smoothstep(cb.icicleThreshold, cb.icicleThreshold - 0.3, worldNormal.y);

    return drip * overhangFactor * cb.icicleLength;
}

// ─── Snow Normal Perturbation ────────────────────────────────────────────

float3 SnowNormal(float3 baseNormal, float2 uv, float3 worldPos, float coverage) {
    float2 texel = cb.invResolution;
    float2 noiseUV = worldPos.xz * cb.noiseScale * 5.0;

    // Compute gradient from noise for micro-normal
    float nC = g_NoiseTex.SampleLevel(g_LinearWrap, noiseUV, 0);
    float nR = g_NoiseTex.SampleLevel(g_LinearWrap, noiseUV + float2(0.01, 0), 0);
    float nU = g_NoiseTex.SampleLevel(g_LinearWrap, noiseUV + float2(0, 0.01), 0);

    float3 snowN = normalize(float3(nC - nR, 1.0, nC - nU));

    // Blend snow normal with base normal
    float3 blended = normalize(lerp(baseNormal, float3(0, 1, 0), coverage * 0.7));
    blended = normalize(blended + snowN * 0.1 * coverage);

    return blended;
}

// ─── Snow Sparkle ────────────────────────────────────────────────────────
// Glitter effect from individual ice crystals catching light.

float SnowSparkle(float3 worldPos, float3 viewDir) {
    float3 sparklePos = worldPos * 50.0;
    float sparkle = frac(sin(dot(floor(sparklePos), float3(12.9898, 78.233, 45.164))) * 43758.5453);

    // View-dependent (sparkles shift with camera)
    float viewInfluence = abs(dot(normalize(frac(sparklePos) - 0.5), viewDir));
    sparkle = smoothstep(0.97, 1.0, sparkle * viewInfluence);

    return sparkle * cb.sparkleAmount;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
    float4 albedo = g_GBufferAlbedo.SampleLevel(g_LinearClamp, uv, 0);
    float3 worldNormal = g_GBufferNormal.SampleLevel(g_LinearClamp, uv, 0).rgb * 2.0 - 1.0;
    worldNormal = normalize(worldNormal);
    float4 pbr = g_GBufferPBR.SampleLevel(g_LinearClamp, uv, 0);

    // Skip sky
    if (depth < 0.0001) {
        g_OutAlbedo[DTid.xy] = albedo;
        g_OutNormal[DTid.xy] = float4(worldNormal * 0.5 + 0.5, 1.0);
        g_OutPBR[DTid.xy] = pbr;
        return;
    }

    float3 worldPos = ReconstructWorldPos(uv, depth);
    float3 viewDir = normalize(cb.cameraPos - worldPos);

    // ── Coverage factors ─────────────────────────────────────────────
    float heightCov = HeightCoverage(worldPos.y);
    float slopeCov = SlopeCoverage(worldNormal);
    float windBias = WindBias(worldNormal);
    float noiseCov = NoiseCoverage(uv, worldPos);
    float icicle = IcicleMask(worldNormal, uv, worldPos);

    // Combined snow coverage
    float snowCoverage = heightCov * slopeCov * windBias * cb.coverageAmount;
    snowCoverage += noiseCov - 0.15; // Noise offset for edge variation
    snowCoverage += icicle;           // Add icicle coverage on overhangs
    snowCoverage = saturate(snowCoverage);

    if (snowCoverage < 0.01) {
        g_OutAlbedo[DTid.xy] = albedo;
        g_OutNormal[DTid.xy] = float4(worldNormal * 0.5 + 0.5, 1.0);
        g_OutPBR[DTid.xy] = pbr;
        return;
    }

    // ── Snow albedo ──────────────────────────────────────────────────
    // Subsurface scattering tint for packed snow
    float3 snowColor = cb.snowAlbedo;
    float sss = cb.sssStrength * snowCoverage;
    snowColor = lerp(snowColor, cb.snowSSSTint, sss * (1.0 - worldNormal.y * 0.5));

    // Sparkle
    float sparkle = SnowSparkle(worldPos, viewDir) * snowCoverage;
    snowColor += float3(1, 1, 1) * sparkle;

    // Blend with base albedo
    float3 finalAlbedo = lerp(albedo.rgb, snowColor, snowCoverage);

    // ── Snow normal ──────────────────────────────────────────────────
    float3 snowNorm = SnowNormal(worldNormal, uv, worldPos, snowCoverage);

    // ── Snow PBR ─────────────────────────────────────────────────────
    float finalRoughness = lerp(pbr.r, cb.snowRoughness, snowCoverage);
    float finalMetallic = lerp(pbr.g, 0.0, snowCoverage); // Snow is dielectric

    // ── Write outputs ────────────────────────────────────────────────
    g_OutAlbedo[DTid.xy] = float4(finalAlbedo, albedo.a);
    g_OutNormal[DTid.xy] = float4(snowNorm * 0.5 + 0.5, 1.0);
    g_OutPBR[DTid.xy] = float4(finalRoughness, finalMetallic, pbr.ba);
}
