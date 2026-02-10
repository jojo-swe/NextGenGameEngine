// ─── Tilt-Shift / Miniature Effect Shader ────────────────────────────────
// Screen-space post-process that simulates a tilt-shift lens, creating
// a miniature/diorama look by blurring areas above and below a focus band.
//
// Features:
//   - Configurable focus band position and width
//   - Smooth gradient blur falloff
//   - Horizontal or vertical tilt axis
//   - Rotation angle for diagonal tilt
//   - Saturation boost for toy-like look
//   - Bokeh-style blur (weighted Gaussian)
//   - Depth-aware mode (use actual depth buffer)

#include "../common/math.hlsl"

Texture2D<float4> g_SceneColor : register(t0);
Texture2D<float>  g_SceneDepth : register(t1);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct TiltShiftConstants {
    float2   resolution;
    float2   invResolution;
    float    time;

    float    focusCenter;         // Focus band center 0-1 (default: 0.5)
    float    focusWidth;          // Focus band half-width (default: 0.15)
    float    blurAmount;          // Max blur radius in pixels (default: 8.0)
    float    blurFalloff;         // Gradient sharpness (default: 2.0)
    float    angle;               // Tilt axis rotation in radians (default: 0.0)
    float    saturationBoost;     // Color saturation boost (default: 1.2)
    u32      useDepth;            // 0=screen-space band, 1=depth-based
    float    depthFocusNear;      // Depth focus near plane (default: 0.3)
    float    depthFocusFar;       // Depth focus far plane (default: 0.6)
    u32      blurSamples;         // Blur quality 4-16 (default: 8)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<TiltShiftConstants> cb;

float ComputeBlurFactor(float2 uv, float depth) {
    if (cb.useDepth != 0) {
        // Depth-based focus
        float focusMid = (cb.depthFocusNear + cb.depthFocusFar) * 0.5;
        float focusRange = (cb.depthFocusFar - cb.depthFocusNear) * 0.5;
        float dist = abs(depth - focusMid);
        return smoothstep(0.0, focusRange, dist);
    }

    // Screen-space band
    float2 center = float2(0.5, cb.focusCenter);

    // Rotate UV around center
    float2 d = uv - center;
    float c = cos(cb.angle);
    float s = sin(cb.angle);
    float rotY = -d.x * s + d.y * c;

    float dist = abs(rotY);
    return pow(smoothstep(0.0, cb.focusWidth, dist - cb.focusWidth), cb.blurFalloff);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float depth = 0.0;
    if (cb.useDepth != 0) {
        depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
    }

    float blurFactor = ComputeBlurFactor(uv, depth);
    float radius = blurFactor * cb.blurAmount;

    float3 color;

    if (radius < 0.5) {
        // In focus — no blur needed
        color = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;
    } else {
        // Weighted disc blur
        float3 sum = float3(0, 0, 0);
        float totalWeight = 0.0;
        int samples = int(min(cb.blurSamples, 16u));

        float angleStep = 6.28318 / float(samples);

        for (int ring = 1; ring <= 3; ++ring) {
            float r = radius * float(ring) / 3.0;
            for (int i = 0; i < samples; ++i) {
                float a = float(i) * angleStep + float(ring) * 0.5;
                float2 offset = float2(cos(a), sin(a)) * r * cb.invResolution;
                float w = 1.0 / float(ring); // Inner rings weighted more
                sum += g_SceneColor.SampleLevel(g_LinearClamp, uv + offset, 0).rgb * w;
                totalWeight += w;
            }
        }

        // Add center sample
        sum += g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;
        totalWeight += 1.0;

        color = sum / totalWeight;
    }

    // Saturation boost for toy/miniature look
    if (cb.saturationBoost != 1.0) {
        float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
        color = lerp(float3(lum, lum, lum), color, cb.saturationBoost);
    }

    g_Output[DTid.xy] = float4(saturate(color), 1.0);
}
