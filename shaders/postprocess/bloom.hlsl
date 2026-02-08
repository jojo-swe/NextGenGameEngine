// ─── Bloom: Compute-Based Downsample/Upsample Chain ──────────────────────
// Energy-conserving bloom with Karis average for firefly suppression.
// Based on Call of Duty: Advanced Warfare bloom (Jimenez, 2014).
//
// Pipeline:
//   1. Threshold + Downsample (13-tap, Karis average on first pass)
//   2. Successive downsamples (13-tap tent filter)
//   3. Upsample with bilinear + tent blend
//   4. Final composite: scene + bloom * intensity

#include "../common/math.hlsl"

// ─── Downsample ──────────────────────────────────────────────────────────

Texture2D<float4>   g_Input        : register(t0);
RWTexture2D<float4> g_Output       : register(u0);
SamplerState        g_LinearClamp  : register(s0);

struct BloomPushConstants {
    float2 texelSize;     // 1.0 / outputResolution
    float  threshold;     // Bloom threshold luminance
    float  softKnee;      // Soft threshold transition
    uint   mipLevel;      // Current mip being processed
    uint   isFirstPass;   // 1 = apply threshold + Karis
    float  bloomIntensity;
    float  pad;
};

[[vk::push_constant]] ConstantBuffer<BloomPushConstants> bloomPC;

float Luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Karis average: weighted average that suppresses fireflies
// Weight = 1 / (1 + Luminance(color))
float KarisWeight(float3 color) {
    return 1.0 / (1.0 + Luminance(color));
}

// Soft threshold curve
float3 ThresholdFilter(float3 color) {
    float brightness = Luminance(color);
    float knee = bloomPC.threshold * bloomPC.softKnee;
    float soft = brightness - bloomPC.threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + EPSILON);
    float contribution = max(soft, brightness - bloomPC.threshold);
    contribution /= max(brightness, EPSILON);
    return color * max(contribution, 0.0);
}

// 13-tap tent filter downsample (Jimenez 2014)
// Samples in a cross pattern with proper weights for energy conservation
float3 DownsampleBox13Tap(Texture2D<float4> tex, SamplerState samp, float2 uv, float2 texelSize) {
    float3 A = tex.SampleLevel(samp, uv + texelSize * float2(-1, -1), 0).rgb;
    float3 B = tex.SampleLevel(samp, uv + texelSize * float2( 0, -1), 0).rgb;
    float3 C = tex.SampleLevel(samp, uv + texelSize * float2( 1, -1), 0).rgb;
    float3 D = tex.SampleLevel(samp, uv + texelSize * float2(-0.5, -0.5), 0).rgb;
    float3 E = tex.SampleLevel(samp, uv + texelSize * float2( 0.5, -0.5), 0).rgb;
    float3 F = tex.SampleLevel(samp, uv + texelSize * float2(-1,  0), 0).rgb;
    float3 G = tex.SampleLevel(samp, uv,                               0).rgb;
    float3 H = tex.SampleLevel(samp, uv + texelSize * float2( 1,  0), 0).rgb;
    float3 I = tex.SampleLevel(samp, uv + texelSize * float2(-0.5, 0.5), 0).rgb;
    float3 J = tex.SampleLevel(samp, uv + texelSize * float2( 0.5, 0.5), 0).rgb;
    float3 K = tex.SampleLevel(samp, uv + texelSize * float2(-1,  1), 0).rgb;
    float3 L = tex.SampleLevel(samp, uv + texelSize * float2( 0,  1), 0).rgb;
    float3 M = tex.SampleLevel(samp, uv + texelSize * float2( 1,  1), 0).rgb;

    if (bloomPC.isFirstPass) {
        // Apply Karis average to suppress fireflies
        float3 g0 = (D + E + I + J) * 0.25;
        float3 g1 = (A + B + F + G) * 0.25;
        float3 g2 = (B + C + G + H) * 0.25;
        float3 g3 = (F + G + K + L) * 0.25;
        float3 g4 = (G + H + L + M) * 0.25;

        float w0 = KarisWeight(g0);
        float w1 = KarisWeight(g1);
        float w2 = KarisWeight(g2);
        float w3 = KarisWeight(g3);
        float w4 = KarisWeight(g4);

        return (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4)
             / (w0 + w1 + w2 + w3 + w4 + EPSILON);
    }

    // Standard 13-tap weights
    float3 result = G * 0.125;
    result += (D + E + I + J) * 0.125;
    result += (A + C + K + M) * 0.03125;
    result += (B + F + H + L) * 0.0625;
    return result;
}

[numthreads(8, 8, 1)]
void DownsampleCS(uint3 DTid : SV_DispatchThreadID) {
    float2 uv = (float2(DTid.xy) + 0.5) * bloomPC.texelSize;
    float3 color = DownsampleBox13Tap(g_Input, g_LinearClamp, uv, bloomPC.texelSize);

    if (bloomPC.isFirstPass) {
        color = ThresholdFilter(color);
    }

    g_Output[DTid.xy] = float4(color, 1.0);
}

// ─── Upsample ────────────────────────────────────────────────────────────
// 9-tap tent filter for smooth upsampling

Texture2D<float4>   g_HighRes  : register(t1);

float3 UpsampleTent9Tap(Texture2D<float4> tex, SamplerState samp, float2 uv, float2 texelSize) {
    float3 result = float3(0, 0, 0);
    result += tex.SampleLevel(samp, uv + texelSize * float2(-1, -1), 0).rgb * 1.0;
    result += tex.SampleLevel(samp, uv + texelSize * float2( 0, -1), 0).rgb * 2.0;
    result += tex.SampleLevel(samp, uv + texelSize * float2( 1, -1), 0).rgb * 1.0;
    result += tex.SampleLevel(samp, uv + texelSize * float2(-1,  0), 0).rgb * 2.0;
    result += tex.SampleLevel(samp, uv,                               0).rgb * 4.0;
    result += tex.SampleLevel(samp, uv + texelSize * float2( 1,  0), 0).rgb * 2.0;
    result += tex.SampleLevel(samp, uv + texelSize * float2(-1,  1), 0).rgb * 1.0;
    result += tex.SampleLevel(samp, uv + texelSize * float2( 0,  1), 0).rgb * 2.0;
    result += tex.SampleLevel(samp, uv + texelSize * float2( 1,  1), 0).rgb * 1.0;
    return result / 16.0;
}

[numthreads(8, 8, 1)]
void UpsampleCS(uint3 DTid : SV_DispatchThreadID) {
    float2 uv = (float2(DTid.xy) + 0.5) * bloomPC.texelSize;

    float3 upsampled = UpsampleTent9Tap(g_Input, g_LinearClamp, uv, bloomPC.texelSize);
    float3 highRes = g_HighRes.SampleLevel(g_LinearClamp, uv, 0).rgb;

    // Additive blend
    g_Output[DTid.xy] = float4(highRes + upsampled, 1.0);
}
