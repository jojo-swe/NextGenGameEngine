// ─── Procedural Ink / Sumi-e Painting Shader ─────────────────────────────
// Screen-space post-process that simulates traditional East Asian ink wash
// painting (sumi-e / suiboku-ga) with brush stroke textures, ink diffusion,
// and rice paper substrate.
//
// Features:
//   - Luminance-based ink density mapping
//   - Sobel edge detection for brush stroke outlines
//   - Procedural rice paper texture
//   - Ink diffusion / bleeding at edges
//   - Brush stroke direction from image gradient
//   - Multi-pass stroke layering (dark to light)
//   - Dry brush texture (broken strokes)
//   - Ink splatter artifacts
//   - Depth-aware stroke width
//   - Configurable ink color, paper color, stroke density
//
// References:
//   - "Real-Time Hatching" (Praun et al., SIGGRAPH 2001)
//   - "Sumi-e Rendering" (Chu & Tai, Pacific Graphics 2005)
//   - "Painterly Rendering" (Hertzmann, SIGGRAPH 1998)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor : register(t0);
Texture2D<float>  g_SceneDepth : register(t1);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct InkPaintingConstants {
    float2   resolution;
    float2   invResolution;
    float    time;

    float3   inkColor;            // Ink color (default: 0.05, 0.03, 0.02)
    float3   paperColor;          // Paper background (default: 0.95, 0.92, 0.85)

    float    strokeWidth;         // Outline stroke width (default: 1.5)
    float    inkDensity;          // Ink opacity/density (default: 0.8)
    float    edgeThreshold;       // Edge detection sensitivity (default: 0.1)
    float    diffusionStrength;   // Ink bleeding amount (default: 0.2)
    float    dryBrushAmount;      // Broken stroke intensity (default: 0.3)
    float    paperGrain;          // Rice paper texture strength (default: 0.4)
    float    splatterAmount;      // Ink splatter probability (default: 0.05)
    float    depthStrokeScale;    // Depth-based stroke width (default: 0.0)
    float    toneLayers;          // Number of ink wash layers (default: 4.0)
    float    strokeJitter;        // Brush stroke randomness (default: 0.2)

    float    pad0;
    float    pad1;
};

[[vk::push_constant]] ConstantBuffer<InkPaintingConstants> cb;

// ─── Noise Functions ─────────────────────────────────────────────────────

float Hash21(float2 p) {
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float ValueNoise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = Hash21(i);
    float b = Hash21(i + float2(1, 0));
    float c = Hash21(i + float2(0, 1));
    float d = Hash21(i + float2(1, 1));

    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float FBM(float2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < octaves; ++i) {
        value += amplitude * ValueNoise(p);
        p *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

// ─── Rice Paper Texture ──────────────────────────────────────────────────

float RicePaper(float2 uv) {
    float2 paperUV = uv * cb.resolution * 0.3;

    // Fiber-like noise (elongated in one direction)
    float fiber1 = ValueNoise(paperUV * float2(1.0, 3.0));
    float fiber2 = ValueNoise(paperUV * float2(3.0, 1.0) + 47.0);

    float paper = (fiber1 + fiber2) * 0.5;

    // Add fine grain
    float grain = ValueNoise(paperUV * 5.0);
    paper = lerp(paper, grain, 0.3);

    return paper;
}

// ─── Sobel Edge Detection ────────────────────────────────────────────────

float2 SobelGradient(float2 uv) {
    float2 texel = cb.invResolution;

    float tl = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(-texel.x, -texel.y), 0).rgb, float3(0.299, 0.587, 0.114));
    float t  = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(0, -texel.y), 0).rgb, float3(0.299, 0.587, 0.114));
    float tr = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(texel.x, -texel.y), 0).rgb, float3(0.299, 0.587, 0.114));
    float l  = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(-texel.x, 0), 0).rgb, float3(0.299, 0.587, 0.114));
    float r  = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(texel.x, 0), 0).rgb, float3(0.299, 0.587, 0.114));
    float bl = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(-texel.x, texel.y), 0).rgb, float3(0.299, 0.587, 0.114));
    float b  = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(0, texel.y), 0).rgb, float3(0.299, 0.587, 0.114));
    float br = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(texel.x, texel.y), 0).rgb, float3(0.299, 0.587, 0.114));

    float gx = -tl - 2.0 * l - bl + tr + 2.0 * r + br;
    float gy = -tl - 2.0 * t - tr + bl + 2.0 * b + br;

    return float2(gx, gy);
}

// ─── Ink Stroke (directional) ────────────────────────────────────────────

float InkStroke(float2 uv, float2 gradient, float width) {
    float edgeMag = length(gradient);

    // Stroke perpendicular to gradient
    float2 strokeDir = normalize(float2(-gradient.y, gradient.x) + 0.001);

    // Add jitter for natural feel
    float jitter = (ValueNoise(uv * cb.resolution * 0.5) - 0.5) * cb.strokeJitter;
    float2 jitteredUV = uv + strokeDir * jitter * cb.invResolution;

    // Recompute edge at jittered position
    float2 jitteredGrad = SobelGradient(jitteredUV);
    float jitteredEdge = length(jitteredGrad);

    // Stroke intensity based on edge strength
    float stroke = smoothstep(cb.edgeThreshold * 0.5, cb.edgeThreshold, jitteredEdge);

    // Width variation
    stroke *= smoothstep(0.0, width, width * stroke);

    return stroke;
}

