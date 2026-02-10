// ─── Procedural Toon / Cel-Shading Outline Effect Shader ─────────────────
// Screen-space post-process for stylized toon rendering with edge
// detection outlines, color quantization, and hatching overlays.
//
// Features:
//   - Sobel edge detection on depth and normals
//   - Configurable outline color, width, and threshold
//   - Color quantization (cel-shading bands)
//   - Depth-aware outline thickness (thinner in distance)
//   - Object ID edge detection (silhouette outlines)
//   - Inner detail lines from normal discontinuities
//   - Optional cross-hatching overlay for shadow regions
//   - Configurable number of shading bands
//
// References:
//   - "Cel Shading in Borderlands" (Gearbox, GDC 2010)
//   - "Toon Shading in Guilty Gear Xrd" (Arc System Works, GDC 2015)
//   - "Real-Time Hatching" (Praun et al., SIGGRAPH 2001)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor    : register(t0);
Texture2D<float>  g_SceneDepth    : register(t1);
Texture2D<float4> g_GBufferNormal : register(t2);
Texture2D<uint>   g_ObjectId      : register(t3); // Per-pixel object ID
Texture2D<float>  g_HatchTex      : register(t4); // Cross-hatch pattern

SamplerState g_PointClamp  : register(s0);
SamplerState g_LinearClamp : register(s1);
SamplerState g_LinearWrap  : register(s2);

RWTexture2D<float4> g_Output : register(u0);

struct ToonConstants {
    float2   resolution;
    float2   invResolution;
    float3   cameraPos;
    float    time;
    float3   outlineColor;       // Edge color (default: 0, 0, 0)
    float    outlineWidth;       // In pixels (default 1.5)
    float    depthThreshold;     // Depth edge sensitivity (default 0.02)
    float    normalThreshold;    // Normal edge sensitivity (default 0.5)
    float    objectIdThreshold;  // Object ID edge detection (default 0.5)
    u32      shadingBands;       // Color quantization levels (default 4)
    float    bandSoftness;       // Transition softness between bands (default 0.05)
    float    outlineDepthFade;   // Fade outline with distance (default 0.5)
    float    hatchingIntensity;  // Cross-hatch overlay strength (default 0.3)
    float    hatchingScale;      // UV scale for hatch pattern (default 8.0)
    float    hatchingShadowThreshold; // Luminance below which hatching appears (default 0.4)
    float3   specularBandColor;  // Highlight band tint (default: 1, 1, 1)
    float    specularBandThreshold; // Luminance for specular band (default 0.8)
    float    saturationBoost;    // Color saturation multiplier (default 1.2)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<ToonConstants> cb;

// ─── Edge Detection ──────────────────────────────────────────────────────

float SobelDepth(float2 uv) {
    float2 texel = cb.invResolution * cb.outlineWidth;

    float tl = g_SceneDepth.SampleLevel(g_PointClamp, uv + float2(-texel.x, -texel.y), 0);
    float tc = g_SceneDepth.SampleLevel(g_PointClamp, uv + float2(0, -texel.y), 0);
    float tr = g_SceneDepth.SampleLevel(g_PointClamp, uv + float2(texel.x, -texel.y), 0);
    float ml = g_SceneDepth.SampleLevel(g_PointClamp, uv + float2(-texel.x, 0), 0);
    float mr = g_SceneDepth.SampleLevel(g_PointClamp, uv + float2(texel.x, 0), 0);
    float bl = g_SceneDepth.SampleLevel(g_PointClamp, uv + float2(-texel.x, texel.y), 0);
    float bc = g_SceneDepth.SampleLevel(g_PointClamp, uv + float2(0, texel.y), 0);
    float br = g_SceneDepth.SampleLevel(g_PointClamp, uv + float2(texel.x, texel.y), 0);

    float gx = -tl - 2.0 * ml - bl + tr + 2.0 * mr + br;
    float gy = -tl - 2.0 * tc - tr + bl + 2.0 * bc + br;

    return sqrt(gx * gx + gy * gy);
}

float SobelNormal(float2 uv) {
    float2 texel = cb.invResolution * cb.outlineWidth;

    float3 tl = g_GBufferNormal.SampleLevel(g_PointClamp, uv + float2(-texel.x, -texel.y), 0).xyz;
    float3 tc = g_GBufferNormal.SampleLevel(g_PointClamp, uv + float2(0, -texel.y), 0).xyz;
    float3 tr = g_GBufferNormal.SampleLevel(g_PointClamp, uv + float2(texel.x, -texel.y), 0).xyz;
    float3 ml = g_GBufferNormal.SampleLevel(g_PointClamp, uv + float2(-texel.x, 0), 0).xyz;
    float3 mr = g_GBufferNormal.SampleLevel(g_PointClamp, uv + float2(texel.x, 0), 0).xyz;
    float3 bl = g_GBufferNormal.SampleLevel(g_PointClamp, uv + float2(-texel.x, texel.y), 0).xyz;
    float3 bc = g_GBufferNormal.SampleLevel(g_PointClamp, uv + float2(0, texel.y), 0).xyz;
    float3 br = g_GBufferNormal.SampleLevel(g_PointClamp, uv + float2(texel.x, texel.y), 0).xyz;

    float3 gx = -tl - 2.0 * ml - bl + tr + 2.0 * mr + br;
    float3 gy = -tl - 2.0 * tc - tr + bl + 2.0 * bc + br;

    return length(gx) + length(gy);
}

float ObjectIdEdge(float2 uv) {
    float2 texel = cb.invResolution * cb.outlineWidth;

    uint center = g_ObjectId.SampleLevel(g_PointClamp, uv, 0);
    uint left   = g_ObjectId.SampleLevel(g_PointClamp, uv + float2(-texel.x, 0), 0);
    uint right  = g_ObjectId.SampleLevel(g_PointClamp, uv + float2(texel.x, 0), 0);
    uint top    = g_ObjectId.SampleLevel(g_PointClamp, uv + float2(0, -texel.y), 0);
    uint bottom = g_ObjectId.SampleLevel(g_PointClamp, uv + float2(0, texel.y), 0);

    float edge = 0.0;
    if (center != left)   edge = 1.0;
    if (center != right)  edge = 1.0;
    if (center != top)    edge = 1.0;
    if (center != bottom) edge = 1.0;

    return edge;
}

// ─── Color Quantization ─────────────────────────────────────────────────

float3 QuantizeColor(float3 color) {
    float bands = float(cb.shadingBands);

    // Compute luminance
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));

    // Quantize luminance
    float quantLum = floor(lum * bands + 0.5) / bands;

    // Soft transition between bands
    float t = frac(lum * bands);
    float softT = smoothstep(0.5 - cb.bandSoftness * bands, 0.5 + cb.bandSoftness * bands, t);
    float finalLum = lerp(floor(lum * bands) / bands, ceil(lum * bands) / bands, softT);

    // Preserve hue: scale color by luminance ratio
    float3 quantized = color * (finalLum / max(lum, 0.001));

    return quantized;
}

