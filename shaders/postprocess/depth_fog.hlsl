// ─── Depth Fog / Distance Fog Shader ─────────────────────────────────────
// Screen-space post-process that applies distance-based and height-based
// fog to the scene. Essential for atmosphere, depth cues, and hiding
// far-plane pop-in.
//
// Features:
//   - Linear fog (start/end distance)
//   - Exponential fog (exp density)
//   - Exponential squared fog (exp2 density)
//   - Height fog (altitude-based density falloff)
//   - Animated noise for volumetric feel
//   - Fog color with sun scattering tint
//   - Depth-based fog with configurable near/far
//   - Inscattering approximation (directional light color bleed)
//   - Per-pixel noise dithering to reduce banding

#include "../common/math.hlsl"

Texture2D<float4> g_SceneColor : register(t0);
Texture2D<float>  g_SceneDepth : register(t1);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct DepthFogConstants {
    float2   resolution;
    float2   invResolution;
    float    time;

    u32      fogMode;             // 0=linear, 1=exp, 2=exp2
    float3   fogColor;            // Base fog color (default: 0.7, 0.8, 0.9)

    // Linear fog
    float    fogStart;            // Linear fog start distance (default: 10.0)
    float    fogEnd;              // Linear fog end distance (default: 500.0)

    // Exponential fog
    float    fogDensity;          // Exp/Exp2 density (default: 0.01)

    // Height fog
    float    heightFogEnabled;    // 0 or 1
    float    heightFogBase;       // World-space base altitude (default: 0.0)
    float    heightFogDensity;    // Height fog density (default: 0.05)
    float    heightFogFalloff;    // Vertical falloff rate (default: 0.1)

    // Inscattering
    float3   sunDirection;        // Normalized sun direction
    float3   sunColor;            // Sun inscatter color (default: 1.0, 0.9, 0.7)
    float    inscatterPower;      // Inscatter phase function power (default: 8.0)
    float    inscatterIntensity;  // Inscatter strength (default: 0.3)

    // Noise
    float    noiseScale;          // Animated noise scale (default: 0.0, disabled)
    float    noiseSpeed;          // Noise animation speed (default: 0.5)

    // Camera
    float    cameraNear;
    float    cameraFar;
    float3   cameraPosition;
    float4x4 invViewProj;

    float    maxFogAmount;        // Clamp max fog (default: 1.0)
    float    ditherStrength;      // Banding reduction (default: 0.5)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<DepthFogConstants> cb;

// ─── Reconstruct World Position ──────────────────────────────────────────

float3 ReconstructWorldPos(float2 uv, float depth) {
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;
    float4 worldPos = mul(cb.invViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}

// ─── Linearize Depth ─────────────────────────────────────────────────────

float LinearizeDepth(float d) {
    return cb.cameraNear * cb.cameraFar / (cb.cameraFar - d * (cb.cameraFar - cb.cameraNear));
}

// ─── Fog Factor Calculation ──────────────────────────────────────────────

float ComputeDistanceFog(float distance) {
    if (cb.fogMode == 0) {
        // Linear
        return saturate((distance - cb.fogStart) / max(cb.fogEnd - cb.fogStart, 0.001));
    } else if (cb.fogMode == 1) {
        // Exponential
        return 1.0 - exp(-cb.fogDensity * distance);
    } else {
        // Exponential squared
        float f = cb.fogDensity * distance;
        return 1.0 - exp(-f * f);
    }
}

float ComputeHeightFog(float3 worldPos, float3 cameraPos) {
    if (cb.heightFogEnabled < 0.5) return 0.0;

    float heightAboveBase = worldPos.y - cb.heightFogBase;
    float cameraHeight = cameraPos.y - cb.heightFogBase;

    // Integrate fog density along the view ray through the height field
    float rayLength = length(worldPos - cameraPos);
    float avgHeight = (heightAboveBase + cameraHeight) * 0.5;

    float density = cb.heightFogDensity * exp(-cb.heightFogFalloff * max(avgHeight, 0.0));
    float fog = 1.0 - exp(-density * rayLength);

    return saturate(fog);
}

// ─── Inscattering ────────────────────────────────────────────────────────

float3 ComputeInscatter(float3 viewDir, float fogAmount) {
    if (cb.inscatterIntensity <= 0.0) return float3(0, 0, 0);

    float cosAngle = dot(viewDir, cb.sunDirection);
    float scatter = pow(saturate(cosAngle), cb.inscatterPower);

    return cb.sunColor * scatter * cb.inscatterIntensity * fogAmount;
}

// ─── Noise Dither ────────────────────────────────────────────────────────

float DitherNoise(float2 uv) {
    float2 pixel = uv * cb.resolution;
    return frac(52.9829189 * frac(0.06711056 * pixel.x + 0.00583715 * pixel.y));
}

// ─── Animated Fog Noise ──────────────────────────────────────────────────

float FogNoise(float3 worldPos) {
    if (cb.noiseScale <= 0.0) return 1.0;

    float3 p = worldPos * cb.noiseScale + cb.time * cb.noiseSpeed;
    float n = frac(sin(dot(floor(p.xz), float2(12.9898, 78.233))) * 43758.5453);
    return lerp(0.7, 1.3, n);
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;
    float3 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;
    float rawDepth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);

    // Skip sky pixels
    if (rawDepth >= 1.0) {
        g_Output[DTid.xy] = float4(sceneColor, 1.0);
        return;
    }

    float linearDepth = LinearizeDepth(rawDepth);
    float3 worldPos = ReconstructWorldPos(uv, rawDepth);
    float3 viewDir = normalize(worldPos - cb.cameraPosition);

    // Distance fog
    float distFog = ComputeDistanceFog(linearDepth);

    // Height fog
    float heightFog = ComputeHeightFog(worldPos, cb.cameraPosition);

    // Combine
    float totalFog = saturate(distFog + heightFog - distFog * heightFog);

    // Animated noise variation
    totalFog *= FogNoise(worldPos);

    // Clamp
    totalFog = min(totalFog, cb.maxFogAmount);

    // Dither to reduce banding
    if (cb.ditherStrength > 0.0) {
        float dither = (DitherNoise(uv) - 0.5) * cb.ditherStrength * 0.02;
        totalFog = saturate(totalFog + dither);
    }

    // Inscattering
    float3 inscatter = ComputeInscatter(viewDir, totalFog);

    // Final blend
    float3 foggedColor = lerp(sceneColor, cb.fogColor + inscatter, totalFog);

    g_Output[DTid.xy] = float4(foggedColor, 1.0);
}
