// ─── Procedural Scanline / CRT Monitor Effect Shader ─────────────────────
// Screen-space post-process for retro CRT monitor aesthetics with
// scanlines, phosphor grid, barrel distortion, and signal artifacts.
//
// Features:
//   - Horizontal scanlines with configurable thickness and gap
//   - Phosphor RGB sub-pixel grid simulation
//   - Barrel distortion (CRT curvature)
//   - Vignette darkening at screen edges
//   - Color bleeding between adjacent pixels
//   - Interlace flicker (odd/even line alternation)
//   - Signal noise (analog static)
//   - Brightness and contrast adjustment
//   - Configurable curvature, scanline intensity, phosphor visibility
//
// References:
//   - "CRT Simulation" (Lottes, 2013)
//   - "A Look at CRT Shaders" (libretro/RetroArch)
//   - "Cathode Ray Tube Rendering" (Themaister, 2014)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor : register(t0);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct CRTScanlineConstants {
    float2   resolution;
    float2   invResolution;
    float    time;
    float    scanlineIntensity;    // Scanline darkness (0-1, default 0.3)
    float    scanlineWidth;        // Scanline thickness in pixels (default 1.0)
    float    phosphorIntensity;    // Phosphor grid visibility (0-1, default 0.15)
    float    curvatureAmount;      // Barrel distortion strength (default 0.03)
    float    vignetteStrength;     // Edge darkening (default 0.3)
    float    colorBleed;           // Horizontal color bleeding (default 0.002)
    float    brightness;           // Brightness multiplier (default 1.1)
    float    contrast;             // Contrast multiplier (default 1.1)
    float    noiseAmount;          // Analog static noise (default 0.03)
    float    interlaceFlicker;     // Interlace effect strength (default 0.0)
    float    cornerRadius;         // Rounded corner radius (default 0.02)
    float    pad0;
    float    pad1;
};

[[vk::push_constant]] ConstantBuffer<CRTScanlineConstants> cb;

// ─── Hash ────────────────────────────────────────────────────────────────

float Hash11(float p) {
    p = frac(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return frac(p);
}

// ─── Barrel Distortion ───────────────────────────────────────────────────

float2 BarrelDistort(float2 uv) {
    float2 centered = uv * 2.0 - 1.0;

    float r2 = dot(centered, centered);
    float distortion = 1.0 + r2 * cb.curvatureAmount + r2 * r2 * cb.curvatureAmount * 0.5;

    centered *= distortion;

    return centered * 0.5 + 0.5;
}

// ─── Scanlines ───────────────────────────────────────────────────────────

float Scanline(float2 uv) {
    float y = uv.y * cb.resolution.y;

    // Scanline pattern
    float scanline = sin(y * 3.14159 / cb.scanlineWidth) * 0.5 + 0.5;
    scanline = pow(scanline, 1.5);

    // Interlace: shift pattern by half a line on alternating frames
    if (cb.interlaceFlicker > 0.0) {
        float frame = floor(cb.time * 60.0); // 60 fps
        float interlaceOffset = fmod(frame, 2.0) * 0.5 / cb.resolution.y;
        float scanline2 = sin((y + interlaceOffset * cb.resolution.y) * 3.14159 / cb.scanlineWidth) * 0.5 + 0.5;
        scanline2 = pow(scanline2, 1.5);
        scanline = lerp(scanline, scanline2, cb.interlaceFlicker);
    }

    return 1.0 - cb.scanlineIntensity * (1.0 - scanline);
}

// ─── Phosphor Grid ───────────────────────────────────────────────────────

float3 PhosphorMask(float2 uv) {
    float2 pixel = uv * cb.resolution;
    int col = int(fmod(pixel.x, 3.0));

    float3 mask = float3(1, 1, 1);

    // RGB phosphor triad pattern
    if (col == 0) mask = float3(1.0, 0.5, 0.5);
    else if (col == 1) mask = float3(0.5, 1.0, 0.5);
    else mask = float3(0.5, 0.5, 1.0);

    return lerp(float3(1, 1, 1), mask, cb.phosphorIntensity);
}

// ─── Vignette ────────────────────────────────────────────────────────────

float Vignette(float2 uv) {
    float2 centered = uv * 2.0 - 1.0;
    float dist = length(centered);
    return 1.0 - dist * dist * cb.vignetteStrength;
}

// ─── Rounded Corners ─────────────────────────────────────────────────────

float RoundedCorner(float2 uv) {
    float2 centered = abs(uv * 2.0 - 1.0);
    float2 corner = max(centered - (1.0 - cb.cornerRadius), 0.0);
    float dist = length(corner) / cb.cornerRadius;
    return smoothstep(1.0, 0.8, dist);
}

// ─── Color Bleeding ──────────────────────────────────────────────────────

float3 ColorBleed(float2 uv) {
    float offset = cb.colorBleed;

    float r = g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(-offset, 0), 0).r;
    float g = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).g;
    float b = g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(offset, 0), 0).b;

    return float3(r, g, b);
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    // ── Barrel distortion ────────────────────────────────────────────
    float2 distortedUV = BarrelDistort(uv);

    // ── Out-of-bounds check (CRT bezel) ──────────────────────────────
    if (any(distortedUV < 0.0) || any(distortedUV > 1.0)) {
        g_Output[DTid.xy] = float4(0.02, 0.02, 0.02, 1.0); // Dark bezel
        return;
    }

    // ── Sample scene with color bleeding ─────────────────────────────
    float3 color;
    if (cb.colorBleed > 0.0) {
        color = ColorBleed(distortedUV);
    } else {
        color = g_SceneColor.SampleLevel(g_LinearClamp, distortedUV, 0).rgb;
    }

    // ── Brightness / Contrast ────────────────────────────────────────
    color = (color - 0.5) * cb.contrast + 0.5;
    color *= cb.brightness;

    // ── Scanlines ────────────────────────────────────────────────────
    float scanline = Scanline(distortedUV);
    color *= scanline;

    // ── Phosphor mask ────────────────────────────────────────────────
    float3 phosphor = PhosphorMask(distortedUV);
    color *= phosphor;

    // ── Analog noise ─────────────────────────────────────────────────
    if (cb.noiseAmount > 0.0) {
        float noise = Hash11(distortedUV.x * 1000.0 + distortedUV.y * 10000.0 + cb.time * 100.0);
        noise = (noise - 0.5) * cb.noiseAmount;
        color += noise;
    }

    // ── Vignette ─────────────────────────────────────────────────────
    float vignette = Vignette(distortedUV);
    color *= vignette;

    // ── Rounded corners ──────────────────────────────────────────────
    float corner = RoundedCorner(distortedUV);
    color *= corner;

    color = saturate(color);

    g_Output[DTid.xy] = float4(color, 1.0);
}
