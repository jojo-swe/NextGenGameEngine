// ─── Procedural Glitch / VHS Distortion Effect Shader ────────────────────
// Screen-space post-process for retro VHS tape artifacts, digital glitch
// effects, and analog video distortion.
//
// Features:
//   - Horizontal scanline jitter (per-line UV offset)
//   - RGB channel splitting (chromatic aberration with time variance)
//   - Block glitch (random rectangular UV displacement)
//   - VHS tracking lines (horizontal noise bands)
//   - Analog tape noise (static grain overlay)
//   - Color bleeding / smearing (horizontal blur in bright areas)
//   - Interlace field simulation (alternating line brightness)
//   - Random glitch frame triggers (probability-based)
//   - Vertical roll / sync loss simulation
//   - CRT curvature vignette
//
// References:
//   - "VHS Shader" (keijiro, Unity post-processing)
//   - "Analog TV Artifacts" (Shadertoy community)
//   - "Glitch Art in Games" (Vlambeer, GDC 2013)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor : register(t0);
Texture2D<float>  g_NoiseTex   : register(t1);

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct GlitchVHSConstants {
    float2   resolution;
    float2   invResolution;
    float    time;
    float    scanlineJitter;      // Horizontal jitter amount (default 0.01)
    float    colorSplit;          // RGB split distance (default 0.005)
    float    blockGlitchProb;    // Probability of block glitch per frame (default 0.05)
    float    blockGlitchSize;    // Block glitch size (default 0.1)
    float    trackingNoiseAmount; // VHS tracking line intensity (default 0.3)
    float    trackingNoiseSpeed;  // Tracking line scroll speed (default 5.0)
    float    staticNoiseAmount;   // Static grain intensity (default 0.08)
    float    colorBleedAmount;    // Horizontal color smear (default 0.003)
    float    interlaceAmount;     // Interlace line darkening (default 0.15)
    float    verticalRollSpeed;   // Sync loss roll speed (default 0.0, disabled)
    float    crtCurvature;        // Screen curvature amount (default 0.02)
    float    glitchIntensity;     // Master glitch intensity (default 1.0)
    float    vignetteAmount;      // CRT vignette strength (default 0.3)
    float    saturationLoss;      // VHS desaturation (default 0.1)
    float    brightnessNoise;     // Random brightness flicker (default 0.05)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<GlitchVHSConstants> cb;

// ─── Hash / Noise ────────────────────────────────────────────────────────

float Hash11(float p) {
    p = frac(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return frac(p);
}

float Hash21(float2 p) {
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

// ─── Scanline Jitter ─────────────────────────────────────────────────────
// Per-scanline horizontal UV offset that varies with time.

float ScanlineJitter(float y, float time) {
    float line = floor(y * cb.resolution.y);
    float jitterSeed = Hash21(float2(line, floor(time * 20.0)));

    // Most lines have zero jitter; occasional lines shift
    float jitter = 0.0;
    if (jitterSeed > 0.97) {
        jitter = (Hash11(line + time * 100.0) - 0.5) * 2.0;
    } else if (jitterSeed > 0.93) {
        jitter = (Hash11(line + time * 50.0) - 0.5) * 0.5;
    }

    return jitter * cb.scanlineJitter * cb.glitchIntensity;
}

// ─── Block Glitch ────────────────────────────────────────────────────────
// Random rectangular regions get displaced.

float2 BlockGlitch(float2 uv, float time) {
    float trigger = Hash11(floor(time * 8.0));
    if (trigger > cb.blockGlitchProb) return uv;

    // Determine block region
    float blockY = Hash11(floor(time * 15.0)) * 0.8 + 0.1;
    float blockH = cb.blockGlitchSize * Hash11(floor(time * 12.0) + 1.0);

    if (uv.y > blockY && uv.y < blockY + blockH) {
        float offset = (Hash11(floor(time * 20.0) + 2.0) - 0.5) * 0.2;
        uv.x += offset * cb.glitchIntensity;
    }

    return uv;
}

// ─── VHS Tracking Noise ──────────────────────────────────────────────────
// Horizontal noise bands that scroll vertically.

float TrackingNoise(float2 uv, float time) {
    float scrollY = uv.y + time * cb.trackingNoiseSpeed;
    float noise = g_NoiseTex.SampleLevel(g_LinearWrap, float2(uv.x * 0.5, scrollY * 0.3), 0);

    // Create band structure
    float band = sin(scrollY * 50.0) * 0.5 + 0.5;
    band = pow(band, 8.0); // Narrow bands

    return noise * band * cb.trackingNoiseAmount * cb.glitchIntensity;
}

// ─── Color Bleeding ──────────────────────────────────────────────────────
// Horizontal smear of bright colors (VHS chroma blur).

float3 ColorBleed(float2 uv) {
    float3 color = float3(0, 0, 0);
    float bleedDist = cb.colorBleedAmount * cb.glitchIntensity;

    // Sample horizontal neighbors
    for (int i = -3; i <= 3; ++i) {
        float weight = 1.0 / (1.0 + abs(float(i)));
        float2 sampleUV = uv + float2(float(i) * bleedDist, 0.0);
        sampleUV = clamp(sampleUV, 0.0, 1.0);
        color += g_SceneColor.SampleLevel(g_LinearClamp, sampleUV, 0).rgb * weight;
    }

    return color / 4.2; // Normalize
}

// ─── CRT Curvature ───────────────────────────────────────────────────────

float2 CRTCurve(float2 uv) {
    float2 centered = uv * 2.0 - 1.0;
    float2 offset = centered * centered * cb.crtCurvature;
    return (centered + centered * offset) * 0.5 + 0.5;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    // ── CRT curvature ────────────────────────────────────────────────
    float2 curvedUV = CRTCurve(uv);

    // Check if outside screen after curvature
    if (curvedUV.x < 0.0 || curvedUV.x > 1.0 || curvedUV.y < 0.0 || curvedUV.y > 1.0) {
        g_Output[DTid.xy] = float4(0, 0, 0, 1);
        return;
    }

    // ── Vertical roll (sync loss) ────────────────────────────────────
    float2 rolledUV = curvedUV;
    if (cb.verticalRollSpeed > 0.0) {
        rolledUV.y = frac(rolledUV.y + cb.time * cb.verticalRollSpeed);
    }

    // ── Block glitch ─────────────────────────────────────────────────
    float2 glitchedUV = BlockGlitch(rolledUV, cb.time);

    // ── Scanline jitter ──────────────────────────────────────────────
    float jitter = ScanlineJitter(glitchedUV.y, cb.time);
    glitchedUV.x += jitter;
    glitchedUV.x = clamp(glitchedUV.x, 0.0, 1.0);

    // ── RGB channel split (chromatic aberration) ─────────────────────
    float splitDist = cb.colorSplit * cb.glitchIntensity;
    // Vary split over time
    float splitVar = sin(cb.time * 3.0) * 0.5 + 0.5;
    splitDist *= 0.5 + splitVar;

    float r = g_SceneColor.SampleLevel(g_LinearClamp,
        clamp(glitchedUV + float2(splitDist, 0.0), 0.0, 1.0), 0).r;
    float g = g_SceneColor.SampleLevel(g_LinearClamp, glitchedUV, 0).g;
    float b = g_SceneColor.SampleLevel(g_LinearClamp,
        clamp(glitchedUV - float2(splitDist, 0.0), 0.0, 1.0), 0).b;

    float3 color = float3(r, g, b);

    // ── Color bleeding ───────────────────────────────────────────────
    if (cb.colorBleedAmount > 0.0) {
        float3 bled = ColorBleed(glitchedUV);
        color = lerp(color, bled, 0.3 * cb.glitchIntensity);
    }

    // ── VHS tracking noise ───────────────────────────────────────────
    float tracking = TrackingNoise(curvedUV, cb.time);
    color += tracking;

    // ── Static noise (grain) ─────────────────────────────────────────
    float staticNoise = g_NoiseTex.SampleLevel(g_LinearWrap,
        uv * 100.0 + cb.time * float2(17.3, 13.7), 0);
    staticNoise = (staticNoise - 0.5) * cb.staticNoiseAmount * cb.glitchIntensity;
    color += staticNoise;

    // ── Brightness flicker ───────────────────────────────────────────
    float flicker = 1.0 + (Hash11(floor(cb.time * 24.0)) - 0.5) * cb.brightnessNoise * cb.glitchIntensity;
    color *= flicker;

    // ── Interlace lines ──────────────────────────────────────────────
    float scanline = float(DTid.y % 2u);
    float interlace = 1.0 - scanline * cb.interlaceAmount;
    color *= interlace;

    // ── VHS saturation loss ──────────────────────────────────────────
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(color, float3(lum, lum, lum), cb.saturationLoss * cb.glitchIntensity);

    // ── CRT vignette ─────────────────────────────────────────────────
    float2 vigUV = curvedUV * 2.0 - 1.0;
    float vignette = 1.0 - dot(vigUV, vigUV) * cb.vignetteAmount;
    color *= saturate(vignette);

    // ── Clamp output ─────────────────────────────────────────────────
    color = max(color, float3(0, 0, 0));

    g_Output[DTid.xy] = float4(color, 1.0);
}
