// ─── Variable Rate Shading (VRS) Image Generation ────────────────────────
// Analyzes the previous frame's luminance variance and motion to generate
// a Shading Rate Image (SRI) for content-adaptive VRS.
//
// Regions with low variance or high motion → coarser shading (2×2, 4×4)
// Edges and detail areas → full rate (1×1)
// Saves 20-40% shading cost with minimal quality impact.
//
// Output: R8_UINT texture where each texel covers a 16×16 pixel tile.
// Values encode VkFragmentShadingRateCombinerOpKHR rates:
//   0 = 1×1 (full), 1 = 1×2, 2 = 2×1, 3 = 2×2, 4 = 2×4, 5 = 4×2, 6 = 4×4

#include "../common/math.hlsl"

struct VRSConstants {
    uint2  screenSize;       // Full resolution
    uint2  tileSize;         // SRI tile size (16×16 typically)
    uint2  sriSize;          // SRI dimensions = screenSize / tileSize
    float  varianceThresholdLow;   // Below this → 4×4
    float  varianceThresholdMed;   // Below this → 2×2
    float  motionThresholdHigh;    // Above this → force 1×1
    float  edgeThreshold;          // Luminance edge threshold
    uint   frameIndex;
    uint   pad;
};

[[vk::push_constant]] ConstantBuffer<VRSConstants> pc;

Texture2D<float4>  g_PrevColor     : register(t0, space9); // Previous frame color
Texture2D<float2>  g_MotionVectors : register(t1, space9); // Screen-space motion
RWTexture2D<uint>  g_SRIOutput     : register(u0, space9); // Shading Rate Image

SamplerState g_PointClamp : register(s0, space9);

// ─── VRS Rate Encoding ───────────────────────────────────────────────────

static const uint VRS_1x1 = 0;
static const uint VRS_1x2 = 1;
static const uint VRS_2x1 = 2;
static const uint VRS_2x2 = 3;
static const uint VRS_2x4 = 4;
static const uint VRS_4x2 = 5;
static const uint VRS_4x4 = 6;

// ─── Luminance ───────────────────────────────────────────────────────────

float Luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// ─── Tile Analysis ───────────────────────────────────────────────────────

groupshared float gs_Luminance[256]; // 16×16 tile max
groupshared float gs_Motion[256];

[numthreads(16, 16, 1)]
void CSMain(uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex) {
    uint2 tileOrigin = Gid.xy * pc.tileSize;
    uint2 pixel = tileOrigin + GTid.xy;

    // Clamp to screen bounds
    pixel = min(pixel, pc.screenSize - 1);

    float2 uv = (float2(pixel) + 0.5) / float2(pc.screenSize);

    // Sample luminance and motion
    float3 color = g_PrevColor.SampleLevel(g_PointClamp, uv, 0).rgb;
    float lum = Luminance(color);
    float2 motion = g_MotionVectors.SampleLevel(g_PointClamp, uv, 0);
    float motionMag = length(motion) * float(pc.screenSize.x); // In pixels

    gs_Luminance[GI] = lum;
    gs_Motion[GI] = motionMag;

    GroupMemoryBarrierWithGroupSync();

    // Only thread 0 computes the tile's shading rate
    if (GI != 0) return;
    if (Gid.x >= pc.sriSize.x || Gid.y >= pc.sriSize.y) return;

    uint tilePixels = pc.tileSize.x * pc.tileSize.y;
    tilePixels = min(tilePixels, 256u);

    // Compute luminance statistics
    float lumMin = 1e10;
    float lumMax = -1e10;
    float lumSum = 0;
    float lumSqSum = 0;
    float maxMotion = 0;

    for (uint i = 0; i < tilePixels; ++i) {
        float l = gs_Luminance[i];
        lumMin = min(lumMin, l);
        lumMax = max(lumMax, l);
        lumSum += l;
        lumSqSum += l * l;
        maxMotion = max(maxMotion, gs_Motion[i]);
    }

    float lumMean = lumSum / float(tilePixels);
    float lumVariance = (lumSqSum / float(tilePixels)) - (lumMean * lumMean);
    lumVariance = max(lumVariance, 0.0);

    // Edge detection: large luminance range indicates edges
    float lumRange = lumMax - lumMin;
    bool hasEdge = lumRange > pc.edgeThreshold;

    // Determine shading rate
    uint rate;

    if (hasEdge || maxMotion > pc.motionThresholdHigh) {
        // High detail needed: edges or fast motion (avoid blur artifacts)
        rate = VRS_1x1;
    } else if (lumVariance < pc.varianceThresholdLow) {
        // Very smooth region: coarsest shading
        rate = VRS_4x4;
    } else if (lumVariance < pc.varianceThresholdMed) {
        // Moderate variance: medium shading rate
        rate = VRS_2x2;
    } else {
        // Higher variance: full rate
        rate = VRS_1x1;
    }

    // Temporal stability: don't change rate too aggressively
    // (Could read previous SRI and limit rate changes to ±1 step per frame)

    g_SRIOutput[Gid.xy] = rate;
}
