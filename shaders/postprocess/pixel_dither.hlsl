// ─── Procedural Pixel Art Dithering Effect Shader ────────────────────────
// Screen-space post-process for retro pixel art aesthetics with ordered
// dithering, color palette reduction, and pixelation.
//
// Features:
//   - Ordered dithering (Bayer matrix 2x2, 4x4, 8x8)
//   - Color palette reduction (configurable palette size)
//   - Pixelation (downscale + nearest-neighbor)
//   - Blue noise dithering option
//   - Custom color palette support (indexed palette)
//   - Brightness-based dither pattern selection
//   - Configurable pixel scale and color depth
//   - Optional edge-aware dithering (preserves outlines)
//
// References:
//   - "Return of the Obra Dinn" (Lucas Pope, 2018)
//   - "Ordered Dithering" (Bayer, 1973)
//   - "Pixel Art in Modern Games" (Celeste, Hyper Light Drifter)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor : register(t0);
Texture2D<float>  g_SceneDepth : register(t1);
Texture2D<float>  g_BlueNoise  : register(t2);

SamplerState g_PointClamp  : register(s0);
SamplerState g_LinearClamp : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct PixelDitherConstants {
    float2 resolution;
    float2 invResolution;
    float  time;
    u32    pixelScale;           // Pixelation factor (default 4)
    u32    colorDepth;           // Bits per channel (1-8, default 3)
    u32    ditherMode;           // 0=Bayer4x4, 1=Bayer8x8, 2=BlueNoise, 3=None
    float  ditherStrength;       // Dither pattern intensity (default 1.0)
    float  saturationBoost;      // Color saturation multiplier (default 1.1)
    float  contrastBoost;        // Contrast multiplier (default 1.1)
    float  brightnessOffset;     // Brightness adjustment (default 0.0)
    u32    paletteMode;          // 0=quantize, 1=custom palette (default 0)
    u32    paletteSize;          // Number of colors in custom palette (max 16)
    float  edgePreserve;         // Edge-aware dithering strength (default 0.0)
    float  pad0;
};

[[vk::push_constant]] ConstantBuffer<PixelDitherConstants> cb;

// Custom palette (up to 16 colors)
StructuredBuffer<float4> g_Palette : register(t3);

// ─── Bayer Dither Matrices ───────────────────────────────────────────────

static const float Bayer4x4[16] = {
     0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
    12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
     3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
    15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
};

static const float Bayer8x8[64] = {
     0.0/64.0, 32.0/64.0,  8.0/64.0, 40.0/64.0,  2.0/64.0, 34.0/64.0, 10.0/64.0, 42.0/64.0,
    48.0/64.0, 16.0/64.0, 56.0/64.0, 24.0/64.0, 50.0/64.0, 18.0/64.0, 58.0/64.0, 26.0/64.0,
    12.0/64.0, 44.0/64.0,  4.0/64.0, 36.0/64.0, 14.0/64.0, 46.0/64.0,  6.0/64.0, 38.0/64.0,
    60.0/64.0, 28.0/64.0, 52.0/64.0, 20.0/64.0, 62.0/64.0, 30.0/64.0, 54.0/64.0, 22.0/64.0,
     3.0/64.0, 35.0/64.0, 11.0/64.0, 43.0/64.0,  1.0/64.0, 33.0/64.0,  9.0/64.0, 41.0/64.0,
    51.0/64.0, 19.0/64.0, 59.0/64.0, 27.0/64.0, 49.0/64.0, 17.0/64.0, 57.0/64.0, 25.0/64.0,
    15.0/64.0, 47.0/64.0,  7.0/64.0, 39.0/64.0, 13.0/64.0, 45.0/64.0,  5.0/64.0, 37.0/64.0,
    63.0/64.0, 31.0/64.0, 55.0/64.0, 23.0/64.0, 61.0/64.0, 29.0/64.0, 53.0/64.0, 21.0/64.0
};

float GetBayer4x4(uint2 pos) {
    uint idx = (pos.y % 4u) * 4u + (pos.x % 4u);
    return Bayer4x4[idx];
}

float GetBayer8x8(uint2 pos) {
    uint idx = (pos.y % 8u) * 8u + (pos.x % 8u);
    return Bayer8x8[idx];
}

// ─── Dither Value ────────────────────────────────────────────────────────

float GetDitherValue(uint2 pixelPos) {
    switch (cb.ditherMode) {
        case 0: return GetBayer4x4(pixelPos) - 0.5;
        case 1: return GetBayer8x8(pixelPos) - 0.5;
        case 2: {
            float2 noiseUV = float2(pixelPos) / 256.0; // Tile blue noise
            return g_BlueNoise.SampleLevel(g_PointClamp, noiseUV, 0) - 0.5;
        }
        default: return 0.0;
    }
}

