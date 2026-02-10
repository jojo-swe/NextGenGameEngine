// ─── Screen-Space Ink/Paint Splatter Shader ──────────────────────────────
// Stylized post-process that applies procedural ink splatter, paint drip,
// and brush stroke effects for non-photorealistic rendering (NPR).
//
// Features:
//   - Procedural splatter mask from Voronoi + noise
//   - Paint drip trails with gravity simulation
//   - Brush stroke texture overlay aligned to surface normals
//   - Edge darkening (ink outline effect)
//   - Color palette quantization for cel-shading look
//   - Wet paint bleed (color bleeding at edges)
//   - Configurable ink density, drip speed, and splatter radius
//
// References:
//   - "Stylized Rendering in Okami" (Clover Studio, GDC 2007)
//   - "Splatoon Ink System" (Nintendo EPD, CEDEC 2015)
//   - "Real-Time Hatching" (Praun et al., SIGGRAPH 2001)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor   : register(t0);
Texture2D<float>  g_SceneDepth   : register(t1);
Texture2D<float4> g_GBufferNormal: register(t2);
Texture2D<float>  g_NoiseTex     : register(t3);
Texture2D<float4> g_BrushTex     : register(t4); // Tileable brush stroke texture

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct InkConstants {
    float2 resolution;
    float2 invResolution;
    float  time;
    float  inkDensity;           // Splatter coverage 0..1 (default 0.4)
    float  splatScale;           // Splatter pattern UV scale (default 3.0)
    float  splatThreshold;       // Voronoi threshold for splat shape (default 0.35)
    float  dripSpeed;            // Gravity drip speed (default 0.5)
    float  dripLength;           // Max drip trail length in UV (default 0.1)
    float  dripThreshold;        // Noise threshold for drip initiation (default 0.8)
    float  brushScale;           // Brush texture UV scale (default 8.0)
    float  brushStrength;        // Brush overlay intensity (default 0.3)
    float  edgeWidth;            // Ink outline width in pixels (default 1.5)
    float  edgeThreshold;        // Depth/normal edge detection threshold (default 0.1)
    float  edgeDarkness;         // Outline darkness (default 0.8)
    float  bleedRadius;          // Color bleed radius in pixels (default 2.0)
    float  bleedStrength;        // Color bleed intensity (default 0.15)
    u32    paletteSize;          // Color quantization levels (default 8)
    float  paletteStrength;      // Quantization blend (default 0.5)
    float3 inkColor;             // Base ink tint (default: 0.05, 0.02, 0.01)
    float  paperWhite;           // Paper brightness (default 0.95)
    float3 paperTint;            // Paper color tint (default: 0.98, 0.96, 0.9)
    float  pad0;
};

[[vk::push_constant]] ConstantBuffer<InkConstants> cb;

// ─── Voronoi Noise for Splatter Shape ────────────────────────────────────

float2 VoronoiHash(float2 p) {
    p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
    return frac(sin(p) * 43758.5453);
}

float Voronoi(float2 uv) {
    float2 ip = floor(uv);
    float2 fp = frac(uv);

    float minDist = 1.0;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float2 neighbor = float2(x, y);
            float2 point = VoronoiHash(ip + neighbor);
            float2 diff = neighbor + point - fp;
            float dist = length(diff);
            minDist = min(minDist, dist);
        }
    }

    return minDist;
}

// ─── Edge Detection ──────────────────────────────────────────────────────
// Sobel-based edge detection on depth and normals for ink outlines.

float DetectEdge(float2 uv) {
    float2 texel = cb.invResolution * cb.edgeWidth;

    // Sample depth neighborhood
    float dC = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
    float dL = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2(-texel.x, 0), 0);
    float dR = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2( texel.x, 0), 0);
    float dU = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2(0, -texel.y), 0);
    float dD = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2(0,  texel.y), 0);

    float depthEdge = abs(dL - dR) + abs(dU - dD);

    // Sample normal neighborhood
    float3 nC = g_GBufferNormal.SampleLevel(g_LinearClamp, uv, 0).rgb * 2.0 - 1.0;
    float3 nL = g_GBufferNormal.SampleLevel(g_LinearClamp, uv + float2(-texel.x, 0), 0).rgb * 2.0 - 1.0;
    float3 nR = g_GBufferNormal.SampleLevel(g_LinearClamp, uv + float2( texel.x, 0), 0).rgb * 2.0 - 1.0;
    float3 nU = g_GBufferNormal.SampleLevel(g_LinearClamp, uv + float2(0, -texel.y), 0).rgb * 2.0 - 1.0;
    float3 nD = g_GBufferNormal.SampleLevel(g_LinearClamp, uv + float2(0,  texel.y), 0).rgb * 2.0 - 1.0;

    float normalEdge = length(nL - nR) + length(nU - nD);

    float edge = max(depthEdge * 50.0, normalEdge);
    return smoothstep(cb.edgeThreshold, cb.edgeThreshold + 0.2, edge);
}

// ─── Paint Drip ──────────────────────────────────────────────────────────

