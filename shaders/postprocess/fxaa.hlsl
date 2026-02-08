// ─── FXAA 3.11 (Fast Approximate Anti-Aliasing) ──────────────────────────
// Lightweight edge-based anti-aliasing that works on the final image.
// No temporal history needed — single-pass screen-space filter.
// Based on NVIDIA FXAA 3.11 by Timothy Lottes.
//
// Quality presets:
//   10 = low (fastest), 20 = medium, 29 = high, 39 = extreme (best quality)

#include "../common/math.hlsl"

struct FXAAConstants {
    uint2  screenSize;
    float  subpixelQuality;   // 0.0 = off, 0.75 = default, 1.0 = max subpixel AA
    float  edgeThreshold;     // 0.166 = default, lower = more edges detected
    float  edgeThresholdMin;  // 0.0833 = default, minimum luma for edge detection
    uint   pad0;
    uint   pad1;
    uint   pad2;
};

[[vk::push_constant]] ConstantBuffer<FXAAConstants> pc;

Texture2D<float4>   g_InputColor  : register(t0, space29);
RWTexture2D<float4> g_OutputColor : register(u0, space29);
SamplerState        g_LinearClamp : register(s0, space29);

// ─── Luma ────────────────────────────────────────────────────────────────

float FXAALuma(float3 rgb) {
    return dot(rgb, float3(0.299, 0.587, 0.114));
}

