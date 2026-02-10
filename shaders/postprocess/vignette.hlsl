// ─── Vignette Shader ─────────────────────────────────────────────────────
// Screen-space post-process that darkens the edges of the frame for a
// cinematic or photographic look. Standalone configurable vignette.
//
// Features:
//   - Circular and elliptical vignette shapes
//   - Configurable inner/outer radius and softness
//   - Color tint (not just black darkening)
//   - Rounded rectangle mode for widescreen
//   - Animated pulse for gameplay effects
//   - Opacity control

#include "../common/math.hlsl"

Texture2D<float4> g_SceneColor : register(t0);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct VignetteConstants {
    float2   resolution;
    float2   invResolution;
    float    time;

    float2   center;              // Vignette center (default: 0.5, 0.5)
    float    innerRadius;         // No darkening inside this (default: 0.3)
    float    outerRadius;         // Full darkening outside this (default: 0.9)
    float    softness;            // Edge softness (default: 0.5)
    float    intensity;           // Darkening strength (default: 1.0)
    float    roundness;           // 0=elliptical, 1=circular (default: 1.0)
    float3   color;               // Vignette color (default: 0,0,0)
    float    aspectCorrection;    // Correct for aspect ratio (default: 1.0)
    float    pulseSpeed;          // Animated pulse speed (default: 0.0)
    float    pulseAmount;         // Pulse intensity variation (default: 0.0)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<VignetteConstants> cb;

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;
    float3 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;

    // Center-relative UV
    float2 d = uv - cb.center;

    // Aspect ratio correction
    if (cb.aspectCorrection > 0.0) {
        float aspect = cb.resolution.x / cb.resolution.y;
        d.x *= lerp(1.0, aspect, cb.aspectCorrection);
    }

    // Distance from center
    float dist;
    if (cb.roundness >= 1.0) {
        dist = length(d);
    } else {
        // Blend between box and circle
        float circle = length(d);
        float box = max(abs(d.x), abs(d.y));
        dist = lerp(box, circle, cb.roundness);
    }

    // Animated pulse
    float outerR = cb.outerRadius;
    float innerR = cb.innerRadius;
    if (cb.pulseSpeed > 0.0 && cb.pulseAmount > 0.0) {
        float pulse = sin(cb.time * cb.pulseSpeed) * cb.pulseAmount;
        outerR += pulse * 0.1;
        innerR += pulse * 0.05;
    }

    // Vignette factor
    float vignette = smoothstep(innerR, outerR, dist);
    vignette = pow(vignette, cb.softness);
    vignette *= cb.intensity;

    // Apply
    float3 result = lerp(sceneColor, cb.color, vignette);

    g_Output[DTid.xy] = float4(result, 1.0);
}