// ─── Saturation ──────────────────────────────────────────────────────────

float3 AdjustSaturation(float3 color, float saturation) {
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
    return lerp(float3(lum, lum, lum), color, saturation);
}

// ─── Cross-Hatching ──────────────────────────────────────────────────────

float CrossHatch(float2 uv, float luminance) {
    if (luminance > cb.hatchingShadowThreshold) return 0.0;

    // Hatching density increases in darker areas
    float darkness = 1.0 - luminance / cb.hatchingShadowThreshold;

    float2 hatchUV = uv * cb.hatchingScale * cb.resolution / 100.0;

    // Layer 1: diagonal lines (45 degrees)
    float hatch1 = g_HatchTex.SampleLevel(g_LinearWrap, hatchUV, 0);

    // Layer 2: perpendicular lines (-45 degrees) for darker areas
    float2 rotUV = float2(hatchUV.x - hatchUV.y, hatchUV.x + hatchUV.y) * 0.707;
    float hatch2 = g_HatchTex.SampleLevel(g_LinearWrap, rotUV, 0);

    float hatch = hatch1;
    if (darkness > 0.5) {
        hatch = max(hatch, hatch2); // Add cross-hatch for very dark areas
    }

    return hatch * darkness * cb.hatchingIntensity;
}

// ─── Specular Band ───────────────────────────────────────────────────────

float3 SpecularBand(float3 color, float luminance) {
    float band = smoothstep(cb.specularBandThreshold, cb.specularBandThreshold + 0.1, luminance);
    return lerp(color, cb.specularBandColor, band * 0.5);
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float4 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);
    float depth = g_SceneDepth.SampleLevel(g_PointClamp, uv, 0);

    // ── Edge detection ───────────────────────────────────────────────
    float depthEdge = SobelDepth(uv);
    float normalEdge = SobelNormal(uv);
    float objectEdge = ObjectIdEdge(uv);

    // Apply thresholds
    float depthLine = smoothstep(cb.depthThreshold * 0.5, cb.depthThreshold, depthEdge);
    float normalLine = smoothstep(cb.normalThreshold * 0.5, cb.normalThreshold, normalEdge);
    float objectLine = objectEdge * cb.objectIdThreshold;

    // Combine edges
    float outline = max(max(depthLine, normalLine), objectLine);

    // Depth-based outline fade (thinner in distance)
    float depthFade = 1.0 - saturate(depth * cb.outlineDepthFade);
    outline *= depthFade;
    outline = saturate(outline);

    // ── Color quantization (cel-shading) ─────────────────────────────
    float3 toonColor = sceneColor.rgb;

    // Boost saturation for toon look
    toonColor = AdjustSaturation(toonColor, cb.saturationBoost);

    // Quantize to discrete shading bands
    toonColor = QuantizeColor(toonColor);

    // ── Specular highlight band ──────────────────────────────────────
    float lum = dot(toonColor, float3(0.2126, 0.7152, 0.0722));
    toonColor = SpecularBand(toonColor, lum);

    // ── Cross-hatching overlay ───────────────────────────────────────
    float hatch = CrossHatch(uv, lum);
    toonColor = lerp(toonColor, toonColor * 0.3, hatch);

    // ── Apply outline ────────────────────────────────────────────────
    float3 finalColor = lerp(toonColor, cb.outlineColor, outline);

    g_Output[DTid.xy] = float4(finalColor, 1.0);
}