// ─── Main FXAA Kernel ────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.screenSize.x || DTid.y >= pc.screenSize.y) return;

    float2 texelSize = 1.0 / float2(pc.screenSize);
    float2 uv = (float2(DTid.xy) + 0.5) * texelSize;

    // Sample center and 4 neighbors
    float3 rgbM  = g_InputColor.SampleLevel(g_LinearClamp, uv, 0).rgb;
    float3 rgbN  = g_InputColor.SampleLevel(g_LinearClamp, uv + float2(0, -texelSize.y), 0).rgb;
    float3 rgbS  = g_InputColor.SampleLevel(g_LinearClamp, uv + float2(0,  texelSize.y), 0).rgb;
    float3 rgbW  = g_InputColor.SampleLevel(g_LinearClamp, uv + float2(-texelSize.x, 0), 0).rgb;
    float3 rgbE  = g_InputColor.SampleLevel(g_LinearClamp, uv + float2( texelSize.x, 0), 0).rgb;

    float lumaM = FXAALuma(rgbM);
    float lumaN = FXAALuma(rgbN);
    float lumaS = FXAALuma(rgbS);
    float lumaW = FXAALuma(rgbW);
    float lumaE = FXAALuma(rgbE);

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float lumaRange = lumaMax - lumaMin;

    // Early exit: not on an edge
    if (lumaRange < max(pc.edgeThresholdMin, lumaMax * pc.edgeThreshold)) {
        g_OutputColor[DTid.xy] = float4(rgbM, 1.0);
        return;
    }

    // Sample diagonal neighbors
    float3 rgbNW = g_InputColor.SampleLevel(g_LinearClamp, uv + float2(-texelSize.x, -texelSize.y), 0).rgb;
    float3 rgbNE = g_InputColor.SampleLevel(g_LinearClamp, uv + float2( texelSize.x, -texelSize.y), 0).rgb;
    float3 rgbSW = g_InputColor.SampleLevel(g_LinearClamp, uv + float2(-texelSize.x,  texelSize.y), 0).rgb;
    float3 rgbSE = g_InputColor.SampleLevel(g_LinearClamp, uv + float2( texelSize.x,  texelSize.y), 0).rgb;

    float lumaNW = FXAALuma(rgbNW);
    float lumaNE = FXAALuma(rgbNE);
    float lumaSW = FXAALuma(rgbSW);
    float lumaSE = FXAALuma(rgbSE);

    float lumaNS = lumaN + lumaS;
    float lumaWE = lumaW + lumaE;
    float lumaNWSW = lumaNW + lumaSW;
    float lumaNENE = lumaNE + lumaSE; // NE + SE
    float lumaNWNE = lumaNW + lumaNE;
    float lumaSWSE = lumaSW + lumaSE;

    // Determine edge direction (horizontal or vertical)
    float edgeHorz = abs(-2.0 * lumaW + lumaNWSW) +
                     abs(-2.0 * lumaM + lumaNS) * 2.0 +
                     abs(-2.0 * lumaE + lumaNENE);
    float edgeVert = abs(-2.0 * lumaN + lumaNWNE) +
                     abs(-2.0 * lumaM + lumaWE) * 2.0 +
                     abs(-2.0 * lumaS + lumaSWSE);

    bool isHorizontal = (edgeHorz >= edgeVert);

    // Select edge pair
    float luma1 = isHorizontal ? lumaN : lumaW;
    float luma2 = isHorizontal ? lumaS : lumaE;

    float gradient1 = abs(luma1 - lumaM);
    float gradient2 = abs(luma2 - lumaM);

    bool is1Steeper = gradient1 >= gradient2;

    float gradientScaled = 0.25 * max(gradient1, gradient2);

    // Step perpendicular to edge
    float stepLength = isHorizontal ? texelSize.y : texelSize.x;
    float lumaLocalAvg = 0;

    if (is1Steeper) {
        stepLength = -stepLength;
        lumaLocalAvg = 0.5 * (luma1 + lumaM);
    } else {
        lumaLocalAvg = 0.5 * (luma2 + lumaM);
    }

    float2 currentUV = uv;
    if (isHorizontal) {
        currentUV.y += stepLength * 0.5;
    } else {
        currentUV.x += stepLength * 0.5;
    }

    // Walk along the edge in both directions
    float2 offset = isHorizontal ? float2(texelSize.x, 0) : float2(0, texelSize.y);

    float2 uv1 = currentUV - offset;
    float2 uv2 = currentUV + offset;

    float lumaEnd1 = FXAALuma(g_InputColor.SampleLevel(g_LinearClamp, uv1, 0).rgb) - lumaLocalAvg;
    float lumaEnd2 = FXAALuma(g_InputColor.SampleLevel(g_LinearClamp, uv2, 0).rgb) - lumaLocalAvg;

    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;

    // Iterate along edge (up to 12 steps)
    [unroll]
    for (uint i = 0; i < 12; ++i) {
        if (!reached1) {
            uv1 -= offset;
            lumaEnd1 = FXAALuma(g_InputColor.SampleLevel(g_LinearClamp, uv1, 0).rgb) - lumaLocalAvg;
            reached1 = abs(lumaEnd1) >= gradientScaled;
        }
        if (!reached2) {
            uv2 += offset;
            lumaEnd2 = FXAALuma(g_InputColor.SampleLevel(g_LinearClamp, uv2, 0).rgb) - lumaLocalAvg;
            reached2 = abs(lumaEnd2) >= gradientScaled;
        }
        if (reached1 && reached2) break;
    }

    // Compute edge blend factor
    float dist1 = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
    float dist2 = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);

    bool isDirection1 = dist1 < dist2;
    float distFinal = min(dist1, dist2);
    float edgeLength = dist1 + dist2;
    float pixelOffset = -distFinal / edgeLength + 0.5;

    bool isLumaCenterSmaller = lumaM < lumaLocalAvg;
    bool correctVariation = ((isDirection1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCenterSmaller;
    float finalOffset = correctVariation ? pixelOffset : 0.0;

    // Subpixel aliasing
    float lumaAvg = (1.0 / 12.0) * (2.0 * (lumaNS + lumaWE) + lumaNWSW + lumaNENE);
    float subpixelOffset1 = clamp(abs(lumaAvg - lumaM) / lumaRange, 0.0, 1.0);
    float subpixelOffset2 = (-2.0 * subpixelOffset1 + 3.0) * subpixelOffset1 * subpixelOffset1;
    float subpixelOffsetFinal = subpixelOffset2 * subpixelOffset2 * pc.subpixelQuality;

    finalOffset = max(finalOffset, subpixelOffsetFinal);

    // Apply offset
    float2 finalUV = uv;
    if (isHorizontal) {
        finalUV.y += finalOffset * stepLength;
    } else {
        finalUV.x += finalOffset * stepLength;
    }

    float3 result = g_InputColor.SampleLevel(g_LinearClamp, finalUV, 0).rgb;
    g_OutputColor[DTid.xy] = float4(result, 1.0);
}
