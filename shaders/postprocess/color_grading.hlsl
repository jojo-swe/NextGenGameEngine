// ─── Procedural Color Grading / LUT Shader ───────────────────────────────
// Screen-space post-process for cinematic color grading without requiring
// a pre-baked 3D LUT texture. All color transforms are computed
// procedurally from configurable parameters.
//
// Features:
//   - Lift / Gamma / Gain color wheels (shadows / midtones / highlights)
//   - Saturation, vibrance, and hue shift
//   - White balance (temperature + tint)
//   - Tone curve (S-curve contrast with shadow/highlight pivot)
//   - Color channel mixer (RGB cross-channel)
//   - Split toning (shadow tint + highlight tint with balance)
//   - Film grain overlay
//   - Procedural 3D LUT generation (no texture needed)
//   - HDR tonemapping integration (ACES, Reinhard, Uncharted2)
//
// References:
//   - "Color Grading in Unreal Engine" (Epic Games)
//   - "ASC CDL" (American Society of Cinematographers)
//   - "Filmic Tonemapping" (John Hable, GDC 2010)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor : register(t0);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct ColorGradingConstants {
    float2   resolution;
    float2   invResolution;
    float    time;

    // Lift / Gamma / Gain (ASC CDL)
    float3   lift;              // Shadow color offset (default: 0,0,0)
    float3   gamma;             // Midtone color power (default: 1,1,1)
    float3   gain;              // Highlight color multiply (default: 1,1,1)

    // Saturation & Hue
    float    saturation;        // Global saturation (default: 1.0)
    float    vibrance;          // Vibrance: boost low-sat colors (default: 0.0)
    float    hueShift;          // Hue rotation in degrees (default: 0.0)

    // White Balance
    float    temperature;       // Cool (-1) to Warm (+1) (default: 0.0)
    float    tint;              // Green (-1) to Magenta (+1) (default: 0.0)

    // Tone Curve
    float    contrast;          // S-curve strength (default: 1.0)
    float    shadowPivot;       // Shadow tone pivot (default: 0.2)
    float    highlightPivot;    // Highlight tone pivot (default: 0.8)

    // Channel Mixer (3x3 row-major)
    float3   channelMixerR;     // Red output from RGB (default: 1,0,0)
    float3   channelMixerG;     // Green output from RGB (default: 0,1,0)
    float3   channelMixerB;     // Blue output from RGB (default: 0,0,1)

    // Split Toning
    float3   shadowTint;        // Shadow tint color (default: 0.5,0.5,0.5)
    float3   highlightTint;     // Highlight tint color (default: 0.5,0.5,0.5)
    float    splitBalance;      // -1 to +1, balance between shadow/highlight (default: 0.0)

    // Film Grain
    float    grainIntensity;    // Film grain strength (default: 0.0)
    float    grainSize;         // Grain particle size (default: 1.6)

    // Tonemapping
    u32      tonemapMode;       // 0=none, 1=ACES, 2=Reinhard, 3=Uncharted2
    float    exposure;          // Exposure compensation in stops (default: 0.0)

    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<ColorGradingConstants> cb;

// ─── Color Space Helpers ─────────────────────────────────────────────────

float3 RGBtoHSV(float3 rgb) {
    float cMax = max(rgb.r, max(rgb.g, rgb.b));
    float cMin = min(rgb.r, min(rgb.g, rgb.b));
    float delta = cMax - cMin;

    float h = 0.0;
    if (delta > 0.0001) {
        if (cMax == rgb.r) h = fmod((rgb.g - rgb.b) / delta, 6.0);
        else if (cMax == rgb.g) h = (rgb.b - rgb.r) / delta + 2.0;
        else h = (rgb.r - rgb.g) / delta + 4.0;
        h /= 6.0;
        if (h < 0.0) h += 1.0;
    }

    float s = cMax > 0.0001 ? delta / cMax : 0.0;
    float v = cMax;

    return float3(h, s, v);
}

float3 HSVtoRGB(float3 hsv) {
    float h = hsv.x * 6.0;
    float s = hsv.y;
    float v = hsv.z;

    float c = v * s;
    float x = c * (1.0 - abs(fmod(h, 2.0) - 1.0));
    float m = v - c;

    float3 rgb;
    if (h < 1.0) rgb = float3(c, x, 0);
    else if (h < 2.0) rgb = float3(x, c, 0);
    else if (h < 3.0) rgb = float3(0, c, x);
    else if (h < 4.0) rgb = float3(0, x, c);
    else if (h < 5.0) rgb = float3(x, 0, c);
    else rgb = float3(c, 0, x);

    return rgb + m;
}

// ─── White Balance ───────────────────────────────────────────────────────

float3 ApplyWhiteBalance(float3 color) {
    // Approximate temperature/tint as RGB multipliers
    float3 wb = float3(
        1.0 + cb.temperature * 0.1,
        1.0 + cb.tint * 0.05,
        1.0 - cb.temperature * 0.1
    );
    return color * wb;
}

// ─── Lift / Gamma / Gain ─────────────────────────────────────────────────

