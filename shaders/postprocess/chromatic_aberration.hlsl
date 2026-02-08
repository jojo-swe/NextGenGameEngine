// ─── Chromatic Aberration + Film Grain ────────────────────────────────────
// Final post-processing pass applied after tone mapping.
// Simulates lens imperfections for cinematic quality:
//   1. Chromatic aberration: shifts R/G/B channels radially from center
//   2. Film grain: temporally-varying noise overlay
//   3. Vignette: darkens screen edges

#include "../common/math.hlsl"

struct CAConstants {
    uint2  screenSize;
    float  caStrength;       // Chromatic aberration (0 = off, 0.005 = subtle, 0.02 = strong)
    float  grainStrength;    // Film grain (0 = off, 0.05 = subtle, 0.15 = heavy)
    float  vignetteStrength; // Vignette (0 = off, 0.5 = subtle, 1.5 = strong)
    float  vignetteSoftness; // Vignette edge softness
    uint   frameIndex;       // For temporal grain variation
    float  time;
};

[[vk::push_constant]] ConstantBuffer<CAConstants> pc;

Texture2D<float4>   g_InputColor  : register(t0, space22);
RWTexture2D<float4> g_OutputColor : register(u0, space22);
SamplerState        g_LinearClamp : register(s0, space22);

// ─── Film Grain ──────────────────────────────────────────────────────────

float GrainNoise(float2 uv, float t) {
    // Hash-based pseudo-random noise
    float3 p = float3(uv, t);
    p = frac(p * float3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return frac((p.x + p.y) * p.z) * 2.0 - 1.0;
}

// ─── Main ────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.screenSize.x || DTid.y >= pc.screenSize.y) return;

    float2 texelSize = 1.0 / float2(pc.screenSize);
    float2 uv = (float2(DTid.xy) + 0.5) * texelSize;

    // Distance from center (for radial effects)
    float2 fromCenter = uv - 0.5;
    float dist = length(fromCenter);
    float dist2 = dist * dist;

    // ── Chromatic Aberration ─────────────────────────────────────────
    float3 color;

    if (pc.caStrength > 0.0) {
        // Radial offset: stronger toward edges
        float2 caOffset = fromCenter * dist2 * pc.caStrength;

        // Sample each channel at a different UV offset
        color.r = g_InputColor.SampleLevel(g_LinearClamp, uv - caOffset, 0).r;
        color.g = g_InputColor.SampleLevel(g_LinearClamp, uv, 0).g;
        color.b = g_InputColor.SampleLevel(g_LinearClamp, uv + caOffset, 0).b;
    } else {
        color = g_InputColor.SampleLevel(g_LinearClamp, uv, 0).rgb;
    }

    // ── Film Grain ───────────────────────────────────────────────────

    if (pc.grainStrength > 0.0) {
        float grain = GrainNoise(uv * float2(pc.screenSize), float(pc.frameIndex) * 0.1 + pc.time);

        // Luminance-dependent grain (less grain in bright areas)
        float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
        float grainMask = 1.0 - saturate(luminance * 2.0);

        color += grain * pc.grainStrength * grainMask;
    }

    // ── Vignette ─────────────────────────────────────────────────────

    if (pc.vignetteStrength > 0.0) {
        float vignette = 1.0 - smoothstep(pc.vignetteSoftness, pc.vignetteSoftness + 0.3,
                                            dist * pc.vignetteStrength);
        color *= vignette;
    }

    // Clamp to valid range
    color = max(color, 0.0);

    g_OutputColor[DTid.xy] = float4(color, 1.0);
}