float DripMask(float2 uv) {
    // Drip columns: vertical streaks from splatter points
    float columnNoise = g_NoiseTex.SampleLevel(g_LinearWrap, float2(uv.x * 20.0, 0.5), 0);

    if (columnNoise < cb.dripThreshold) return 0.0;

    // Drip trail: extends downward from splatter
    float dripPhase = frac(cb.time * cb.dripSpeed + columnNoise * 5.0);
    float dripY = uv.y + dripPhase * cb.dripLength;

    float dripNoise = g_NoiseTex.SampleLevel(g_LinearWrap, float2(uv.x * 15.0, dripY * 30.0), 0);

    // Thin drip trail
    float dripWidth = smoothstep(0.45, 0.5, dripNoise) * smoothstep(0.55, 0.5, dripNoise);

    // Fade with distance from origin
    float fadeFactor = 1.0 - saturate(dripPhase);

    return dripWidth * fadeFactor * (columnNoise - cb.dripThreshold) / (1.0 - cb.dripThreshold);
}

// ─── Brush Stroke Overlay ────────────────────────────────────────────────

float BrushStroke(float2 uv, float3 worldNormal) {
    // Align brush strokes to surface tangent direction
    float2 tangentDir = normalize(worldNormal.xz + 0.001);
    float2 brushUV;
    brushUV.x = dot(uv * cb.brushScale, tangentDir);
    brushUV.y = dot(uv * cb.brushScale, float2(-tangentDir.y, tangentDir.x));

    float brush = g_BrushTex.SampleLevel(g_LinearWrap, brushUV, 0).r;
    return brush;
}

// ─── Color Palette Quantization ──────────────────────────────────────────

float3 QuantizeColor(float3 color, uint levels) {
    float3 quantized = floor(color * float(levels) + 0.5) / float(levels);
    return quantized;
}

// ─── Wet Paint Bleed ─────────────────────────────────────────────────────

float3 ColorBleed(float2 uv) {
    float3 bleed = float3(0, 0, 0);
    float totalWeight = 0.0;

    float2 texel = cb.invResolution;
    int radius = int(cb.bleedRadius);

    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            float2 offset = float2(x, y) * texel;
            float dist = length(float2(x, y));
            if (dist > cb.bleedRadius) continue;

            float weight = 1.0 - (dist / cb.bleedRadius);
            // Noise-modulated weight for organic bleeding
            float noise = g_NoiseTex.SampleLevel(g_LinearWrap, (uv + offset) * 50.0, 0);
            weight *= (0.5 + 0.5 * noise);

            float3 sample = g_SceneColor.SampleLevel(g_LinearClamp, uv + offset, 0).rgb;
            bleed += sample * weight;
            totalWeight += weight;
        }
    }

    return totalWeight > 0.0 ? bleed / totalWeight : g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float3 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;
    float depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);

    // Paper background for sky
    if (depth < 0.0001) {
        float3 paper = cb.paperTint * cb.paperWhite;
        // Paper texture
        float paperNoise = g_NoiseTex.SampleLevel(g_LinearWrap, uv * 30.0, 0);
        paper *= (0.95 + 0.05 * paperNoise);
        g_Output[DTid.xy] = float4(paper, 1.0);
        return;
    }

    float3 worldNormal = g_GBufferNormal.SampleLevel(g_LinearClamp, uv, 0).rgb * 2.0 - 1.0;

    // ── Splatter mask ────────────────────────────────────────────────
    float voronoi = Voronoi(uv * cb.splatScale);
    float noise = g_NoiseTex.SampleLevel(g_LinearWrap, uv * cb.splatScale * 2.0, 0);
    float splatMask = smoothstep(cb.splatThreshold, cb.splatThreshold + 0.1, voronoi + noise * 0.3);
    splatMask = 1.0 - splatMask; // Invert: splatter where Voronoi cells are close
    splatMask *= cb.inkDensity;

    // ── Drip trails ──────────────────────────────────────────────────
    float drip = DripMask(uv);

    // ── Edge detection (ink outline) ─────────────────────────────────
    float edge = DetectEdge(uv);

    // ── Brush stroke overlay ─────────────────────────────────────────
    float brush = BrushStroke(uv, worldNormal);

    // ── Color processing ─────────────────────────────────────────────
    float3 color = sceneColor;

    // Wet paint bleed
    if (cb.bleedStrength > 0.0) {
        float3 bled = ColorBleed(uv);
        color = lerp(color, bled, cb.bleedStrength * splatMask);
    }

    // Color palette quantization
    if (cb.paletteSize > 0 && cb.paletteStrength > 0.0) {
        float3 quantized = QuantizeColor(color, cb.paletteSize);
        color = lerp(color, quantized, cb.paletteStrength);
    }

    // ── Compositing ──────────────────────────────────────────────────

    // Paper base
    float paperNoise = g_NoiseTex.SampleLevel(g_LinearWrap, uv * 30.0, 0);
    float3 paper = cb.paperTint * cb.paperWhite * (0.95 + 0.05 * paperNoise);

    // Apply brush stroke texture modulation
    color *= (1.0 - cb.brushStrength + brush * cb.brushStrength);

    // Ink splatter overlay
    float inkMask = saturate(splatMask + drip);
    color = lerp(color, cb.inkColor, inkMask * 0.6);

    // Edge darkening (ink outline)
    color = lerp(color, cb.inkColor, edge * cb.edgeDarkness);

    // Paper bleed: blend with paper at low ink areas
    float paperBlend = 1.0 - saturate(splatMask + edge * 0.5);
    color = lerp(color, lerp(color, paper, 0.1), paperBlend * 0.3);

    g_Output[DTid.xy] = float4(color, 1.0);
}