// ─── Color Quantization ─────────────────────────────────────────────────

float3 QuantizeToDepth(float3 color, u32 bits) {
    float levels = float((1u << bits) - 1u);
    return floor(color * levels + 0.5) / levels;
}

// ─── Palette Matching ────────────────────────────────────────────────────

float3 FindClosestPaletteColor(float3 color) {
    float bestDist = 1e10;
    float3 bestColor = color;

    u32 count = min(cb.paletteSize, 16u);
    for (u32 i = 0; i < count; ++i) {
        float3 palColor = g_Palette[i].rgb;
        float3 diff = color - palColor;
        float dist = dot(diff, diff);
        if (dist < bestDist) {
            bestDist = dist;
            bestColor = palColor;
        }
    }

    return bestColor;
}

// ─── Edge Detection (Sobel on luminance) ─────────────────────────────────

float EdgeDetect(float2 uv) {
    float2 texel = cb.invResolution;

    float tl = dot(g_SceneColor.SampleLevel(g_PointClamp, uv + float2(-texel.x, -texel.y), 0).rgb, float3(0.299, 0.587, 0.114));
    float tc = dot(g_SceneColor.SampleLevel(g_PointClamp, uv + float2(0, -texel.y), 0).rgb, float3(0.299, 0.587, 0.114));
    float tr = dot(g_SceneColor.SampleLevel(g_PointClamp, uv + float2(texel.x, -texel.y), 0).rgb, float3(0.299, 0.587, 0.114));
    float ml = dot(g_SceneColor.SampleLevel(g_PointClamp, uv + float2(-texel.x, 0), 0).rgb, float3(0.299, 0.587, 0.114));
    float mr = dot(g_SceneColor.SampleLevel(g_PointClamp, uv + float2(texel.x, 0), 0).rgb, float3(0.299, 0.587, 0.114));
    float bl = dot(g_SceneColor.SampleLevel(g_PointClamp, uv + float2(-texel.x, texel.y), 0).rgb, float3(0.299, 0.587, 0.114));
    float bc = dot(g_SceneColor.SampleLevel(g_PointClamp, uv + float2(0, texel.y), 0).rgb, float3(0.299, 0.587, 0.114));
    float br = dot(g_SceneColor.SampleLevel(g_PointClamp, uv + float2(texel.x, texel.y), 0).rgb, float3(0.299, 0.587, 0.114));

    float gx = -tl - 2.0 * ml - bl + tr + 2.0 * mr + br;
    float gy = -tl - 2.0 * tc - tr + bl + 2.0 * bc + br;

    return saturate(sqrt(gx * gx + gy * gy));
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    // ── Pixelation ───────────────────────────────────────────────────
    uint2 pixelBlock = DTid.xy / cb.pixelScale * cb.pixelScale;
    float2 blockCenter = (float2(pixelBlock) + float(cb.pixelScale) * 0.5) * cb.invResolution;

    float3 color = g_SceneColor.SampleLevel(g_PointClamp, blockCenter, 0).rgb;

    // ── Brightness / Contrast ────────────────────────────────────────
    color = (color - 0.5) * cb.contrastBoost + 0.5 + cb.brightnessOffset;
    color = saturate(color);

    // ── Saturation boost ─────────────────────────────────────────────
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(float3(lum, lum, lum), color, cb.saturationBoost);

    // ── Edge-aware dithering ─────────────────────────────────────────
    float edgeFactor = 1.0;
    if (cb.edgePreserve > 0.0) {
        float edge = EdgeDetect(blockCenter);
        edgeFactor = 1.0 - edge * cb.edgePreserve;
    }

    // ── Dithering ────────────────────────────────────────────────────
    float dither = GetDitherValue(pixelBlock / cb.pixelScale);
    float ditherAmount = cb.ditherStrength * edgeFactor;

    // Scale dither by quantization step size
    float levels = float((1u << cb.colorDepth) - 1u);
    float stepSize = 1.0 / levels;

    color += dither * stepSize * ditherAmount;
    color = saturate(color);

    // ── Color quantization ───────────────────────────────────────────
    float3 quantized;
    if (cb.paletteMode == 0) {
        quantized = QuantizeToDepth(color, cb.colorDepth);
    } else {
        quantized = FindClosestPaletteColor(color);
    }

    g_Output[DTid.xy] = float4(quantized, 1.0);
}
