// ─── Mip Downsample Compute Shader ───────────────────────────────────────
// Generates a single mip level from the previous mip.
// Supports Box (2×2 average), Kaiser, and Lanczos-3 filter kernels.
// sRGB-correct: converts to linear before filtering, back to sRGB after.

#define THREAD_GROUP_SIZE 8

cbuffer MipConstants : register(b0) {
    uint  g_SrcMip;
    uint  g_DstMip;
    uint  g_DstWidth;
    uint  g_DstHeight;
    uint  g_ArrayLayer;
    uint  g_IsSRGB;
    float g_InvDstWidth;
    float g_InvDstHeight;
};

Texture2D<float4>   g_SrcTexture : register(t0);
RWTexture2D<float4> g_DstTexture : register(u0);
SamplerState        g_LinearClamp : register(s0);

// ─── sRGB Conversion ─────────────────────────────────────────────────────

float3 SRGBToLinear(float3 srgb) {
    return float3(
        srgb.r <= 0.04045 ? srgb.r / 12.92 : pow((srgb.r + 0.055) / 1.055, 2.4),
        srgb.g <= 0.04045 ? srgb.g / 12.92 : pow((srgb.g + 0.055) / 1.055, 2.4),
        srgb.b <= 0.04045 ? srgb.b / 12.92 : pow((srgb.b + 0.055) / 1.055, 2.4)
    );
}

float3 LinearToSRGB(float3 linear) {
    return float3(
        linear.r <= 0.0031308 ? linear.r * 12.92 : 1.055 * pow(linear.r, 1.0 / 2.4) - 0.055,
        linear.g <= 0.0031308 ? linear.g * 12.92 : 1.055 * pow(linear.g, 1.0 / 2.4) - 0.055,
        linear.b <= 0.0031308 ? linear.b * 12.92 : 1.055 * pow(linear.b, 1.0 / 2.4) - 0.055
    );
}

float4 SampleLinear(float2 uv) {
    float4 c = g_SrcTexture.SampleLevel(g_LinearClamp, uv, g_SrcMip);
    if (g_IsSRGB) c.rgb = SRGBToLinear(c.rgb);
    return c;
}

float4 FinalizeOutput(float4 color) {
    if (g_IsSRGB) color.rgb = LinearToSRGB(color.rgb);
    return color;
}

// ─── Box Filter (2×2 Average) ────────────────────────────────────────────

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void CSBoxFilter(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= g_DstWidth || dtid.y >= g_DstHeight) return;

    float2 texelSize = float2(g_InvDstWidth, g_InvDstHeight);
    float2 uv = (float2(dtid.xy) + 0.5) * texelSize;

    // Sample 4 texels from the source mip
    float2 offsets[4] = {
        float2(-0.25, -0.25),
        float2( 0.25, -0.25),
        float2(-0.25,  0.25),
        float2( 0.25,  0.25),
    };

    float4 sum = float4(0, 0, 0, 0);
    [unroll]
    for (int i = 0; i < 4; i++) {
        sum += SampleLinear(uv + offsets[i] * texelSize);
    }

    g_DstTexture[dtid.xy] = FinalizeOutput(sum * 0.25);
}

// ─── Kaiser Filter (Windowed Sinc) ───────────────────────────────────────
// Kaiser-windowed sinc with 4×4 support. Better quality than box,
// preserves more high-frequency detail without ringing.

float Kaiser(float x, float alpha) {
    // Simplified Kaiser window approximation
    float t = x * x;
    float window = max(1.0 - t / (alpha * alpha), 0.0);
    return window * window; // Squared for smoother falloff
}

float Sinc(float x) {
    if (abs(x) < 1e-6) return 1.0;
    float px = x * 3.14159265;
    return sin(px) / px;
}

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void CSKaiserFilter(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= g_DstWidth || dtid.y >= g_DstHeight) return;

    float2 texelSize = float2(g_InvDstWidth, g_InvDstHeight);
    float2 center = (float2(dtid.xy) + 0.5) * texelSize;

    float4 sum = float4(0, 0, 0, 0);
    float weightSum = 0;

    // 4×4 sample grid
    [unroll]
    for (int y = -1; y <= 2; y++) {
        [unroll]
        for (int x = -1; x <= 2; x++) {
            float2 offset = float2(float(x) - 0.5, float(y) - 0.5);
            float2 sampleUV = center + offset * texelSize * 0.5;

            float dist = length(offset);
            float weight = Kaiser(dist, 2.0) * Sinc(dist);

            sum += SampleLinear(sampleUV) * weight;
            weightSum += weight;
        }
    }

    g_DstTexture[dtid.xy] = FinalizeOutput(sum / max(weightSum, 1e-6));
}

// ─── Lanczos-3 Filter ────────────────────────────────────────────────────
// Lanczos resampling with 3-lobe support. Sharpest of the three filters,
// may exhibit slight ringing on high-contrast edges.

float Lanczos3(float x) {
    if (abs(x) < 1e-6) return 1.0;
    if (abs(x) >= 3.0) return 0.0;
    float px = x * 3.14159265;
    float px3 = px / 3.0;
    return (sin(px) * sin(px3)) / (px * px3);
}

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void CSLanczosFilter(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= g_DstWidth || dtid.y >= g_DstHeight) return;

    float2 texelSize = float2(g_InvDstWidth, g_InvDstHeight);
    float2 center = (float2(dtid.xy) + 0.5) * texelSize;

    float4 sum = float4(0, 0, 0, 0);
    float weightSum = 0;

    // 6×6 sample grid for Lanczos-3
    [unroll]
    for (int y = -2; y <= 3; y++) {
        [unroll]
        for (int x = -2; x <= 3; x++) {
            float2 offset = float2(float(x) - 0.5, float(y) - 0.5);
            float2 sampleUV = center + offset * texelSize * 0.5;

            float wx = Lanczos3(offset.x);
            float wy = Lanczos3(offset.y);
            float weight = wx * wy;

            sum += SampleLinear(sampleUV) * weight;
            weightSum += weight;
        }
    }

    g_DstTexture[dtid.xy] = FinalizeOutput(sum / max(weightSum, 1e-6));
}
