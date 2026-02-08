// ─── Screen-Space Subsurface Scattering (Separable SSS) ─────────────────
// Implements the separable approximation of subsurface scattering for
// skin, wax, marble, and other translucent materials.
//
// Algorithm (Jimenez et al. 2015 - Separable SSS):
//   1. Render scene normally to get diffuse irradiance
//   2. Horizontal blur pass weighted by SSS kernel
//   3. Vertical blur pass weighted by SSS kernel
//   4. Kernel weights derived from sum-of-Gaussians diffusion profile
//
// The kernel is pre-computed from a diffusion profile and stored in a
// 1D texture. Depth and stencil are used to prevent bleeding across
// material boundaries.

#include "../common/math.hlsl"

Texture2D<float4> g_Irradiance    : register(t0); // Scene diffuse irradiance
Texture2D<float>  g_DepthBuffer   : register(t1);
Texture2D<uint>   g_StencilBuffer : register(t2); // Material ID / SSS mask
Texture1D<float4> g_SSSKernel     : register(t3); // Pre-computed kernel weights

RWTexture2D<float4> g_Output : register(u0);

SamplerState g_PointClamp  : register(s0);
SamplerState g_LinearClamp : register(s1);

struct SSSConstants {
    float2 resolution;
    float2 invResolution;
    float2 blurDirection;     // (1,0) for horizontal, (0,1) for vertical
    float  sssWidth;          // World-space scattering width (mm)
    float  maxDepthDiffMm;    // Depth threshold to prevent cross-object bleeding
    float  near;
    float  far;
    uint   kernelSize;        // Number of kernel samples (typically 25)
    uint   sssStencilBit;     // Stencil bit indicating SSS material
    float4x4 projection;
};

[[vk::push_constant]] ConstantBuffer<SSSConstants> cb;

// ─── Diffusion Profiles ──────────────────────────────────────────────────
// Sum-of-Gaussians approximation for skin diffusion.
// R(r) = sum_i w_i * G(v_i, r)
// where G(v, r) = exp(-r^2 / (2*v)) / (2*pi*v)

static const uint MAX_KERNEL_SIZE = 25;

// Pre-defined skin profile weights and variances
// Based on "A Practical Model for Subsurface Light Transport" (d'Eon, Luebke 2007)
float3 SkinDiffusionProfile(float r) {
    // Gaussian mixture for skin (red, green, blue channels)
    float3 result = float3(0, 0, 0);

    // Weight, variance pairs per channel (simplified 3-Gaussian fit)
    // Red channel scatters furthest
    result.r = 0.233f * exp(-r * r / (2.0 * 0.0064)) +
               0.100f * exp(-r * r / (2.0 * 0.0484)) +
               0.118f * exp(-r * r / (2.0 * 0.187));

    // Green channel
    result.g = 0.455f * exp(-r * r / (2.0 * 0.0101)) +
               0.336f * exp(-r * r / (2.0 * 0.0432)) +
               0.198f * exp(-r * r / (2.0 * 0.14));

    // Blue channel scatters least
    result.b = 0.649f * exp(-r * r / (2.0 * 0.0064)) +
               0.344f * exp(-r * r / (2.0 * 0.0292)) +
               0.043f * exp(-r * r / (2.0 * 0.0872));

    return result;
}

float LinearizeDepth(float d) {
    return cb.near * cb.far / (cb.far - d * (cb.far - cb.near));
}

// ─── Separable Blur Pass ─────────────────────────────────────────────────
// Called twice: once with blurDirection=(1,0), once with (0,1)