float3 ApplyLiftGammaGain(float3 color) {
    // Lift: offset added to shadows
    color = color + cb.lift;

    // Gain: multiply highlights
    color = color * cb.gain;

    // Gamma: power curve for midtones
    color = pow(max(color, 0.0001), 1.0 / max(cb.gamma, 0.001));

    return color;
}

// ─── Saturation & Vibrance ───────────────────────────────────────────────

float3 ApplySaturationVibrance(float3 color) {
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));

    // Global saturation
    color = lerp(float3(lum, lum, lum), color, cb.saturation);

    // Vibrance: boost under-saturated colors more
    if (cb.vibrance != 0.0) {
        float maxC = max(color.r, max(color.g, color.b));
        float minC = min(color.r, min(color.g, color.b));
        float sat = (maxC > 0.001) ? (maxC - minC) / maxC : 0.0;
        float vibranceFactor = 1.0 + cb.vibrance * (1.0 - sat);
        color = lerp(float3(lum, lum, lum), color, vibranceFactor);
    }

    return color;
}

// ─── Hue Shift ───────────────────────────────────────────────────────────

float3 ApplyHueShift(float3 color) {
    if (abs(cb.hueShift) < 0.001) return color;

    float3 hsv = RGBtoHSV(color);
    hsv.x = frac(hsv.x + cb.hueShift / 360.0);
    return HSVtoRGB(hsv);
}

// ─── Channel Mixer ───────────────────────────────────────────────────────

float3 ApplyChannelMixer(float3 color) {
    return float3(
        dot(color, cb.channelMixerR),
        dot(color, cb.channelMixerG),
        dot(color, cb.channelMixerB)
    );
}

// ─── Tone Curve (S-Curve) ────────────────────────────────────────────────

float3 ApplyToneCurve(float3 color) {
    if (abs(cb.contrast - 1.0) < 0.001) return color;

    // Per-channel S-curve
    float3 result;
    for (int i = 0; i < 3; ++i) {
        float x = saturate(color[i]);
        // Midpoint pivot-based contrast
        float mid = 0.5;
        x = mid + (x - mid) * cb.contrast;
        // Soft clamp with smoothstep at edges
        x = smoothstep(0.0, 1.0, x);
        result[i] = x;
    }
    return result;
}

// ─── Split Toning ────────────────────────────────────────────────────────

float3 ApplySplitToning(float3 color) {
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));

    // Shadow/highlight blend based on luminance and balance
    float shadowMask = smoothstep(0.5 + cb.splitBalance * 0.5, 0.0, lum);
    float highlightMask = smoothstep(0.5 - cb.splitBalance * 0.5, 1.0, lum);

    // Tint: blend toward tint color in shadows/highlights
    float3 shadowBlend = lerp(color, color * cb.shadowTint * 2.0, shadowMask * 0.3);
    float3 highlightBlend = lerp(color, color * cb.highlightTint * 2.0, highlightMask * 0.3);

    return lerp(shadowBlend, highlightBlend, lum);
}

// ─── Film Grain ──────────────────────────────────────────────────────────

float3 ApplyFilmGrain(float3 color, float2 uv) {
    if (cb.grainIntensity <= 0.0) return color;

    float2 grainUV = uv * cb.resolution / cb.grainSize;
    float noise = frac(sin(dot(grainUV + cb.time * 0.1, float2(12.9898, 78.233))) * 43758.5453);
    noise = (noise - 0.5) * cb.grainIntensity;

    // Grain is stronger in midtones
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
    float grainMask = 1.0 - abs(lum - 0.5) * 2.0;
    grainMask = max(grainMask, 0.3);

    return color + noise * grainMask;
}

// ─── Tonemapping ─────────────────────────────────────────────────────────

float3 ACESFilm(float3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 Reinhard(float3 x) {
    return x / (1.0 + x);
}

float3 Uncharted2Partial(float3 x) {
    float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 Uncharted2(float3 x) {
    float3 curr = Uncharted2Partial(x * 2.0);
    float3 whiteScale = 1.0 / Uncharted2Partial(float3(11.2, 11.2, 11.2));
    return curr * whiteScale;
}

float3 ApplyTonemapping(float3 color) {
    // Exposure
    color *= exp2(cb.exposure);

    if (cb.tonemapMode == 1) return ACESFilm(color);
    if (cb.tonemapMode == 2) return Reinhard(color);
    if (cb.tonemapMode == 3) return Uncharted2(color);

    return saturate(color);
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;
    float3 color = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;

    // ── Pipeline order matters ───────────────────────────────────────
    color = ApplyWhiteBalance(color);
    color = ApplyLiftGammaGain(color);
    color = ApplyChannelMixer(color);
    color = ApplySaturationVibrance(color);
    color = ApplyHueShift(color);
    color = ApplyToneCurve(color);
    color = ApplySplitToning(color);
    color = ApplyTonemapping(color);
    color = ApplyFilmGrain(color, uv);

    color = max(color, 0.0);

    g_Output[DTid.xy] = float4(color, 1.0);
}
