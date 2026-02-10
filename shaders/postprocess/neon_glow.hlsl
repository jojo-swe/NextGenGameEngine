// ─── Procedural Neon Glow / Signage Effect Shader ────────────────────────
// Screen-space post-process for neon light effects with emissive glow,
// light bleeding, flicker animation, and color-shifting tubes.
//
// Features:
//   - Emissive threshold extraction (bright neon sources)
//   - Multi-pass Gaussian blur for soft glow
//   - Color-shifted halo (outer glow tinted differently from core)
//   - Neon tube flicker simulation (per-source random timing)
//   - Bloom intensity falloff with distance from source
//   - Additive blending with scene color
//   - Animated color cycling for dynamic signage
//   - Glow saturation boost for vibrant neon look
//   - Configurable glow radius, threshold, and intensity
//
// References:
//   - "Neon Lighting in Cyberpunk 2077" (CD Projekt Red, GDC 2021)
//   - "Real-Time Glow" (GPU Gems, Ch. 21)
//   - "Bloom and HDR Rendering" (Kawase, GDC 2003)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor   : register(t0);
Texture2D<float4> g_EmissiveMask : register(t1); // Emissive channel from GBuffer
Texture2D<float>  g_NoiseTex     : register(t2);

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct NeonGlowConstants {
    float2   resolution;
    float2   invResolution;
    float    time;
    float    glowThreshold;       // Emissive brightness threshold (default 0.5)
    float    glowIntensity;       // Glow brightness multiplier (default 2.0)
    float    glowRadius;          // Blur radius in pixels (default 8.0)
    float3   innerGlowTint;      // Core glow color tint (default: 1, 1, 1)
    float    outerGlowFalloff;   // Outer halo falloff exponent (default 2.0)
    float3   outerGlowTint;      // Outer halo color tint (default: 0.8, 0.9, 1.0)
    float    flickerSpeed;        // Flicker animation speed (default 8.0)
    float    flickerAmount;       // Flicker intensity variation (default 0.15)
    float    colorCycleSpeed;     // Hue cycling speed (default 0.0, disabled)
    float    saturationBoost;     // Glow saturation multiplier (default 1.5)
    float    bloomMix;            // Bloom blend with scene (default 1.0)
    float    distanceFalloff;     // Glow falloff with distance (default 1.0)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<NeonGlowConstants> cb;

// ─── Hash ────────────────────────────────────────────────────────────────

float Hash11(float p) {
    p = frac(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return frac(p);
}

// ─── HSV Conversion ──────────────────────────────────────────────────────

float3 RGBtoHSV(float3 c) {
    float4 K = float4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
    float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

float3 HSVtoRGB(float3 c) {
    float4 K = float4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// ─── Emissive Extraction ─────────────────────────────────────────────────

float3 ExtractNeonEmissive(float2 uv) {
    float4 emissive = g_EmissiveMask.SampleLevel(g_LinearClamp, uv, 0);
    float luminance = dot(emissive.rgb, float3(0.2126, 0.7152, 0.0722));

    // Threshold: only bright emissive regions contribute to glow
    float mask = smoothstep(cb.glowThreshold, cb.glowThreshold + 0.2, luminance);

    return emissive.rgb * mask;
}

// ─── Gaussian Blur (Separable 2-pass approximation in single pass) ───────

float3 BlurGlow(float2 uv) {
    float3 sum = float3(0, 0, 0);
    float totalWeight = 0.0;

    float radius = cb.glowRadius;
    int samples = int(min(radius * 2.0, 16.0));

    // Dual-axis blur approximation
    for (int y = -samples; y <= samples; ++y) {
        for (int x = -samples; x <= samples; ++x) {
            float2 offset = float2(float(x), float(y)) * cb.invResolution * (radius / float(samples));
            float dist = length(float2(float(x), float(y))) / float(samples);

            if (dist > 1.0) continue;

            // Gaussian weight
            float weight = exp(-dist * dist * 3.0);

            float2 sampleUV = uv + offset;
            sampleUV = clamp(sampleUV, 0.0, 1.0);

            float3 emissive = ExtractNeonEmissive(sampleUV);
            sum += emissive * weight;
            totalWeight += weight;
        }
    }

    return totalWeight > 0.0 ? sum / totalWeight : float3(0, 0, 0);
}

// ─── Flicker ─────────────────────────────────────────────────────────────

float NeonFlicker(float2 uv) {
    // Per-pixel flicker based on position hash
    float pixelSeed = Hash11(floor(uv.x * 50.0) + floor(uv.y * 50.0) * 100.0);

    // Multiple frequency flicker
    float flicker1 = sin(cb.time * cb.flickerSpeed + pixelSeed * 20.0) * 0.5 + 0.5;
    float flicker2 = sin(cb.time * cb.flickerSpeed * 2.7 + pixelSeed * 10.0) * 0.5 + 0.5;

    // Occasional complete dropout
    float dropout = step(0.98, Hash11(floor(cb.time * 30.0) + pixelSeed * 7.0));

    float flicker = 1.0 - cb.flickerAmount * (1.0 - flicker1 * flicker2) - dropout * 0.5;

    return saturate(flicker);
}

// ─── Color Cycling ───────────────────────────────────────────────────────

float3 CycleColor(float3 color) {
    if (cb.colorCycleSpeed <= 0.0) return color;

    float3 hsv = RGBtoHSV(color);
    hsv.x = frac(hsv.x + cb.time * cb.colorCycleSpeed);
    return HSVtoRGB(hsv);
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float4 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);

    // ── Extract and blur neon glow ───────────────────────────────────
    float3 innerGlow = ExtractNeonEmissive(uv);
    float3 blurredGlow = BlurGlow(uv);

    // ── Apply flicker ────────────────────────────────────────────────
    float flicker = NeonFlicker(uv);

    // ── Inner glow (core emissive) ───────────────────────────────────
    float3 coreGlow = innerGlow * cb.innerGlowTint * cb.glowIntensity * flicker;

    // ── Outer glow (blurred halo) ────────────────────────────────────
    float3 outerGlow = blurredGlow * cb.outerGlowTint * cb.glowIntensity * flicker;

    // Distance falloff for outer glow
    float outerLum = dot(outerGlow, float3(0.2126, 0.7152, 0.0722));
    outerGlow *= pow(saturate(outerLum), cb.outerGlowFalloff) * cb.distanceFalloff;

    // ── Color cycling ────────────────────────────────────────────────
    coreGlow = CycleColor(coreGlow);
    outerGlow = CycleColor(outerGlow);

    // ── Saturation boost ─────────────────────────────────────────────
    float coreLum = dot(coreGlow, float3(0.2126, 0.7152, 0.0722));
    coreGlow = lerp(float3(coreLum, coreLum, coreLum), coreGlow, cb.saturationBoost);

    float outerLum2 = dot(outerGlow, float3(0.2126, 0.7152, 0.0722));
    outerGlow = lerp(float3(outerLum2, outerLum2, outerLum2), outerGlow, cb.saturationBoost);

    // ── Combine: additive blend ──────────────────────────────────────
    float3 totalGlow = coreGlow + outerGlow;
    float3 result = sceneColor.rgb + totalGlow * cb.bloomMix;

    g_Output[DTid.xy] = float4(result, 1.0);
}