// ─── Dry Brush Effect ────────────────────────────────────────────────────

float DryBrush(float2 uv, float inkAmount) {
    if (cb.dryBrushAmount <= 0.0) return inkAmount;

    // Noise that breaks up strokes
    float brushNoise = ValueNoise(uv * cb.resolution * 0.8);
    float dryMask = smoothstep(0.3, 0.7, brushNoise);

    // More dry brush at lighter ink levels
    float dryFactor = lerp(0.0, cb.dryBrushAmount, 1.0 - inkAmount);

    return inkAmount * lerp(1.0, dryMask, dryFactor);
}

// ─── Ink Diffusion ───────────────────────────────────────────────────────

float InkDiffusion(float2 uv, float inkAmount) {
    if (cb.diffusionStrength <= 0.0) return inkAmount;

    float2 texel = cb.invResolution;

    // Sample neighbors for diffusion
    float sum = 0.0;
    float count = 0.0;

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            float2 sampleUV = uv + float2(dx, dy) * texel * 2.0;
            float3 c = g_SceneColor.SampleLevel(g_LinearClamp, sampleUV, 0).rgb;
            float lum = 1.0 - dot(c, float3(0.299, 0.587, 0.114));
            sum += lum;
            count += 1.0;
        }
    }

    float avgInk = sum / count;
    float diffused = lerp(inkAmount, avgInk, cb.diffusionStrength * 0.5);

    // Add noise to diffusion edge
    float diffNoise = FBM(uv * cb.resolution * 0.1, 3);
    diffused += (diffNoise - 0.5) * cb.diffusionStrength * 0.2;

    return saturate(diffused);
}

// ─── Ink Splatter ────────────────────────────────────────────────────────

float InkSplatter(float2 uv) {
    if (cb.splatterAmount <= 0.0) return 0.0;

    float2 splatUV = uv * cb.resolution * 0.05;
    float trigger = Hash21(floor(splatUV));

    if (trigger > cb.splatterAmount) return 0.0;

    // Circular splatter
    float2 center = (floor(splatUV) + 0.5) / (cb.resolution * 0.05);
    float dist = length(uv - center) * cb.resolution.x * 0.05;

    float radius = Hash21(floor(splatUV) + 100.0) * 0.8 + 0.2;
    float splat = smoothstep(radius, radius * 0.3, dist);

    // Irregular edge
    float angle = atan2(uv.y - center.y, uv.x - center.x);
    float edgeNoise = ValueNoise(float2(angle * 3.0, trigger * 50.0));
    splat *= lerp(0.5, 1.0, edgeNoise);

    return splat * 0.5;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;
    float3 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;

    // ── Luminance for ink density ────────────────────────────────────
    float lum = dot(sceneColor, float3(0.299, 0.587, 0.114));

    // ── Quantize to ink wash layers ──────────────────────────────────
    float inkAmount = 1.0 - lum;
    if (cb.toneLayers > 1.0) {
        inkAmount = floor(inkAmount * cb.toneLayers) / cb.toneLayers;
    }

    // ── Edge detection for outlines ──────────────────────────────────
    float2 gradient = SobelGradient(uv);

    float strokeWidth = cb.strokeWidth;
    if (cb.depthStrokeScale > 0.0) {
        float depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
        strokeWidth *= (1.0 + depth * cb.depthStrokeScale);
    }

    float edgeStroke = InkStroke(uv, gradient, strokeWidth);

    // ── Ink diffusion ────────────────────────────────────────────────
    inkAmount = InkDiffusion(uv, inkAmount);

    // ── Dry brush effect ─────────────────────────────────────────────
    inkAmount = DryBrush(uv, inkAmount);

    // ── Combine ink wash + edge strokes ──────────────────────────────
    float totalInk = saturate(inkAmount * cb.inkDensity + edgeStroke * 0.8);

    // ── Ink splatter ─────────────────────────────────────────────────
    totalInk = saturate(totalInk + InkSplatter(uv));

    // ── Rice paper texture ───────────────────────────────────────────
    float paper = RicePaper(uv);
    float3 paperTinted = cb.paperColor * lerp(0.9, 1.1, paper * cb.paperGrain);

    // ── Blend ink on paper ───────────────────────────────────────────
    float3 color = lerp(paperTinted, cb.inkColor, totalInk);

    // ── Paper texture shows through light ink ────────────────────────
    float paperShow = (1.0 - totalInk) * cb.paperGrain * 0.3;
    color += (paper - 0.5) * paperShow;

    color = saturate(color);

    g_Output[DTid.xy] = float4(color, 1.0);
}
