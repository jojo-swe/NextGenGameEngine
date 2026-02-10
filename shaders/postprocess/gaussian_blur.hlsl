// ─── Gaussian Blur Utility Shader ────────────────────────────────────────
// Separable Gaussian blur compute shader. Run horizontal pass first,
// then vertical pass on the result. Used as a building block for bloom,
// DOF, tilt-shift, and any effect needing smooth blurring.
//
// Features:
//   - Separable two-pass (horizontal + vertical)
//   - Configurable kernel radius (1-32 taps)
//   - Sigma auto-calculated or manual
//   - Multi-resolution support (half/quarter res)
//   - Weighted by precomputed Gaussian coefficients

#include "../common/math.hlsl"

Texture2D<float4> g_Input : register(t0);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct GaussianBlurConstants {
    float2   resolution;
    float2   invResolution;
    u32      direction;           // 0=horizontal, 1=vertical
    u32      kernelRadius;        // Tap radius 1-32 (default: 7)
    float    sigma;               // Gaussian sigma (default: 0.0 = auto from radius)
    float    intensity;           // Output multiplier (default: 1.0)
};

[[vk::push_constant]] ConstantBuffer<GaussianBlurConstants> cb;

float GaussianWeight(float offset, float sigma) {
    return exp(-(offset * offset) / (2.0 * sigma * sigma));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float sigma = cb.sigma > 0.0 ? cb.sigma : float(cb.kernelRadius) * 0.5;
    int radius = int(min(cb.kernelRadius, 32u));

    float2 dir = cb.direction == 0
        ? float2(cb.invResolution.x, 0.0)
        : float2(0.0, cb.invResolution.y);

    float4 sum = float4(0, 0, 0, 0);
    float totalWeight = 0.0;

    for (int i = -radius; i <= radius; ++i) {
        float w = GaussianWeight(float(i), sigma);
        float2 sampleUV = uv + dir * float(i);
        sum += g_Input.SampleLevel(g_LinearClamp, sampleUV, 0) * w;
        totalWeight += w;
    }

    float4 result = sum / totalWeight;
    result *= cb.intensity;

    g_Output[DTid.xy] = result;
}
