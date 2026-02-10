// ─── Procedural Outline / Silhouette Edge Detection Shader ───────────────
// Screen-space post-process for rendering outlines and silhouette edges
// using depth, normal, and color discontinuity detection.
//
// Features:
//   - Depth-based edge detection (object silhouettes)
//   - Normal-based edge detection (surface creases)
//   - Color/luminance-based edge detection (texture edges)
//   - Combined multi-source edge detection
//   - Configurable edge thickness and color
//   - Depth-aware thickness (thinner at distance)
//   - Inner vs outer outline modes
//   - Edge color from scene or solid color
//   - Animated edge pulse effect
//   - Object ID edge detection (per-object outlines)
//
// References:
//   - "Borderlands Art Style" (Gearbox, GDC 2010)
//   - "Edge Detection in Guilty Gear Xrd" (Arc System Works, GDC 2015)
//   - "Sobel/Roberts/Prewitt Operators" (Image Processing, Gonzalez)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor  : register(t0);
Texture2D<float>  g_SceneDepth  : register(t1);
Texture2D<float4> g_SceneNormal : register(t2);
Texture2D<uint>   g_ObjectID    : register(t3);

SamplerState g_LinearClamp : register(s0);
SamplerState g_PointClamp  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct OutlineEdgeConstants {
    float2   resolution;
    float2   invResolution;
    float    time;
    float    depthThreshold;       // Depth edge sensitivity (default 0.01)
    float    normalThreshold;      // Normal edge sensitivity (default 0.5)
    float    colorThreshold;       // Color edge sensitivity (default 0.1)
    float    depthWeight;          // Depth edge contribution (default 1.0)
    float    normalWeight;         // Normal edge contribution (default 0.5)
    float    colorWeight;          // Color edge contribution (default 0.3)
    float    objectIDWeight;       // Object ID edge contribution (default 0.0)
    float    edgeThickness;        // Edge width in pixels (default 1.0)
    float3   edgeColor;            // Solid edge color (default: 0, 0, 0)
    u32      edgeColorMode;        // 0=solid, 1=scene darkened, 2=scene inverted
    float    depthFadeStart;       // Distance where edges start thinning (default 0.5)
    float    depthFadeEnd;         // Distance where edges disappear (default 1.0)
    float    pulseSpeed;           // Animated pulse speed (default 0.0, disabled)
    float    pulseAmount;          // Pulse intensity (default 0.0)
    float    edgeOpacity;          // Edge blend opacity (default 1.0)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<OutlineEdgeConstants> cb;

// ─── Depth Edge Detection (Roberts Cross) ────────────────────────────────

float DepthEdge(float2 uv) {
    float2 texel = cb.invResolution * cb.edgeThickness;

    float d00 = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
    float d10 = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2(texel.x, 0), 0);
    float d01 = g_SceneDepth.SampleLevel(g_LinearClamp, uv + float2(0, texel.y), 0);
    float d11 = g_SceneDepth.SampleLevel(g_LinearClamp, uv + texel, 0);

    float gx = d00 - d11;
    float gy = d10 - d01;

    float edge = sqrt(gx * gx + gy * gy);

    // Normalize by depth to handle perspective
    float avgDepth = (d00 + d10 + d01 + d11) * 0.25;
    if (avgDepth > 0.001) {
        edge /= avgDepth;
    }

    return step(cb.depthThreshold, edge);
}

// ─── Normal Edge Detection (Sobel) ───────────────────────────────────────

