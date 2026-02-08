// ─── Tone Mapping + Color Grading ─────────────────────────────────────────
// Supports multiple tonemapping operators:
//   - AgX (default, modern filmic)
//   - Tony McMapface (neutral, wide gamut)
//   - ACES (industry standard)
//   - Reinhard (simple reference)
//
// After tone mapping: apply color grading via 3D LUT, then sRGB gamma.

#include "../common/math.hlsl"

Texture2D<float4>   g_SceneHDR    : register(t0);
RWTexture2D<float4> g_OutputLDR   : register(u0);
Texture3D<float4>   g_ColorLUT    : register(t1); // 3D LUT for color grading
SamplerState        g_LinearClamp : register(s0);

struct TonemapPushConstants {
    uint   tonemapOperator;  // 0=AgX, 1=TonyMcMapface, 2=ACES, 3=Reinhard
    float  exposure;         // EV exposure adjustment
    float  contrast;         // Contrast adjustment (1.0 = neutral)
    float  saturation;       // Saturation (1.0 = neutral)
    float  whitePoint;       // White point for Reinhard
    uint   enableLUT;        // Apply color grading LUT
    uint   screenWidth;
    uint   screenHeight;
};

[[vk::push_constant]] ConstantBuffer<TonemapPushConstants> tmPC;

// ─── AgX Tone Mapping ────────────────────────────────────────────────────
// Open-source filmic tone mapper with good highlight handling.
// Attempt to approximate the AgX sigmoid curve.

float3 AgXDefaultContrastApprox(float3 x) {
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return + 15.5     * x4 * x2
           - 40.14    * x4 * x
           + 31.96    * x4
           - 6.868    * x2 * x
           + 0.4298   * x2
           + 0.1191   * x
           - 0.00232;
}

float3 AgX(float3 color) {
    // AgX input transform (to log space)
    const float3x3 agxTransform = float3x3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772, 0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104
    );

    float3 val = mul(agxTransform, color);
    val = clamp(log2(val), -12.47393, 4.026069);
    val = (val - (-12.47393)) / (4.026069 - (-12.47393));

    val = AgXDefaultContrastApprox(val);

    // AgX output transform
    const float3x3 agxInvTransform = float3x3(
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116
    );

    return mul(agxInvTransform, val);
}

// ─── ACES Tone Mapping ───────────────────────────────────────────────────
// Attempt to approximate the ACES RRT+ODT for sRGB.

float3 ACESApprox(float3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 ACESTonemap(float3 color) {
    const float3x3 ACESInput = float3x3(
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777
    );
    const float3x3 ACESOutput = float3x3(
         1.60475, -0.53108, -0.07367,
        -0.10208,  1.10813, -0.00605,
        -0.00327, -0.07276,  1.07602
    );
    color = mul(ACESInput, color);
    color = ACESApprox(color);
    return mul(ACESOutput, color);
}

// ─── Reinhard Tone Mapping ───────────────────────────────────────────────

float3 ReinhardTonemap(float3 color) {
    float wp2 = tmPC.whitePoint * tmPC.whitePoint;
    return color * (1.0 + color / wp2) / (1.0 + color);
}

// ─── Color Grading ───────────────────────────────────────────────────────

float3 ApplyColorGrading(float3 color) {
    // Contrast (around mid-gray)
    color = pow(color, tmPC.contrast);

    // Saturation
    float luma = Luminance(color);
    color = lerp(float3(luma, luma, luma), color, tmPC.saturation);

    // 3D LUT
    if (tmPC.enableLUT) {
        float3 lutCoord = saturate(color);
        // Scale to LUT dimensions (assuming 32x32x32)
        lutCoord = lutCoord * (31.0 / 32.0) + (0.5 / 32.0);
        color = g_ColorLUT.SampleLevel(g_LinearClamp, lutCoord, 0).rgb;
    }

    return color;
}

// ─── sRGB Gamma ──────────────────────────────────────────────────────────

float3 LinearToSRGB(float3 color) {
    float3 lo = color * 12.92;
    float3 hi = 1.055 * pow(color, 1.0 / 2.4) - 0.055;
    return lerp(hi, lo, step(color, 0.0031308));
}

// ─── Main ────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= tmPC.screenWidth || DTid.y >= tmPC.screenHeight) return;

    float3 hdrColor = g_SceneHDR[DTid.xy].rgb;

    // Exposure
    hdrColor *= exp2(tmPC.exposure);

    // Tone mapping
    float3 ldrColor;
    switch (tmPC.tonemapOperator) {
        case 0:  ldrColor = AgX(hdrColor); break;
        case 2:  ldrColor = ACESTonemap(hdrColor); break;
        case 3:  ldrColor = ReinhardTonemap(hdrColor); break;
        default: ldrColor = AgX(hdrColor); break; // Default to AgX
    }

    // Color grading
    ldrColor = ApplyColorGrading(ldrColor);

    // Gamma correction (linear → sRGB)
    ldrColor = LinearToSRGB(saturate(ldrColor));

    g_OutputLDR[DTid.xy] = float4(ldrColor, 1.0);
}
