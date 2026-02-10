// ─── Procedural Thermal / Infrared Vision Effect Shader ──────────────────
// Screen-space post-process for thermal imaging and infrared vision with
// heat-based color mapping, edge detection, and noise simulation.
//
// Features:
//   - Luminance-to-heat color ramp (black-body or custom palette)
//   - Multiple palette modes: White-Hot, Black-Hot, Iron, Rainbow
//   - Edge detection overlay (Sobel filter on depth)
//   - Sensor noise simulation (temporal + spatial)
//   - Heat source highlighting (emissive regions glow brighter)
//   - Depth-based temperature falloff (distant objects cooler)
//   - Vignette and scan-line artifacts
//   - Configurable sensitivity, contrast, and noise level
//   - Animated noise pattern for realism
//
// References:
//   - "Thermal Vision in Splinter Cell" (Ubisoft, GDC 2005)
//   - "Infrared Rendering for Military Simulation" (I/ITSEC 2010)
//   - "FLIR Sensor Simulation" (Raytheon, 2008)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor   : register(t0);
Texture2D<float>  g_SceneDepth   : register(t1);
Texture2D<float4> g_EmissiveMask : register(t2);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct ThermalVisionConstants {
    float2   resolution;
    float2   invResolution;
    float    time;
    u32      paletteMode;          // 0=WhiteHot, 1=BlackHot, 2=Iron, 3=Rainbow
    float    sensitivity;          // Heat sensitivity multiplier (default 1.0)
    float    contrast;             // Contrast adjustment (default 1.2)
    float    noiseAmount;          // Sensor noise intensity (default 0.05)
    float    noiseSpeed;           // Noise animation speed (default 10.0)
    float    edgeIntensity;        // Edge overlay strength (default 0.3)
    float    depthFalloff;         // Temperature decrease with distance (default 0.5)
    float    emissiveBoost;        // Extra heat for emissive regions (default 2.0)
    float    vignetteStrength;     // Edge darkening (default 0.2)
    float    scanlineIntensity;    // Scan-line artifact (default 0.05)
    float    brightnessOffset;     // Base temperature offset (default 0.0)
    float    pad0;
    float    pad1;
};

[[vk::push_constant]] ConstantBuffer<ThermalVisionConstants> cb;

// ─── Hash ────────────────────────────────────────────────────────────────

float Hash11(float p) {
    p = frac(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return frac(p);
}

float Hash21(float2 p) {
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

// ─── Color Palettes ──────────────────────────────────────────────────────

float3 WhiteHotPalette(float t) {
    // Black -> dark blue -> white
    float3 cold = float3(0.0, 0.0, 0.1);
    float3 mid = float3(0.3, 0.3, 0.4);
    float3 hot = float3(1.0, 1.0, 1.0);

    if (t < 0.5) return lerp(cold, mid, t * 2.0);
    return lerp(mid, hot, (t - 0.5) * 2.0);
}

float3 BlackHotPalette(float t) {
    // White -> grey -> black (inverted)
    return float3(1.0 - t, 1.0 - t, 1.0 - t);
}

float3 IronPalette(float t) {
    // Black -> purple -> red -> orange -> yellow -> white
    float3 c0 = float3(0.0, 0.0, 0.0);
    float3 c1 = float3(0.3, 0.0, 0.5);
    float3 c2 = float3(0.8, 0.1, 0.1);
    float3 c3 = float3(1.0, 0.5, 0.0);
    float3 c4 = float3(1.0, 1.0, 0.3);
    float3 c5 = float3(1.0, 1.0, 1.0);

    if (t < 0.2) return lerp(c0, c1, t * 5.0);
    if (t < 0.4) return lerp(c1, c2, (t - 0.2) * 5.0);
    if (t < 0.6) return lerp(c2, c3, (t - 0.4) * 5.0);
    if (t < 0.8) return lerp(c3, c4, (t - 0.6) * 5.0);
    return lerp(c4, c5, (t - 0.8) * 5.0);
}

float3 RainbowPalette(float t) {
    // Blue -> cyan -> green -> yellow -> red
    float3 c0 = float3(0.0, 0.0, 0.5);
    float3 c1 = float3(0.0, 0.5, 1.0);
    float3 c2 = float3(0.0, 1.0, 0.0);
    float3 c3 = float3(1.0, 1.0, 0.0);
    float3 c4 = float3(1.0, 0.0, 0.0);

    if (t < 0.25) return lerp(c0, c1, t * 4.0);
    if (t < 0.5) return lerp(c1, c2, (t - 0.25) * 4.0);
    if (t < 0.75) return lerp(c2, c3, (t - 0.5) * 4.0);
    return lerp(c3, c4, (t - 0.75) * 4.0);
}

float3 ApplyPalette(float heat) {
    heat = saturate(heat);

    if (cb.paletteMode == 0) return WhiteHotPalette(heat);
    if (cb.paletteMode == 1) return BlackHotPalette(heat);
    if (cb.paletteMode == 2) return IronPalette(heat);
    return RainbowPalette(heat);
}

// ─── Sobel Edge Detection ────────────────────────────────────────────────

float SobelEdge(float2 uv) {
    float2 texel = cb.invResolution;

    float tl = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2(-texel.x, -texel.y), 0);
    float tc = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2(0, -texel.y), 0);
    float tr = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2(texel.x, -texel.y), 0);
    float ml = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2(-texel.x, 0), 0);
    float mr = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2(texel.x, 0), 0);
    float bl = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2(-texel.x, texel.y), 0);
    float bc = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2(0, texel.y), 0);
    float br = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2(texel.x, texel.y), 0);

    float gx = -tl - 2.0 * ml - bl + tr + 2.0 * mr + br;
    float gy = -tl - 2.0 * tc - tr + bl + 2.0 * bc + br;

    return sqrt(gx * gx + gy * gy);
}