float NormalEdge(float2 uv) {
    float2 texel = cb.invResolution * cb.edgeThickness;

    float3 n00 = g_SceneNormal.SampleLevel(g_LinearClamp, uv + float2(-texel.x, -texel.y), 0).xyz * 2.0 - 1.0;
    float3 n10 = g_SceneNormal.SampleLevel(g_LinearClamp, uv + float2(0, -texel.y), 0).xyz * 2.0 - 1.0;
    float3 n20 = g_SceneNormal.SampleLevel(g_LinearClamp, uv + float2(texel.x, -texel.y), 0).xyz * 2.0 - 1.0;
    float3 n01 = g_SceneNormal.SampleLevel(g_LinearClamp, uv + float2(-texel.x, 0), 0).xyz * 2.0 - 1.0;
    float3 n21 = g_SceneNormal.SampleLevel(g_LinearClamp, uv + float2(texel.x, 0), 0).xyz * 2.0 - 1.0;
    float3 n02 = g_SceneNormal.SampleLevel(g_LinearClamp, uv + float2(-texel.x, texel.y), 0).xyz * 2.0 - 1.0;
    float3 n12 = g_SceneNormal.SampleLevel(g_LinearClamp, uv + float2(0, texel.y), 0).xyz * 2.0 - 1.0;
    float3 n22 = g_SceneNormal.SampleLevel(g_LinearClamp, uv + float2(texel.x, texel.y), 0).xyz * 2.0 - 1.0;

    // Sobel X
    float3 gx = -n00 - 2.0 * n01 - n02 + n20 + 2.0 * n21 + n22;
    // Sobel Y
    float3 gy = -n00 - 2.0 * n10 - n20 + n02 + 2.0 * n12 + n22;

    float edge = length(gx) + length(gy);

    return step(cb.normalThreshold, edge);
}

// ─── Color Edge Detection (Laplacian) ────────────────────────────────────

float ColorEdge(float2 uv) {
    float2 texel = cb.invResolution * cb.edgeThickness;

    float3 c = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;
    float3 l = g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(-texel.x, 0), 0).rgb;
    float3 r = g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(texel.x, 0), 0).rgb;
    float3 t = g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(0, -texel.y), 0).rgb;
    float3 b = g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(0, texel.y), 0).rgb;

    float3 laplacian = 4.0 * c - l - r - t - b;
    float edge = length(laplacian);

    return step(cb.colorThreshold, edge);
}

// ─── Object ID Edge Detection ────────────────────────────────────────────

float ObjectIDEdge(float2 uv) {
    int2 pixel = int2(uv * cb.resolution);
    int offset = int(cb.edgeThickness);

    uint center = g_ObjectID.Load(int3(pixel, 0));
    uint right  = g_ObjectID.Load(int3(pixel + int2(offset, 0), 0));
    uint down   = g_ObjectID.Load(int3(pixel + int2(0, offset), 0));

    return (center != right || center != down) ? 1.0 : 0.0;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float4 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);

    // ── Compute edge from multiple sources ───────────────────────────
    float edge = 0.0;

    if (cb.depthWeight > 0.0) {
        edge += DepthEdge(uv) * cb.depthWeight;
    }
    if (cb.normalWeight > 0.0) {
        edge += NormalEdge(uv) * cb.normalWeight;
    }
    if (cb.colorWeight > 0.0) {
        edge += ColorEdge(uv) * cb.colorWeight;
    }
    if (cb.objectIDWeight > 0.0) {
        edge += ObjectIDEdge(uv) * cb.objectIDWeight;
    }

    edge = saturate(edge);

    // ── Depth-based fade ─────────────────────────────────────────────
    float depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
    float depthFade = 1.0 - smoothstep(cb.depthFadeStart, cb.depthFadeEnd, depth);
    edge *= depthFade;

    // ── Animated pulse ───────────────────────────────────────────────
    if (cb.pulseSpeed > 0.0) {
        float pulse = sin(cb.time * cb.pulseSpeed) * 0.5 + 0.5;
        edge *= lerp(1.0 - cb.pulseAmount, 1.0, pulse);
    }

    // ── Determine edge color ─────────────────────────────────────────
    float3 finalEdgeColor;

    if (cb.edgeColorMode == 0) {
        finalEdgeColor = cb.edgeColor;
    } else if (cb.edgeColorMode == 1) {
        // Scene color darkened
        finalEdgeColor = sceneColor.rgb * 0.2;
    } else {
        // Inverted scene color
        finalEdgeColor = 1.0 - sceneColor.rgb;
    }

    // ── Blend edge with scene ────────────────────────────────────────
    float blendFactor = edge * cb.edgeOpacity;
    float3 result = lerp(sceneColor.rgb, finalEdgeColor, blendFactor);

    g_Output[DTid.xy] = float4(result, 1.0);
}
