// ─── Procedural Chromatic Depth-of-Field Bokeh Shader ────────────────────
// Screen-space post-process for cinematic depth of field with chromatic
// bokeh shapes, variable aperture simulation, and optical aberrations.
//
// Features:
//   - Circle-of-confusion (CoC) from depth buffer
//   - Chromatic bokeh (RGB channels separated radially)
//   - Configurable aperture blade count (hex, octagon, circle)
//   - Bokeh shape rotation with aperture angle
//   - Near-field and far-field DOF with smooth transition
//   - Bokeh brightness weighting (cats-eye / optical vignette)
//   - Anamorphic stretch (horizontal oval bokeh)
//   - Focus distance and range controls
//   - Foreground bleed protection (alpha premultiply)
//
// References:
//   - "Next Generation Post Processing in Call of Duty: AW" (Jimenez, SIGGRAPH 2014)
//   - "Bokeh DOF in Unreal Engine" (Epic Games)
//   - "Practical Post-Process DOF" (Kawase, GDC 2009)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor : register(t0);
Texture2D<float>  g_SceneDepth : register(t1);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct ChromaticBokehConstants {
    float2   resolution;
    float2   invResolution;
    float    focusDistance;        // Focus plane depth (default 10.0)
    float    focusRange;          // Depth range in focus (default 5.0)
    float    maxBlurRadius;       // Max CoC radius in pixels (default 16.0)
    float    aperture;            // F-stop simulation (default 2.8)
    u32      bladeCount;          // Aperture blades (0=circle, 5-8 typical, default 6)
    float    bladeRotation;       // Aperture rotation in radians (default 0.0)
    float    chromaticAmount;     // RGB separation amount (default 0.02)
    float    anamorphicStretch;   // Horizontal stretch (1.0=circle, 2.0=oval, default 1.0)
    float    bokehBrightness;     // Bright spot enhancement (default 2.0)
    float    bokehThreshold;      // Luminance threshold for bright bokeh (default 0.8)
    float    nearBlurScale;       // Near-field blur multiplier (default 1.0)
    float    farBlurScale;        // Far-field blur multiplier (default 1.0)
    float    catsEyeAmount;       // Optical vignette on bokeh (default 0.3)
    float    sampleQuality;       // 0=low(16), 1=medium(32), 2=high(64) (default 1)
    float    pad0;
    float    pad1;
};

[[vk::push_constant]] ConstantBuffer<ChromaticBokehConstants> cb;

// ─── Circle of Confusion ─────────────────────────────────────────────────

float ComputeCoC(float depth) {
    float dist = abs(depth - cb.focusDistance);
    float halfRange = cb.focusRange * 0.5;

    // Signed CoC: negative = near, positive = far
    float sign = (depth < cb.focusDistance) ? -1.0 : 1.0;

    float coc = smoothstep(0.0, halfRange * 2.0, dist) * cb.maxBlurRadius * sign;

    // Scale by near/far multipliers
    if (coc < 0.0) coc *= cb.nearBlurScale;
    else coc *= cb.farBlurScale;

    return coc;
}

// ─── Aperture Shape ──────────────────────────────────────────────────────

float ApertureShape(float2 offset, u32 blades, float rotation) {
    if (blades == 0) {
        // Perfect circle
        return length(offset) <= 1.0 ? 1.0 : 0.0;
    }

    // Polygon aperture
    float angle = atan2(offset.y, offset.x) + rotation;
    float r = length(offset);

    float segmentAngle = 6.28318 / float(blades);
    float halfAngle = segmentAngle * 0.5;

    // Distance to nearest polygon edge
    float theta = fmod(abs(angle), segmentAngle);
    if (theta > halfAngle) theta = segmentAngle - theta;

    float polyRadius = cos(halfAngle) / cos(theta);

    return r <= polyRadius ? 1.0 : 0.0;
}

// ─── Cats-Eye Vignette ───────────────────────────────────────────────────
// Simulates optical vignette where bokeh becomes cats-eye shaped at edges.

float CatsEyeFactor(float2 screenUV, float2 sampleOffset) {
    float2 toEdge = screenUV * 2.0 - 1.0;
    float edgeDist = length(toEdge);
    float vignette = 1.0 - edgeDist * cb.catsEyeAmount;

    // Clip bokeh toward screen center at edges
    float2 toCenter = -normalize(toEdge + 0.001);
    float alignment = dot(normalize(sampleOffset + 0.001), toCenter);
    float catsEye = lerp(1.0, max(0.0, alignment * 0.5 + 0.5), edgeDist * cb.catsEyeAmount);

    return saturate(vignette * catsEye);
}

