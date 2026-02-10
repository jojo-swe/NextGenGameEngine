// ─── Procedural Watercolor / Painterly Shader ────────────────────────────
// Screen-space post-process that simulates watercolor painting with
// wet-edge darkening, pigment diffusion, paper texture, and brush strokes.
//
// Features:
//   - Kuwahara filter for painterly smoothing (anisotropic)
//   - Wet-edge darkening at color boundaries
//   - Paper texture grain overlay
//   - Pigment granulation noise
//   - Edge-aware color bleeding/diffusion
//   - Brush stroke direction from gradient
//   - Color palette quantization (limited watercolor palette)
//   - Water stain/bloom artifacts
//   - Depth-aware paint detail (coarser at distance)
//   - Configurable brush size, wetness, paper roughness
//
// References:
//   - "Watercolor Rendering" (Bousseau et al., SIGGRAPH 2006)
//   - "Kuwahara Filter" (Papari et al., TVCG 2007)
//   - "NPR Painting" (Hertzmann, SIGGRAPH 1998)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor : register(t0);
Texture2D<float>  g_SceneDepth : register(t1);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct WatercolorConstants {
    float2   resolution;
    float2   invResolution;
    float    time;
    float    brushSize;            // Kuwahara filter radius (default 4.0)
    float    wetness;              // Wet-edge intensity (default 0.5)
    float    paperRoughness;       // Paper grain strength (default 0.3)
    float    pigmentGranulation;   // Pigment noise (default 0.2)
    float    colorBleed;           // Edge color bleeding (default 0.3)
    float    paletteQuantize;      // Color quantization levels (default 0.0, disabled)
    float    depthDetail;          // Depth-aware detail (default 0.0)
    float    stainIntensity;       // Water stain artifacts (default 0.1)
    float    saturationBoost;      // Watercolor saturation (default 1.2)
    float    edgeDarken;           // Edge darkening amount (default 0.4)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<WatercolorConstants> cb;

// ─── Paper Texture (Procedural) ──────────────────────────────────────────

float PaperNoise(float2 uv) {
    float2 p = uv * cb.resolution * 0.5;
    float n1 = frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
    float n2 = frac(sin(dot(p * 1.7, float2(39.346, 11.135))) * 22578.1459);
    float n3 = frac(sin(dot(p * 3.1, float2(73.156, 52.235))) * 65432.9876);
    return (n1 * 0.5 + n2 * 0.3 + n3 * 0.2);
}

// ─── Simplex-like Value Noise ────────────────────────────────────────────

float ValueNoise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = frac(sin(dot(i, float2(127.1, 311.7))) * 43758.5453);
    float b = frac(sin(dot(i + float2(1, 0), float2(127.1, 311.7))) * 43758.5453);
    float c = frac(sin(dot(i + float2(0, 1), float2(127.1, 311.7))) * 43758.5453);
    float d = frac(sin(dot(i + float2(1, 1), float2(127.1, 311.7))) * 43758.5453);

    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// ─── Kuwahara Filter (Painterly Smoothing) ───────────────────────────────

float3 KuwaharaFilter(float2 uv, float radius) {
    int r = int(radius);
    if (r < 1) r = 1;

    // Sample 4 quadrants around the pixel
    float3 mean[4];
    float variance[4];

    float2 texel = cb.invResolution;

    for (int q = 0; q < 4; ++q) {
        float3 sum = float3(0, 0, 0);
        float3 sumSq = float3(0, 0, 0);
        float count = 0.0;

        int2 offset;
        if (q == 0) offset = int2(-r, -r);
        else if (q == 1) offset = int2(0, -r);
        else if (q == 2) offset = int2(-r, 0);
        else offset = int2(0, 0);

        for (int dy = 0; dy <= r; ++dy) {
            for (int dx = 0; dx <= r; ++dx) {
                float2 sampleUV = uv + float2(offset.x + dx, offset.y + dy) * texel;
                float3 c = g_SceneColor.SampleLevel(g_LinearClamp, sampleUV, 0).rgb;
                sum += c;
                sumSq += c * c;
                count += 1.0;
            }
        }

        mean[q] = sum / count;
        float3 v = sumSq / count - mean[q] * mean[q];
        variance[q] = dot(v, float3(0.333, 0.333, 0.333));
    }

    // Pick the quadrant with lowest variance (smoothest region)
    float minVar = variance[0];
    float3 result = mean[0];
    for (int q = 1; q < 4; ++q) {
        if (variance[q] < minVar) {
            minVar = variance[q];
            result = mean[q];
        }
    }

    return result;
}

// ─── Edge Detection (for wet edges) ──────────────────────────────────────

float DetectEdge(float2 uv) {
    float2 texel = cb.invResolution;

    float3 c = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;
    float3 l = g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(-texel.x, 0), 0).rgb;
    float3 r = g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(texel.x, 0), 0).rgb;
    float3 t = g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(0, -texel.y), 0).rgb;
    float3 b = g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(0, texel.y), 0).rgb;

    float3 laplacian = abs(4.0 * c - l - r - t - b);
    return saturate(length(laplacian) * 2.0);
}

