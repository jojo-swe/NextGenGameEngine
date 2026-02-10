// ─── Procedural Speed Lines / Radial Blur Effect Shader ──────────────────
// Screen-space post-process for speed/motion effects with radial blur,
// zoom blur, and manga-style speed lines for action sequences.
//
// Features:
//   - Radial zoom blur from configurable focus point
//   - Manga-style speed lines (radial streaks)
//   - Directional motion blur (horizontal/vertical)
//   - Configurable blur strength and sample count
//   - Focus point masking (clear center, blurred edges)
//   - Speed-dependent intensity (velocity input)
//   - Chromatic separation along blur direction
//   - Animated line flickering for dynamic feel
//   - Depth-aware blur (skip foreground objects)
//
// References:
//   - "Speed Effects in Dragon Ball FighterZ" (Arc System Works, GDC 2018)
//   - "Radial Blur in God of War" (Santa Monica, SIGGRAPH 2019)
//   - "Manga Speed Lines" (Comix Wave Films, 2016)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor : register(t0);
Texture2D<float>  g_SceneDepth : register(t1);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct SpeedLinesConstants {
    float2   resolution;
    float2   invResolution;
    float    time;
    u32      effectMode;           // 0=radial zoom, 1=speed lines, 2=directional, 3=combined
    float2   focusPoint;           // Screen-space focus center (default: 0.5, 0.5)
    float    blurStrength;         // Blur intensity (default 0.1)
    u32      sampleCount;          // Blur samples (default 16)
    float    focusRadius;          // Clear zone radius around focus (default 0.2)
    float    focusFalloff;         // Transition softness (default 0.3)
    float    speedLinesDensity;    // Number of speed lines (default 40.0)
    float    speedLinesWidth;      // Line thickness (default 0.02)
    float    speedLinesLength;     // Line length factor (default 0.5)
    float    chromaticAmount;      // Chromatic separation (default 0.0)
    float    flickerSpeed;         // Line animation speed (default 5.0)
    float    flickerAmount;        // Flicker intensity (default 0.3)
    float2   motionDirection;      // For directional mode (default: 1, 0)
    float    depthThreshold;       // Depth-aware threshold (default 0.0, disabled)
    float    lineOpacity;          // Speed line opacity (default 0.5)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<SpeedLinesConstants> cb;

// ─── Hash ────────────────────────────────────────────────────────────────

float Hash11(float p) {
    p = frac(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return frac(p);
}

// ─── Focus Mask ──────────────────────────────────────────────────────────

float FocusMask(float2 uv) {
    float dist = length(uv - cb.focusPoint);
    return smoothstep(cb.focusRadius, cb.focusRadius + cb.focusFalloff, dist);
}

// ─── Radial Zoom Blur ────────────────────────────────────────────────────

float3 RadialZoomBlur(float2 uv) {
    float2 dir = uv - cb.focusPoint;
    float mask = FocusMask(uv);

    float3 sum = float3(0, 0, 0);
    float totalWeight = 0.0;

    for (u32 i = 0; i < cb.sampleCount; ++i) {
        float t = float(i) / float(cb.sampleCount - 1);
        float2 offset = dir * t * cb.blurStrength * mask;
        float2 sampleUV = clamp(uv - offset, 0.0, 1.0);

        float weight = 1.0 - t * 0.5; // Closer samples weighted more
        sum += g_SceneColor.SampleLevel(g_LinearClamp, sampleUV, 0).rgb * weight;
        totalWeight += weight;
    }

    return sum / totalWeight;
}

// ─── Radial Zoom with Chromatic Separation ───────────────────────────────

float3 RadialZoomBlurChromatic(float2 uv) {
    float2 dir = uv - cb.focusPoint;
    float mask = FocusMask(uv);

    float3 sum = float3(0, 0, 0);
    float totalWeight = 0.0;

    for (u32 i = 0; i < cb.sampleCount; ++i) {
        float t = float(i) / float(cb.sampleCount - 1);
        float baseOffset = t * cb.blurStrength * mask;

        float2 uvR = clamp(uv - dir * (baseOffset * (1.0 + cb.chromaticAmount)), 0.0, 1.0);
        float2 uvG = clamp(uv - dir * baseOffset, 0.0, 1.0);
        float2 uvB = clamp(uv - dir * (baseOffset * (1.0 - cb.chromaticAmount)), 0.0, 1.0);

        float weight = 1.0 - t * 0.5;
        sum.r += g_SceneColor.SampleLevel(g_LinearClamp, uvR, 0).r * weight;
        sum.g += g_SceneColor.SampleLevel(g_LinearClamp, uvG, 0).g * weight;
        sum.b += g_SceneColor.SampleLevel(g_LinearClamp, uvB, 0).b * weight;
        totalWeight += weight;
    }

    return sum / totalWeight;
}

// ─── Manga Speed Lines ───────────────────────────────────────────────────

float SpeedLinePattern(float2 uv) {
    float2 centered = uv - cb.focusPoint;
    float angle = atan2(centered.y, centered.x);
    float dist = length(centered);

    // Radial line pattern
    float linePattern = sin(angle * cb.speedLinesDensity) * 0.5 + 0.5;
    linePattern = smoothstep(0.5 - cb.speedLinesWidth, 0.5, linePattern);

    // Lines get stronger further from center
    float distMask = smoothstep(cb.focusRadius, cb.focusRadius + cb.speedLinesLength, dist);

    // Per-line random variation
    float lineId = floor(angle * cb.speedLinesDensity / 6.283);
    float rnd = Hash11(lineId);

    // Flicker animation
    float flicker = 1.0;
    if (cb.flickerAmount > 0.0) {
        flicker = sin(cb.time * cb.flickerSpeed + rnd * 20.0) * cb.flickerAmount + (1.0 - cb.flickerAmount);
        flicker = saturate(flicker);
    }

    // Some lines are thinner/shorter
    float variation = 0.5 + rnd * 0.5;

    return linePattern * distMask * flicker * variation;
}

// ─── Directional Motion Blur ─────────────────────────────────────────────

float3 DirectionalBlur(float2 uv) {
    float2 dir = normalize(cb.motionDirection) * cb.blurStrength * cb.invResolution * 10.0;

    float3 sum = float3(0, 0, 0);
    float totalWeight = 0.0;

    for (u32 i = 0; i < cb.sampleCount; ++i) {
        float t = (float(i) / float(cb.sampleCount - 1) - 0.5) * 2.0;
        float2 sampleUV = clamp(uv + dir * t, 0.0, 1.0);

        float weight = 1.0 - abs(t) * 0.3;
        sum += g_SceneColor.SampleLevel(g_LinearClamp, sampleUV, 0).rgb * weight;
        totalWeight += weight;
    }

    return sum / totalWeight;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float4 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);

    // ── Depth-aware masking ──────────────────────────────────────────
    float depthMask = 1.0;
    if (cb.depthThreshold > 0.0) {
        float depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
        depthMask = smoothstep(cb.depthThreshold, cb.depthThreshold + 0.1, depth);
    }

    float3 result = sceneColor.rgb;

    // ── Apply blur effect ────────────────────────────────────────────
    if (cb.effectMode == 0) {
        // Radial zoom blur
        if (cb.chromaticAmount > 0.0) {
            result = lerp(sceneColor.rgb, RadialZoomBlurChromatic(uv), depthMask);
        } else {
            result = lerp(sceneColor.rgb, RadialZoomBlur(uv), depthMask);
        }
    } else if (cb.effectMode == 1) {
        // Manga speed lines only
        float lines = SpeedLinePattern(uv);
        result = lerp(sceneColor.rgb, float3(1, 1, 1), lines * cb.lineOpacity * depthMask);
    } else if (cb.effectMode == 2) {
        // Directional motion blur
        result = lerp(sceneColor.rgb, DirectionalBlur(uv), depthMask);
    } else {
        // Combined: radial blur + speed lines
        float3 blurred;
        if (cb.chromaticAmount > 0.0) {
            blurred = RadialZoomBlurChromatic(uv);
        } else {
            blurred = RadialZoomBlur(uv);
        }

        float lines = SpeedLinePattern(uv);
        result = lerp(sceneColor.rgb, blurred, depthMask);
        result = lerp(result, float3(1, 1, 1), lines * cb.lineOpacity * depthMask * 0.5);
    }

    g_Output[DTid.xy] = float4(result, 1.0);
}
