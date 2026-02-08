// ─── Contrast Adaptive Sharpening (CAS) ──────────────────────────────────
// AMD FidelityFX CAS-inspired sharpening filter.
// Adapts sharpening strength based on local contrast — sharpens soft areas
// more while avoiding over-sharpening already sharp edges.
//
// Applied as a final post-process pass after TAA/upscaling to recover detail.

#include "../common/math.hlsl"

struct CASConstants {
    uint2  screenSize;
    float  sharpness;     // 0.0 = off, 0.5 = moderate, 1.0 = max sharpening
    uint   pad;
};

[[vk::push_constant]] ConstantBuffer<CASConstants> pc;

Texture2D<float4>   g_InputColor  : register(t0, space31);
RWTexture2D<float4> g_OutputColor : register(u0, space31);

// ─── CAS Core ────────────────────────────────────────────────────────────

float3 LoadRGB(int2 pos) {
    pos = clamp(pos, int2(0, 0), int2(pc.screenSize) - 1);
    return g_InputColor[pos].rgb;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.screenSize.x || DTid.y >= pc.screenSize.y) return;

    int2 pos = int2(DTid.xy);

    // 3×3 cross neighborhood (CAS uses a diamond pattern)
    float3 a = LoadRGB(pos + int2( 0, -1)); // North
    float3 b = LoadRGB(pos + int2(-1,  0)); // West
    float3 c = LoadRGB(pos);                 // Center
    float3 d = LoadRGB(pos + int2( 1,  0)); // East
    float3 e = LoadRGB(pos + int2( 0,  1)); // South

    // Also sample corners for better adaptation
    float3 f = LoadRGB(pos + int2(-1, -1)); // NW
    float3 g = LoadRGB(pos + int2( 1, -1)); // NE
    float3 h = LoadRGB(pos + int2(-1,  1)); // SW
    float3 i = LoadRGB(pos + int2( 1,  1)); // SE

    // Per-channel min/max of the neighborhood
    float3 minRGB = min(min(min(a, b), min(c, d)), e);
    float3 maxRGB = max(max(max(a, b), max(c, d)), e);

    // Include corners
    minRGB = min(minRGB, min(min(f, g), min(h, i)));
    maxRGB = max(maxRGB, max(max(f, g), max(h, i)));

    // Compute adaptive weight per channel
    // CAS: w = sqrt(min(min_rgb, 1-max_rgb) / max_rgb)
    // This makes the filter stronger where contrast is low
    float3 ampRGB = saturate(min(minRGB, 1.0 - maxRGB) / max(maxRGB, 0.001));
    ampRGB = sqrt(ampRGB);

    // Scale by user sharpness
    float3 w = ampRGB * lerp(-0.125, -0.2, pc.sharpness);

    // Apply sharpening: weighted average of neighbors vs center
    // Higher negative weight on center = more sharpening
    float3 sharpened = (a * w + b * w + d * w + e * w + c) / (4.0 * w + 1.0);

    // Clamp to avoid ringing artifacts
    sharpened = clamp(sharpened, minRGB, maxRGB);

    // Ensure non-negative
    sharpened = max(sharpened, 0.0);

    g_OutputColor[DTid.xy] = float4(sharpened, 1.0);
}
