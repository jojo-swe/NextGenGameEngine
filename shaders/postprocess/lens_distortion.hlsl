// ─── Lens Distortion Shader ──────────────────────────────────────────────
// Screen-space post-process that simulates optical lens distortion
// (barrel, pincushion, mustache) for camera simulation or stylized effects.
//
// Features:
//   - Barrel distortion (wide-angle / fisheye)
//   - Pincushion distortion (telephoto)
//   - Mustache distortion (barrel center + pincushion edges)
//   - Per-channel distortion (chromatic fringe at edges)
//   - Configurable distortion coefficients (k1, k2, k3)
//   - Center offset for off-axis lenses
//   - Edge darkening from distortion falloff
//   - Aspect ratio correction

#include "../common/math.hlsl"

Texture2D<float4> g_SceneColor : register(t0);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct LensDistortionConstants {
    float2   resolution;
    float2   invResolution;

    float    k1;                  // Primary radial distortion (default: 0.0)
    float    k2;                  // Secondary radial distortion (default: 0.0)
    float    k3;                  // Tertiary radial distortion (default: 0.0)
    float2   center;              // Distortion center (default: 0.5, 0.5)
    float    chromaticShift;      // Per-channel distortion offset (default: 0.0)
    float    scale;               // Post-distortion zoom (default: 1.0)
    float    edgeDarken;          // Darken distorted edges (default: 0.0)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<LensDistortionConstants> cb;

float2 Distort(float2 uv, float extraK) {
    float2 d = uv - cb.center;
    float aspect = cb.resolution.x / cb.resolution.y;
    d.x *= aspect;

    float r2 = dot(d, d);
    float r4 = r2 * r2;
    float r6 = r4 * r2;

    float k1 = cb.k1 + extraK;
    float distortion = 1.0 + k1 * r2 + cb.k2 * r4 + cb.k3 * r6;

    d *= distortion;
    d.x /= aspect;

    // Scale to compensate for distortion
    d /= max(cb.scale, 0.01);

    return d + cb.center;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float3 color;

    if (abs(cb.chromaticShift) > 0.0001) {
        // Per-channel distortion for chromatic fringe
        float2 uvR = Distort(uv, -cb.chromaticShift);
        float2 uvG = Distort(uv, 0.0);
        float2 uvB = Distort(uv, cb.chromaticShift);

        float r = g_SceneColor.SampleLevel(g_LinearClamp, saturate(uvR), 0).r;
        float g = g_SceneColor.SampleLevel(g_LinearClamp, saturate(uvG), 0).g;
        float b = g_SceneColor.SampleLevel(g_LinearClamp, saturate(uvB), 0).b;

        color = float3(r, g, b);

        // Black out pixels that sample outside [0,1]
        if (any(uvR < 0.0) || any(uvR > 1.0) ||
            any(uvB < 0.0) || any(uvB > 1.0)) {
            color = float3(0, 0, 0);
        }
    } else {
        float2 distUV = Distort(uv, 0.0);

        if (any(distUV < 0.0) || any(distUV > 1.0)) {
            color = float3(0, 0, 0);
        } else {
            color = g_SceneColor.SampleLevel(g_LinearClamp, distUV, 0).rgb;
        }
    }

    // Edge darkening
    if (cb.edgeDarken > 0.0) {
        float2 d = uv - cb.center;
        float r2 = dot(d, d) * 4.0;
        float darken = 1.0 - r2 * cb.edgeDarken;
        color *= max(darken, 0.0);
    }

    g_Output[DTid.xy] = float4(color, 1.0);
}