[numthreads(8, 8, 1)]
void CSBlur(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    // Check stencil — only blur SSS materials
    uint stencil = g_StencilBuffer.Load(int3(DTid.xy, 0));
    if ((stencil & cb.sssStencilBit) == 0) {
        g_Output[DTid.xy] = g_Irradiance.SampleLevel(g_PointClamp, uv, 0);
        return;
    }

    float centerDepth = LinearizeDepth(g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0));
    float4 centerColor = g_Irradiance.SampleLevel(g_PointClamp, uv, 0);

    // Scale the blur width based on depth (closer = wider scatter in screen space)
    float sssScale = cb.sssWidth / centerDepth;
    // Convert to pixels
    float pixelScale = sssScale * cb.projection[0][0] * cb.resolution.x * 0.5;
    pixelScale = clamp(pixelScale, 1.0, 64.0);

    float3 totalColor = float3(0, 0, 0);
    float3 totalWeight = float3(0, 0, 0);

    int halfKernel = int(cb.kernelSize) / 2;

    for (int i = -halfKernel; i <= halfKernel; ++i) {
        float offset = float(i);
        float2 sampleUV = uv + cb.blurDirection * offset * pixelScale * cb.invResolution;

        // Clamp to screen
        sampleUV = saturate(sampleUV);

        float sampleDepth = LinearizeDepth(g_DepthBuffer.SampleLevel(g_PointClamp, sampleUV, 0));
        float4 sampleColor = g_Irradiance.SampleLevel(g_LinearClamp, sampleUV, 0);

        // Check stencil of sample
        int2 sampleCoord = int2(sampleUV * cb.resolution);
        uint sampleStencil = g_StencilBuffer.Load(int3(sampleCoord, 0));

        // Depth-based edge stopping to prevent bleeding
        float depthDiff = abs(centerDepth - sampleDepth);
        bool validSample = (sampleStencil & cb.sssStencilBit) != 0 &&
                           depthDiff < cb.maxDepthDiffMm;

        // Kernel weight from diffusion profile
        float normalizedDist = abs(offset) / max(pixelScale, 1.0);
        float3 kernelWeight = SkinDiffusionProfile(normalizedDist * cb.sssWidth);

        if (!validSample) {
            // Fall back to center color for invalid samples
            kernelWeight *= 0.0;
        }

        totalColor += sampleColor.rgb * kernelWeight;
        totalWeight += kernelWeight;
    }

    // Normalize
    float3 result = totalColor / max(totalWeight, float3(0.001, 0.001, 0.001));

    g_Output[DTid.xy] = float4(result, centerColor.a);
}

// ─── Transmittance Pass ──────────────────────────────────────────────────
// Approximates light transmission through thin geometry (ears, nostrils).
// Uses shadow map depth to estimate thickness.

Texture2D<float>  g_ShadowMap      : register(t4);
RWTexture2D<float4> g_Transmittance : register(u1);

struct TransmittanceConstants {
    float2 resolution;
    float2 invResolution;
    float4x4 lightViewProj;
    float3 lightColor;
    float  translucency;     // Material translucency factor [0,1]
    float  sssWidth;
    float  near;
    float  far;
    float  pad0;
};

// Separate push constant block for transmittance pass would be bound separately
// For this shader, reusing cb with transmittance-specific values

[numthreads(8, 8, 1)]
void CSTransmittance(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    uint stencil = g_StencilBuffer.Load(int3(DTid.xy, 0));
    if ((stencil & cb.sssStencilBit) == 0) {
        g_Transmittance[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    float depth = g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0);
    float linearDepth = LinearizeDepth(depth);

    // Reconstruct world position and project into shadow map
    // (Simplified — would use full inverse VP in production)
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;

    // Estimate thickness from shadow map depth difference
    // For a proper implementation, project world pos into light space
    float shadowDepth = g_ShadowMap.SampleLevel(g_LinearClamp, uv, 0);
    float thickness = max(linearDepth - shadowDepth, 0.0);

    // Compute transmittance using diffusion profile
    float3 transmittance = SkinDiffusionProfile(thickness / cb.sssWidth);
    transmittance = saturate(transmittance);

    g_Transmittance[DTid.xy] = float4(transmittance, 1.0);
}

// ─── Combine Pass ────────────────────────────────────────────────────────
// Blends SSS-blurred irradiance with specular and non-SSS materials.

Texture2D<float4> g_SSSResult     : register(t5); // After H+V blur
Texture2D<float4> g_Specular      : register(t6);
Texture2D<float4> g_TransmResult  : register(t7);
RWTexture2D<float4> g_FinalOutput : register(u2);

[numthreads(8, 8, 1)]
void CSCombine(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    uint stencil = g_StencilBuffer.Load(int3(DTid.xy, 0));
    float4 original = g_Irradiance.SampleLevel(g_PointClamp, uv, 0);

    if ((stencil & cb.sssStencilBit) == 0) {
        // Non-SSS material — pass through
        g_FinalOutput[DTid.xy] = original;
        return;
    }

    float4 sssColor = g_SSSResult.SampleLevel(g_PointClamp, uv, 0);
    float4 specular = g_Specular.SampleLevel(g_PointClamp, uv, 0);
    float4 transmittance = g_TransmResult.SampleLevel(g_PointClamp, uv, 0);

    // Combine: SSS diffuse + specular (unblurred) + transmittance
    float3 finalColor = sssColor.rgb + specular.rgb + transmittance.rgb * original.rgb;

    g_FinalOutput[DTid.xy] = float4(finalColor, original.a);
}
