// ─── Film Noir / Black-and-White Cinematic Shader ────────────────────────
// Screen-space post-process that creates a classic film noir look with
// high-contrast black-and-white, dramatic shadows, and vintage film grain.
//
// Features:
//   - Desaturation with configurable luminance weights
//   - High-contrast S-curve for dramatic shadows
//   - Film grain (temporal, luminance-aware)
//   - Vignette darkening
//   - Selective color accent (keep one hue in color)
//   - Halation / bloom glow on highlights
//   - Crushed blacks / lifted whites
//   - Sepia / warm tone option
//   - Scratch lines (vertical film scratches)
//   - Flicker (temporal brightness variation)

#include "../common/math.hlsl"

Texture2D<float4> g_SceneColor : register(t0);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct FilmNoirConstants {
    float2   resolution;
    float2   invResolution;
    float    time;

    float    contrast;            // S-curve contrast (default: 1.5)
    float    brightness;          // Brightness offset (default: 0.0)
    float    blackPoint;          // Crush blacks below this (default: 0.05)
    float    whitePoint;          // Clip whites above this (default: 0.95)

    // Grain
    float    grainIntensity;      // Film grain strength (default: 0.15)
    float    grainSize;           // Grain particle size (default: 1.5)

    // Vignette
    float    vignetteIntensity;   // Edge darkening (default: 0.6)
    float    vignetteRadius;      // Vignette outer radius (default: 0.8)

    // Selective color
    float    selectiveHue;        // Hue to keep in color, degrees (default: -1 = disabled)
    float    selectiveRange;      // Hue range to keep (default: 30.0)
    float    selectiveSaturation; // Saturation of kept hue (default: 1.0)

    // Tone
    u32      toneMode;            // 0=pure B&W, 1=sepia, 2=cool blue, 3=warm amber
    float    toneIntensity;       // Tone color strength (default: 0.3)

    // Halation
    float    halationIntensity;   // Highlight glow (default: 0.1)
    float    halationThreshold;   // Brightness threshold for glow (default: 0.7)

    // Film artifacts
    float    scratchIntensity;    // Vertical scratch lines (default: 0.05)
    float    flickerIntensity;    // Temporal brightness flicker (default: 0.03)

    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<FilmNoirConstants> cb;

// ─── RGB to HSV ──────────────────────────────────────────────────────────

float3 RGBtoHSV(float3 rgb) {
    float cMax = max(rgb.r, max(rgb.g, rgb.b));
    float cMin = min(rgb.r, min(rgb.g, rgb.b));
    float delta = cMax - cMin;

    float h = 0.0;
    if (delta > 0.0001) {
        if (cMax == rgb.r) h = fmod((rgb.g - rgb.b) / delta, 6.0);
        else if (cMax == rgb.g) h = (rgb.b - rgb.r) / delta + 2.0;
        else h = (rgb.r - rgb.g) / delta + 4.0;
        h *= 60.0;
        if (h < 0.0) h += 360.0;
    }

    float s = cMax > 0.0001 ? delta / cMax : 0.0;
    return float3(h, s, cMax);
}

// ─── Noise ───────────────────────────────────────────────────────────────

float Hash21(float2 p) {
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

// ─── S-Curve Contrast ────────────────────────────────────────────────────

float SCurve(float x, float contrast) {
    x = saturate(x);
    float mid = 0.5;
    x = mid + (x - mid) * contrast;
    return smoothstep(0.0, 1.0, x);
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;
    float3 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;

    // ── Luminance ────────────────────────────────────────────────────
    float lum = dot(sceneColor, float3(0.2126, 0.7152, 0.0722));

    // ── Selective color (keep one hue) ───────────────────────────────
    float3 bw = float3(lum, lum, lum);
    float3 baseColor = bw;

    if (cb.selectiveHue >= 0.0) {
        float3 hsv = RGBtoHSV(sceneColor);
        float hueDist = abs(hsv.x - cb.selectiveHue);
        if (hueDist > 180.0) hueDist = 360.0 - hueDist;

        if (hueDist < cb.selectiveRange && hsv.y > 0.1) {
            float mask = smoothstep(cb.selectiveRange, cb.selectiveRange * 0.3, hueDist);
            baseColor = lerp(bw, sceneColor * cb.selectiveSaturation, mask);
        }
    }

    // ── Contrast S-curve ─────────────────────────────────────────────
    float lumCurved = SCurve(lum, cb.contrast);
    lumCurved += cb.brightness;

    // ── Black/white point crush ──────────────────────────────────────
    lumCurved = smoothstep(cb.blackPoint, cb.whitePoint, lumCurved);

    // Apply contrast to color
    float3 color = baseColor * (lumCurved / max(lum, 0.001));
    color = saturate(color);

    // ── Tone coloring ────────────────────────────────────────────────
    float3 toneColor;
    if (cb.toneMode == 1) toneColor = float3(1.0, 0.87, 0.67);       // Sepia
    else if (cb.toneMode == 2) toneColor = float3(0.7, 0.8, 1.0);    // Cool blue
    else if (cb.toneMode == 3) toneColor = float3(1.0, 0.9, 0.7);    // Warm amber
    else toneColor = float3(1, 1, 1);                                  // Pure B&W

    if (cb.toneMode > 0) {
        color = lerp(color, color * toneColor, cb.toneIntensity);
    }

    // ── Halation (highlight glow) ────────────────────────────────────
    if (cb.halationIntensity > 0.0) {
        // Simple box blur of bright areas
        float3 glow = float3(0, 0, 0);
        float count = 0.0;
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                float2 sampleUV = uv + float2(dx, dy) * cb.invResolution * 3.0;
                float3 s = g_SceneColor.SampleLevel(g_LinearClamp, sampleUV, 0).rgb;
                float sLum = dot(s, float3(0.2126, 0.7152, 0.0722));
                if (sLum > cb.halationThreshold) {
                    glow += s;
                    count += 1.0;
                }
            }
        }
        if (count > 0.0) {
            glow /= count;
            color += glow * cb.halationIntensity;
        }
    }

    // ── Vignette ─────────────────────────────────────────────────────
    if (cb.vignetteIntensity > 0.0) {
        float2 d = uv - 0.5;
        float aspect = cb.resolution.x / cb.resolution.y;
        d.x *= aspect;
        float dist = length(d);
        float vig = smoothstep(cb.vignetteRadius * 0.5, cb.vignetteRadius, dist);
        color *= 1.0 - vig * cb.vignetteIntensity;
    }

    // ── Film grain ───────────────────────────────────────────────────
    if (cb.grainIntensity > 0.0) {
        float2 grainUV = uv * cb.resolution / cb.grainSize;
        float noise = Hash21(grainUV + frac(cb.time * 7.0));
        noise = (noise - 0.5) * cb.grainIntensity;
        // Grain stronger in midtones
        float grainMask = 1.0 - abs(lumCurved - 0.5) * 2.0;
        color += noise * max(grainMask, 0.2);
    }

    // ── Film scratches ───────────────────────────────────────────────
    if (cb.scratchIntensity > 0.0) {
        float scratchX = Hash21(float2(floor(cb.time * 12.0), 0.0));
        float scratchDist = abs(uv.x - scratchX);
        float scratch = smoothstep(0.002, 0.0, scratchDist);
        scratch *= Hash21(float2(uv.y * 100.0, cb.time)) > 0.3 ? 1.0 : 0.0;
        color += scratch * cb.scratchIntensity;
    }

    // ── Flicker ──────────────────────────────────────────────────────
    if (cb.flickerIntensity > 0.0) {
        float flicker = Hash21(float2(floor(cb.time * 24.0), 42.0));
        flicker = (flicker - 0.5) * cb.flickerIntensity;
        color += flicker;
    }

    color = saturate(color);

    g_Output[DTid.xy] = float4(color, 1.0);
}