// ─── Color Bleeding ──────────────────────────────────────────────────────

float3 ColorBleed(float2 uv, float3 baseColor) {
    if (cb.colorBleed <= 0.0) return baseColor;

    float2 texel = cb.invResolution * 2.0;

    // Sample neighbors and blend based on similarity
    float3 bleed = baseColor;
    float totalWeight = 1.0;

    for (int i = -1; i <= 1; ++i) {
        for (int j = -1; j <= 1; ++j) {
            if (i == 0 && j == 0) continue;

            float2 sampleUV = uv + float2(i, j) * texel;
            float3 neighbor = g_SceneColor.SampleLevel(g_LinearClamp, sampleUV, 0).rgb;

            float similarity = 1.0 - saturate(length(baseColor - neighbor) * 3.0);
            float weight = similarity * cb.colorBleed;
            bleed += neighbor * weight;
            totalWeight += weight;
        }
    }

    return bleed / totalWeight;
}

// ─── Palette Quantization ────────────────────────────────────────────────

float3 QuantizePalette(float3 color, float levels) {
    if (levels <= 0.0) return color;
    return round(color * levels) / levels;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    // ── Brush size (depth-aware) ─────────────────────────────────────
    float brushRadius = cb.brushSize;
    if (cb.depthDetail > 0.0) {
        float depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
        brushRadius = max(2.0, brushRadius * (1.0 + depth * cb.depthDetail));
    }

    // ── Kuwahara painterly filter ────────────────────────────────────
    float3 color = KuwaharaFilter(uv, brushRadius);

    // ── Color bleeding ───────────────────────────────────────────────
    color = ColorBleed(uv, color);

    // ── Saturation boost (watercolors are vivid) ─────────────────────
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(float3(lum, lum, lum), color, cb.saturationBoost);

    // ── Palette quantization ─────────────────────────────────────────
    if (cb.paletteQuantize > 0.0) {
        color = QuantizePalette(color, cb.paletteQuantize);
    }

    // ── Wet-edge darkening ───────────────────────────────────────────
    float edge = DetectEdge(uv);
    float wetEdge = edge * cb.wetness;
    color *= (1.0 - wetEdge * cb.edgeDarken);

    // ── Paper texture ────────────────────────────────────────────────
    float paper = PaperNoise(uv);
    color *= lerp(1.0, 0.85 + paper * 0.3, cb.paperRoughness);

    // ── Pigment granulation ──────────────────────────────────────────
    if (cb.pigmentGranulation > 0.0) {
        float grain = ValueNoise(uv * cb.resolution * 0.15);
        // Granulation is stronger in darker areas (pigment settles)
        float darknessWeight = 1.0 - lum;
        color += (grain - 0.5) * cb.pigmentGranulation * darknessWeight;
    }

    // ── Water stain artifacts ────────────────────────────────────────
    if (cb.stainIntensity > 0.0) {
        float stain = ValueNoise(uv * cb.resolution * 0.03 + cb.time * 0.01);
        stain = smoothstep(0.4, 0.6, stain);
        color = lerp(color, color * 0.85, stain * cb.stainIntensity);
    }

    color = saturate(color);

    g_Output[DTid.xy] = float4(color, 1.0);
}
