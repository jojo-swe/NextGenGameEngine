// ─── Procedural Pixelation / Mosaic Effect Shader ────────────────────────
// Screen-space post-process for pixelation, mosaic, and retro downscale
// effects with multiple pattern modes and color quantization.
//
// Features:
//   - Square pixelation (uniform block size)
//   - Hexagonal mosaic pattern
//   - Diamond/rhombus mosaic
//   - Voronoi cell mosaic (organic shapes)
//   - Color palette quantization (reduce color depth)
//   - Depth-aware pixelation (larger blocks at distance)
//   - Animated transition (pixelation amount over time)
//   - Edge-aware mode (preserve edges at block boundaries)
//   - Configurable block size, color levels, and pattern
//
// References:
//   - "Retro Rendering in Obra Dinn" (Pope, GDC 2019)
//   - "Pixel Art Rendering" (Colton, 2015)
//   - "Hexagonal Grids" (Red Blob Games)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor : register(t0);
Texture2D<float>  g_SceneDepth : register(t1);

SamplerState g_LinearClamp : register(s0);
SamplerState g_PointClamp  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct PixelationConstants {
    float2   resolution;
    float2   invResolution;
    float    time;
    u32      patternMode;          // 0=square, 1=hex, 2=diamond, 3=voronoi
    float    blockSize;            // Pixel block size (default 8.0)
    u32      colorLevels;          // Color quantization levels per channel (0=off, default 0)
    float    depthScale;           // Depth-based block size scaling (default 0.0)
    float    transitionAmount;     // Animated transition 0-1 (default 1.0)
    float    edgeThreshold;        // Edge preservation threshold (default 0.0)
    float    ditherAmount;         // Dithering for color quantization (default 0.0)
    float    aspectCorrection;     // Aspect ratio correction (default 1.0)
    float    pad0;
    float    pad1;
    float    pad2;
};

[[vk::push_constant]] ConstantBuffer<PixelationConstants> cb;

// ─── Hash ────────────────────────────────────────────────────────────────

float2 Hash22(float2 p) {
    float3 a = frac(p.xyx * float3(123.34, 234.34, 345.65));
    a += dot(a, a + 34.45);
    return frac(float2(a.x * a.y, a.y * a.z));
}

// ─── Square Pixelation ───────────────────────────────────────────────────

float2 SquarePixelate(float2 uv, float blockSize) {
    float2 pixelSize = blockSize * cb.invResolution;
    pixelSize.x *= cb.aspectCorrection;

    float2 snapped = floor(uv / pixelSize) * pixelSize + pixelSize * 0.5;
    return snapped;
}

// ─── Hexagonal Mosaic ────────────────────────────────────────────────────

float2 HexPixelate(float2 uv, float blockSize) {
    float2 pixelSize = blockSize * cb.invResolution;

    float2 h = float2(pixelSize.x, pixelSize.y * 0.866); // sqrt(3)/2
    float2 a = fmod(uv, h) - h * 0.5;
    float2 b = fmod(uv - h * 0.5, h) - h * 0.5;

    float2 center;
    if (dot(a, a) < dot(b, b)) {
        center = uv - a;
    } else {
        center = uv - b;
    }

    return center;
}

// ─── Diamond Mosaic ──────────────────────────────────────────────────────

float2 DiamondPixelate(float2 uv, float blockSize) {
    float2 pixelSize = blockSize * cb.invResolution;

    // Rotate 45 degrees, snap, rotate back
    float2 rotated = float2(uv.x + uv.y, uv.x - uv.y) * 0.707;
    float2 snapped = floor(rotated / pixelSize) * pixelSize + pixelSize * 0.5;
    float2 unrotated = float2(snapped.x + snapped.y, snapped.x - snapped.y) * 0.707;

    return unrotated;
}

// ─── Voronoi Mosaic ──────────────────────────────────────────────────────

float2 VoronoiPixelate(float2 uv, float blockSize) {
    float2 pixelSize = blockSize * cb.invResolution;
    float2 p = uv / pixelSize;
    float2 cell = floor(p);

    float minDist = 100.0;
    float2 closestCenter = uv;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float2 neighbor = cell + float2(float(x), float(y));
            float2 point = neighbor + Hash22(neighbor) * 0.8 + 0.1;
            float dist = length(p - point);

            if (dist < minDist) {
                minDist = dist;
                closestCenter = point * pixelSize;
            }
        }
    }

    return closestCenter;
}

// ─── Color Quantization ──────────────────────────────────────────────────

float3 QuantizeColor(float3 color, u32 levels) {
    if (levels == 0) return color;

    float f = float(levels - 1);
    return floor(color * f + 0.5) / f;
}

// ─── Bayer Dither ────────────────────────────────────────────────────────

float BayerDither4x4(float2 pixelCoord) {
    int2 p = int2(pixelCoord) % 4;

    float4x4 bayer = float4x4(
         0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
        12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
         3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
        15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
    );

    return bayer[p.y][p.x];
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    // ── Compute effective block size ─────────────────────────────────
    float blockSize = cb.blockSize;

    // Depth-based scaling
    if (cb.depthScale > 0.0) {
        float depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
        blockSize *= 1.0 + depth * cb.depthScale;
    }

    // Transition animation
    blockSize *= cb.transitionAmount;
    blockSize = max(blockSize, 1.0);

    // ── Compute mosaic center UV ─────────────────────────────────────
    float2 mosaicUV;

    if (cb.patternMode == 0) {
        mosaicUV = SquarePixelate(uv, blockSize);
    } else if (cb.patternMode == 1) {
        mosaicUV = HexPixelate(uv, blockSize);
    } else if (cb.patternMode == 2) {
        mosaicUV = DiamondPixelate(uv, blockSize);
    } else {
        mosaicUV = VoronoiPixelate(uv, blockSize);
    }

    mosaicUV = clamp(mosaicUV, 0.0, 1.0);

    // ── Sample at mosaic center ──────────────────────────────────────
    float3 color = g_SceneColor.SampleLevel(g_LinearClamp, mosaicUV, 0).rgb;

    // ── Edge preservation ────────────────────────────────────────────
    if (cb.edgeThreshold > 0.0) {
        float3 original = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;
        float diff = length(color - original);

        if (diff > cb.edgeThreshold) {
            color = original; // Preserve edge detail
        }
    }

    // ── Color quantization with optional dithering ───────────────────
    if (cb.colorLevels > 0) {
        if (cb.ditherAmount > 0.0) {
            float dither = (BayerDither4x4(float2(DTid.xy)) - 0.5) * cb.ditherAmount;
            color += dither;
        }

        color = QuantizeColor(color, cb.colorLevels);
    }

    color = saturate(color);

    g_Output[DTid.xy] = float4(color, 1.0);
}