// ─── Generate Sample Points ──────────────────────────────────────────────

static const u32 MAX_SAMPLES = 64;

u32 GetSampleCount() {
    if (cb.sampleQuality >= 2.0) return 64;
    if (cb.sampleQuality >= 1.0) return 32;
    return 16;
}

// Golden angle spiral for sample distribution
float2 GoldenSpiralSample(u32 index, u32 total) {
    float goldenAngle = 2.39996; // radians
    float r = sqrt(float(index) / float(total));
    float theta = float(index) * goldenAngle;
    return float2(cos(theta), sin(theta)) * r;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float centerDepth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
    float coc = ComputeCoC(centerDepth);
    float absCoc = abs(coc);

    // Skip if in focus
    if (absCoc < 0.5) {
        float4 sharp = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);
        g_Output[DTid.xy] = sharp;
        return;
    }

    u32 sampleCount = GetSampleCount();
    float blurRadius = absCoc * cb.invResolution.y; // Normalized radius

    // Accumulate bokeh
    float3 colorR = float3(0, 0, 0);
    float3 colorG = float3(0, 0, 0);
    float3 colorB = float3(0, 0, 0);
    float weightR = 0.0;
    float weightG = 0.0;
    float weightB = 0.0;

    for (u32 i = 0; i < sampleCount; ++i) {
        float2 offset = GoldenSpiralSample(i, sampleCount);

        // Apply aperture shape
        float apertureWeight = ApertureShape(offset, cb.bladeCount, cb.bladeRotation);
        if (apertureWeight <= 0.0) continue;

        // Apply anamorphic stretch
        offset.x *= cb.anamorphicStretch;

        // Cats-eye vignette
        float catsEye = CatsEyeFactor(uv, offset);
        float sampleWeight = apertureWeight * catsEye;

        // Chromatic separation: shift R outward, B inward
        float chromaticShift = cb.chromaticAmount;
        float2 offsetR = offset * (1.0 + chromaticShift);
        float2 offsetG = offset;
        float2 offsetB = offset * (1.0 - chromaticShift);

        float2 sampleUV_R = uv + offsetR * blurRadius;
        float2 sampleUV_G = uv + offsetG * blurRadius;
        float2 sampleUV_B = uv + offsetB * blurRadius;

        sampleUV_R = clamp(sampleUV_R, 0.0, 1.0);
        sampleUV_G = clamp(sampleUV_G, 0.0, 1.0);
        sampleUV_B = clamp(sampleUV_B, 0.0, 1.0);

        // Sample each channel
        float sR = g_SceneColor.SampleLevel(g_LinearClamp, sampleUV_R, 0).r;
        float3 sG_full = g_SceneColor.SampleLevel(g_LinearClamp, sampleUV_G, 0).rgb;
        float sB = g_SceneColor.SampleLevel(g_LinearClamp, sampleUV_B, 0).b;

        // Check sample depth for foreground bleed protection
        float sampleDepth = g_SceneDepth.SampleLevel(g_LinearClamp, sampleUV_G, 0);
        float sampleCoc = ComputeCoC(sampleDepth);

        // Only include sample if it's also out of focus or behind
        float depthWeight = (abs(sampleCoc) >= 0.5 || sampleDepth >= centerDepth) ? 1.0 : 0.3;

        // Bright bokeh enhancement
        float sampleLum = dot(sG_full, float3(0.2126, 0.7152, 0.0722));
        float brightBoost = 1.0 + max(0.0, sampleLum - cb.bokehThreshold) * cb.bokehBrightness;

        float w = sampleWeight * depthWeight * brightBoost;

        colorR += float3(sR, 0, 0) * w;
        colorG += float3(0, sG_full.g, 0) * w;
        colorB += float3(0, 0, sB) * w;

        weightR += w;
        weightG += w;
        weightB += w;
    }

    // Normalize
    float3 finalR = weightR > 0.0 ? colorR / weightR : float3(0, 0, 0);
    float3 finalG = weightG > 0.0 ? colorG / weightG : float3(0, 0, 0);
    float3 finalB = weightB > 0.0 ? colorB / weightB : float3(0, 0, 0);

    float3 bokehColor = finalR + finalG + finalB;

    // Blend with sharp image based on CoC
    float4 sharpColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);
    float blendFactor = smoothstep(0.5, 2.0, absCoc);
    float3 result = lerp(sharpColor.rgb, bokehColor, blendFactor);

    g_Output[DTid.xy] = float4(result, 1.0);
}
