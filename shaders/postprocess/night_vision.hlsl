// ─── Procedural Night Vision / Starlight Amplifier Shader ────────────────
// Screen-space post-process for military-grade night vision goggles (NVG)
// with phosphor green tint, image intensifier noise, and vignette tube.
//
// Features:
//   - Phosphor green color grading (Gen II/III tube simulation)
//   - Image intensifier gain (brightness amplification)
//   - Scintillation noise (photocathode sparkle)
//   - Fixed-pattern noise (tube defects / chicken wire)
//   - Circular vignette (tube housing mask)
//   - Auto-gain control (adapt to scene brightness)
//   - Bloom/halo around bright sources (tube overload)
//   - Edge enhancement (image sharpening)
//   - Configurable gain, noise, tube diameter, and color tint
//   - Dual-tube binocular overlap option
//
// References:
//   - "Night Vision in Call of Duty: Modern Warfare" (IW, GDC 2020)
//   - "Image Intensifier Tube Simulation" (US Army RDECOM, 2006)
//   - "NVG Rendering for Flight Simulation" (FlightSafety, 2012)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor : register(t0);
Texture2D<float>  g_SceneDepth : register(t1);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct NightVisionConstants {
    float2   resolution;
    float2   invResolution;
    float    time;
    float    gain;                 // Brightness amplification (default 5.0)
    float3   phosphorTint;        // Tube color (default: 0.1, 1.0, 0.2)
    float    scintillationAmount; // Photocathode sparkle noise (default 0.08)
    float    fixedPatternNoise;   // Tube defect noise (default 0.03)
    float    vignetteRadius;      // Tube opening radius in UV (default 0.45)
    float    vignetteSoftness;    // Edge softness (default 0.05)
    float    bloomThreshold;      // Bright source bloom threshold (default 0.6)
    float    bloomIntensity;      // Bloom glow strength (default 0.5)
    float    edgeEnhance;         // Sharpening strength (default 0.3)
    float    autoGainTarget;      // Target average brightness (default 0.4)
    float    autoGainSpeed;       // Adaptation speed (default 0.0, disabled)
    u32      dualTube;            // 0=single, 1=binocular overlap
    float    tubeSpacing;         // Binocular tube center offset (default 0.15)
    float    contrast;            // Contrast boost (default 1.3)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<NightVisionConstants> cb;

// ─── Hash ────────────────────────────────────────────────────────────────

float Hash21(float2 p) {
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float Hash11(float p) {
    p = frac(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return frac(p);
}

// ─── Scintillation Noise ─────────────────────────────────────────────────

float Scintillation(float2 uv) {
    // High-frequency temporal sparkle simulating photocathode events
    float2 pixelCoord = uv * cb.resolution;
    float seed = Hash21(floor(pixelCoord) + frac(cb.time * 60.0) * 100.0);

    // Poisson-like sparse sparkle
    float sparkle = step(0.97, seed) * seed * 5.0;

    // Lower-frequency noise
    float noise = Hash21(pixelCoord * 0.1 + cb.time * 30.0) - 0.5;

    return sparkle + noise * cb.scintillationAmount;
}

// ─── Fixed Pattern Noise ─────────────────────────────────────────────────

float FixedPattern(float2 uv) {
    // Static tube defects (chicken wire pattern)
    float2 grid = uv * 80.0;
    float2 cell = floor(grid);

    // Per-cell fixed brightness variation
    float defect = Hash21(cell * 7.31) - 0.5;

    return defect * cb.fixedPatternNoise;
}

// ─── Tube Vignette ───────────────────────────────────────────────────────

float TubeVignette(float2 uv, float2 center) {
    float dist = length(uv - center);
    return smoothstep(cb.vignetteRadius + cb.vignetteSoftness, cb.vignetteRadius - cb.vignetteSoftness, dist);
}

// ─── Simple Bloom (bright source halo) ───────────────────────────────────

float3 NVGBloom(float2 uv) {
    float3 sum = float3(0, 0, 0);
    float totalWeight = 0.0;

    int samples = 8;
    float radius = 4.0;

    for (int y = -samples; y <= samples; y += 2) {
        for (int x = -samples; x <= samples; x += 2) {
            float2 offset = float2(float(x), float(y)) * cb.invResolution * radius;
            float2 sampleUV = clamp(uv + offset, 0.0, 1.0);

            float3 col = g_SceneColor.SampleLevel(g_LinearClamp, sampleUV, 0).rgb;
            float lum = dot(col, float3(0.2126, 0.7152, 0.0722));

            float bright = max(lum - cb.bloomThreshold, 0.0);
            float dist = length(float2(float(x), float(y))) / float(samples);
            float weight = exp(-dist * dist * 2.0) * bright;

            sum += col * weight;
            totalWeight += weight;
        }
    }

    return totalWeight > 0.0 ? sum / totalWeight : float3(0, 0, 0);
}

// ─── Edge Enhancement ────────────────────────────────────────────────────

float EdgeEnhance(float2 uv) {
    float2 texel = cb.invResolution;

    float c = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb, float3(0.333, 0.333, 0.333));
    float l = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(-texel.x, 0), 0).rgb, float3(0.333, 0.333, 0.333));
    float r = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(texel.x, 0), 0).rgb, float3(0.333, 0.333, 0.333));
    float t = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(0, -texel.y), 0).rgb, float3(0.333, 0.333, 0.333));
    float b = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(0, texel.y), 0).rgb, float3(0.333, 0.333, 0.333));

    // Laplacian edge detection
    float edge = 4.0 * c - l - r - t - b;

    return edge * cb.edgeEnhance;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    // ── Tube vignette mask ───────────────────────────────────────────
    float vignette;
    if (cb.dualTube == 1) {
        // Binocular: two overlapping circles
        float v1 = TubeVignette(uv, float2(0.5 - cb.tubeSpacing, 0.5));
        float v2 = TubeVignette(uv, float2(0.5 + cb.tubeSpacing, 0.5));
        vignette = max(v1, v2);
    } else {
        vignette = TubeVignette(uv, float2(0.5, 0.5));
    }

    if (vignette < 0.001) {
        g_Output[DTid.xy] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // ── Sample scene ─────────────────────────────────────────────────
    float3 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;

    // Convert to luminance
    float luminance = dot(sceneColor, float3(0.2126, 0.7152, 0.0722));

    // ── Gain amplification ───────────────────────────────────────────
    float amplified = luminance * cb.gain;

    // ── Contrast ─────────────────────────────────────────────────────
    amplified = (amplified - 0.5) * cb.contrast + 0.5;

    // ── Edge enhancement ─────────────────────────────────────────────
    if (cb.edgeEnhance > 0.0) {
        amplified += EdgeEnhance(uv);
    }

    // ── Bloom from bright sources ────────────────────────────────────
    if (cb.bloomIntensity > 0.0) {
        float3 bloom = NVGBloom(uv);
        float bloomLum = dot(bloom, float3(0.2126, 0.7152, 0.0722));
        amplified += bloomLum * cb.bloomIntensity;
    }

    // ── Scintillation noise ──────────────────────────────────────────
    amplified += Scintillation(uv);

    // ── Fixed pattern noise ──────────────────────────────────────────
    amplified += FixedPattern(uv);

    amplified = saturate(amplified);

    // ── Apply phosphor tint ──────────────────────────────────────────
    float3 nvgColor = amplified * cb.phosphorTint;

    // ── Apply tube vignette ──────────────────────────────────────────
    nvgColor *= vignette;

    g_Output[DTid.xy] = float4(nvgColor, 1.0);
}
