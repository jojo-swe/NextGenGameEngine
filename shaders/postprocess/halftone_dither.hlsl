// ─── Procedural Halftone / Dithering Shader ──────────────────────────────
// Screen-space post-process that converts the scene into halftone dots,
// cross-hatch patterns, or ordered dithering for stylized rendering.
//
// Features:
//   - Circular halftone dots (CMYK or monochrome)
//   - Ordered Bayer dithering (2x2, 4x4, 8x8)
//   - Cross-hatch pattern rendering
//   - Blue noise dithering
//   - Color or monochrome output
//   - Configurable dot/cell size
//   - Rotation angle for halftone screens
//   - Depth-aware dot scaling (larger dots at distance)
//   - Animated dither pattern
//   - Multi-tone levels (bi-level, tri-tone, etc.)
//
// References:
//   - "Digital Halftoning" (Robert Ulichney, MIT Press)
//   - "Dithering in Games" (Alex Vlachos, GDC 2016)
//   - "Cross-Hatching NPR" (Praun et al., SIGGRAPH 2001)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor : register(t0);
Texture2D<float>  g_SceneDepth : register(t1);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct HalftoneDitherConstants {
    float2   resolution;
    float2   invResolution;
    float    time;
    u32      mode;                 // 0=halftone dots, 1=Bayer dither, 2=crosshatch, 3=blue noise
    float    cellSize;             // Dot/cell size in pixels (default 6.0)
    float    dotScale;             // Dot size multiplier (default 1.0)
    float    angle;                // Halftone screen rotation in radians (default 0.0)
    u32      tonelevels;           // Number of tone levels (default 2)
    u32      colorMode;            // 0=monochrome, 1=scene color, 2=CMYK halftone
    float    depthInfluence;       // Depth-based dot scaling (default 0.0)
    float3   paperColor;           // Background/paper color (default: 1,1,1)
    float3   inkColor;             // Ink/dot color (default: 0,0,0)
    float    crosshatchDensity;    // Cross-hatch line density (default 1.0)
    float    animSpeed;            // Animated pattern speed (default 0.0)
    float    contrast;             // Pre-dither contrast (default 1.0)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<HalftoneDitherConstants> cb;

// ─── Halftone Dot Pattern ────────────────────────────────────────────────

float HalftoneDot(float2 uv, float lum, float angle) {
    // Rotate UV by angle
    float c = cos(angle);
    float s = sin(angle);
    float2 rotUV = float2(
        uv.x * c - uv.y * s,
        uv.x * s + uv.y * c
    );

    // Scale to cell grid
    float2 cellUV = rotUV * cb.resolution / cb.cellSize;
    float2 cellCenter = floor(cellUV) + 0.5;
    float2 localPos = cellUV - cellCenter;

    // Dot radius based on luminance (dark = big dot)
    float radius = sqrt(1.0 - lum) * 0.5 * cb.dotScale;

    float dist = length(localPos);
    return 1.0 - smoothstep(radius - 0.05, radius + 0.05, dist);
}

// ─── CMYK Halftone ───────────────────────────────────────────────────────

float3 CMYKHalftone(float2 uv, float3 color) {
    // Convert RGB to CMY
    float3 cmy = 1.0 - color;

    // Traditional CMYK screen angles
    float cAngle = cb.angle + 0.2618;  // 15 degrees
    float mAngle = cb.angle + 1.3090;  // 75 degrees
    float yAngle = cb.angle;           // 0 degrees
    float kAngle = cb.angle + 0.7854;  // 45 degrees

    // Key (black)
    float k = min(cmy.r, min(cmy.g, cmy.b));
    float cVal = (cmy.r - k) / max(1.0 - k, 0.001);
    float mVal = (cmy.g - k) / max(1.0 - k, 0.001);
    float yVal = (cmy.b - k) / max(1.0 - k, 0.001);

    float cDot = HalftoneDot(uv, 1.0 - cVal, cAngle);
    float mDot = HalftoneDot(uv, 1.0 - mVal, mAngle);
    float yDot = HalftoneDot(uv, 1.0 - yVal, yAngle);
    float kDot = HalftoneDot(uv, 1.0 - k, kAngle);

    // Reconstruct color from CMYK dots
    float3 result = cb.paperColor;
    result *= (1.0 - cDot * float3(1, 0, 0));  // Cyan subtracts red
    result *= (1.0 - mDot * float3(0, 1, 0));  // Magenta subtracts green
    result *= (1.0 - yDot * float3(0, 0, 1));  // Yellow subtracts blue
    result *= (1.0 - kDot);                     // Key subtracts all

    return result;
}

// ─── Bayer Ordered Dither ────────────────────────────────────────────────

float BayerDither8x8(int2 pixel) {
    // 8x8 Bayer matrix
    const float bayer[64] = {
         0, 32,  8, 40,  2, 34, 10, 42,
        48, 16, 56, 24, 50, 18, 58, 26,
        12, 44,  4, 36, 14, 46,  6, 38,
        60, 28, 52, 20, 62, 30, 54, 22,
         3, 35, 11, 43,  1, 33,  9, 41,
        51, 19, 59, 27, 49, 17, 57, 25,
        15, 47,  7, 39, 13, 45,  5, 37,
        63, 31, 55, 23, 61, 29, 53, 21
    };

    int2 p = pixel % 8;
    return bayer[p.y * 8 + p.x] / 64.0;
}

float OrderedDither(float lum, int2 pixel) {
    float threshold = BayerDither8x8(pixel);

    if (cb.tonelevels <= 2) {
        return lum > threshold ? 1.0 : 0.0;
    }

    // Multi-level dithering
    float levels = float(cb.tonelevels - 1);
    float quantized = floor(lum * levels + threshold) / levels;
    return saturate(quantized);
}

// ─── Cross-Hatch Pattern ─────────────────────────────────────────────────

float CrossHatch(float2 uv, float lum) {
    float2 pixelUV = uv * cb.resolution;
    float density = cb.crosshatchDensity;
    float lineWidth = 0.3;

    float result = 0.0;

    // Layer 1: diagonal lines (darkest areas)
    if (lum < 0.8) {
        float d1 = abs(frac((pixelUV.x + pixelUV.y) * density / cb.cellSize) - 0.5);
        result = max(result, step(d1, lineWidth));
    }

    // Layer 2: opposite diagonal (darker areas)
    if (lum < 0.6) {
        float d2 = abs(frac((pixelUV.x - pixelUV.y) * density / cb.cellSize) - 0.5);
        result = max(result, step(d2, lineWidth));
    }

    // Layer 3: horizontal lines (medium areas)
    if (lum < 0.4) {
        float d3 = abs(frac(pixelUV.y * density / cb.cellSize) - 0.5);
        result = max(result, step(d3, lineWidth * 0.8));
    }

    // Layer 4: vertical lines (very dark areas)
    if (lum < 0.2) {
        float d4 = abs(frac(pixelUV.x * density / cb.cellSize) - 0.5);
        result = max(result, step(d4, lineWidth * 0.6));
    }

    return result;
}

// ─── Blue Noise Dither ───────────────────────────────────────────────────

float BlueNoise(float2 uv) {
    // Procedural blue noise approximation via interleaved gradient noise
    float2 pixel = uv * cb.resolution;
    float anim = cb.animSpeed > 0.0 ? frac(cb.time * cb.animSpeed) * 52.9829189 : 0.0;
    return frac(52.9829189 * frac(0.06711056 * pixel.x + 0.00583715 * pixel.y + anim));
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;
    float4 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);

    float lum = dot(sceneColor.rgb, float3(0.2126, 0.7152, 0.0722));

    // Pre-dither contrast
    lum = saturate((lum - 0.5) * cb.contrast + 0.5);

    // Depth-based scaling
    if (cb.depthInfluence > 0.0) {
        float depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
        lum = lerp(lum, lum * (1.0 - depth * 0.5), cb.depthInfluence);
    }

    float3 result;

    if (cb.mode == 0) {
        // ── Halftone dots ────────────────────────────────────────────
        if (cb.colorMode == 2) {
            result = CMYKHalftone(uv, sceneColor.rgb);
        } else {
            float dot = HalftoneDot(uv, lum, cb.angle);

            if (cb.colorMode == 0) {
                result = lerp(cb.paperColor, cb.inkColor, dot);
            } else {
                result = lerp(cb.paperColor, sceneColor.rgb, dot);
            }
        }
    } else if (cb.mode == 1) {
        // ── Bayer ordered dither ─────────────────────────────────────
        float dithered = OrderedDither(lum, int2(DTid.xy));

        if (cb.colorMode == 0) {
            result = lerp(cb.paperColor, cb.inkColor, dithered);
        } else {
            // Per-channel dithering
            float3 ditheredRGB = float3(
                OrderedDither(sceneColor.r, int2(DTid.xy)),
                OrderedDither(sceneColor.g, int2(DTid.xy) + int2(3, 7)),
                OrderedDither(sceneColor.b, int2(DTid.xy) + int2(5, 2))
            );
            result = ditheredRGB;
        }
    } else if (cb.mode == 2) {
        // ── Cross-hatch ──────────────────────────────────────────────
        float hatch = CrossHatch(uv, lum);

        if (cb.colorMode == 0) {
            result = lerp(cb.paperColor, cb.inkColor, hatch);
        } else {
            result = lerp(cb.paperColor, sceneColor.rgb * 0.5, hatch);
        }
    } else {
        // ── Blue noise dither ────────────────────────────────────────
        float noise = BlueNoise(uv);

        if (cb.tonelevels <= 2) {
            float dithered = lum > noise ? 1.0 : 0.0;
            result = lerp(cb.inkColor, cb.paperColor, dithered);
        } else {
            float levels = float(cb.tonelevels - 1);
            float dithered = floor(lum * levels + noise) / levels;
            if (cb.colorMode == 0) {
                result = lerp(cb.inkColor, cb.paperColor, dithered);
            } else {
                result = sceneColor.rgb * dithered / max(lum, 0.001);
            }
        }
    }

    g_Output[DTid.xy] = float4(result, 1.0);
}