// ─── Sensor Noise ────────────────────────────────────────────────────────

float SensorNoise(float2 uv) {
    float spatial = Hash21(uv * cb.resolution + frac(cb.time * cb.noiseSpeed) * 1000.0);
    float temporal = Hash11(cb.time * cb.noiseSpeed + uv.x * 100.0 + uv.y * 10000.0);

    return (spatial * 0.7 + temporal * 0.3 - 0.5) * cb.noiseAmount;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float4 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);
    float depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
    float4 emissive = g_EmissiveMask.SampleLevel(g_LinearClamp, uv, 0);

    // ── Compute heat value ───────────────────────────────────────────
    // Base heat from scene luminance
    float luminance = dot(sceneColor.rgb, float3(0.2126, 0.7152, 0.0722));
    float heat = luminance * cb.sensitivity;

    // Emissive regions are hotter
    float emissiveLum = dot(emissive.rgb, float3(0.2126, 0.7152, 0.0722));
    heat += emissiveLum * cb.emissiveBoost;

    // Depth falloff: distant objects appear cooler
    heat -= depth * cb.depthFalloff;

    // Brightness offset
    heat += cb.brightnessOffset;

    // Contrast
    heat = (heat - 0.5) * cb.contrast + 0.5;

    // ── Sensor noise ─────────────────────────────────────────────────
    heat += SensorNoise(uv);

    // ── Apply color palette ──────────────────────────────────────────
    float3 thermalColor = ApplyPalette(heat);

    // ── Edge detection overlay ───────────────────────────────────────
    if (cb.edgeIntensity > 0.0) {
        float edge = SobelEdge(uv);
        edge = saturate(edge * 50.0) * cb.edgeIntensity;
        thermalColor += float3(edge, edge, edge);
    }

    // ── Scan-line artifact ───────────────────────────────────────────
    if (cb.scanlineIntensity > 0.0) {
        float scanline = sin(uv.y * cb.resolution.y * 3.14159) * 0.5 + 0.5;
        thermalColor *= 1.0 - cb.scanlineIntensity * (1.0 - scanline);
    }

    // ── Vignette ─────────────────────────────────────────────────────
    float2 centered = uv * 2.0 - 1.0;
    float vignette = 1.0 - dot(centered, centered) * cb.vignetteStrength;
    thermalColor *= vignette;

    thermalColor = saturate(thermalColor);

    g_Output[DTid.xy] = float4(thermalColor, 1.0);
}
