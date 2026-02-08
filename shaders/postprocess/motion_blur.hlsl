// ─── Per-Object Motion Blur ──────────────────────────────────────────────
// Velocity-based motion blur using per-pixel motion vectors.
// Implements a variable-length directional blur along the velocity direction.
//
// Features:
//   - Tile-based max velocity for early-out on static regions
//   - Depth-aware neighbor sampling to prevent background bleeding
//   - Jittered sampling along velocity for temporal stability

#include "../common/math.hlsl"

struct MotionBlurConstants {
    uint2  screenSize;
    uint2  tileSize;        // e.g., 16×16
    float  velocityScale;   // Pixels-per-frame multiplier
    float  maxBlurRadius;   // Max blur in pixels
    uint   sampleCount;     // Samples along velocity (8-16)
    float  depthThreshold;  // Depth rejection threshold
    uint   frameIndex;
    float  exposureTime;    // 0..1 shutter open fraction
    uint   pad0;
    uint   pad1;
};

[[vk::push_constant]] ConstantBuffer<MotionBlurConstants> pc;

Texture2D<float4>  g_SceneColor    : register(t0, space19);
Texture2D<float2>  g_MotionVectors : register(t1, space19);
Texture2D<float>   g_DepthBuffer   : register(t2, space19);

RWTexture2D<float2> g_TileMaxVel   : register(u0, space19); // Per-tile max velocity
RWTexture2D<float4> g_OutputColor  : register(u1, space19);

SamplerState g_LinearClamp : register(s0, space19);
SamplerState g_PointClamp  : register(s1, space19);

// ─── Pass 1: Tile Max Velocity ───────────────────────────────────────────
// Computes the maximum velocity magnitude per tile for early-out.

groupshared float2 gs_Velocity[256]; // 16×16

[numthreads(16, 16, 1)]
void TileMaxCS(uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex) {
    uint2 pixel = Gid.xy * pc.tileSize + GTid.xy;
    pixel = min(pixel, pc.screenSize - 1);

    float2 velocity = g_MotionVectors[pixel] * pc.velocityScale;
    gs_Velocity[GI] = velocity;

    GroupMemoryBarrierWithGroupSync();

    // Parallel reduction to find max velocity in tile
    if (GI == 0) {
        float2 maxVel = float2(0, 0);
        float maxLen = 0;
        uint tilePixels = min(pc.tileSize.x * pc.tileSize.y, 256u);

        for (uint i = 0; i < tilePixels; ++i) {
            float len = length(gs_Velocity[i]);
            if (len > maxLen) {
                maxLen = len;
                maxVel = gs_Velocity[i];
            }
        }

        // Clamp to max blur radius
        float velLen = length(maxVel);
        if (velLen > pc.maxBlurRadius) {
            maxVel *= pc.maxBlurRadius / velLen;
        }

        g_TileMaxVel[Gid.xy] = maxVel;
    }
}

// ─── Pass 2: Motion Blur Gather ──────────────────────────────────────────

float InterleavedGradientNoise(float2 pos) {
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(pos, magic.xy)));
}

[numthreads(8, 8, 1)]
void BlurCS(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.screenSize.x || DTid.y >= pc.screenSize.y) return;

    float2 texelSize = 1.0 / float2(pc.screenSize);
    float2 uv = (float2(DTid.xy) + 0.5) * texelSize;

    // Check tile max velocity for early-out
    uint2 tileCoord = DTid.xy / pc.tileSize;
    float2 tileMaxVel = g_TileMaxVel[tileCoord];
    float tileVelLen = length(tileMaxVel);

    if (tileVelLen < 0.5) {
        // Static tile — no motion blur needed
        g_OutputColor[DTid.xy] = g_SceneColor.SampleLevel(g_PointClamp, uv, 0);
        return;
    }

    // Per-pixel velocity
    float2 velocity = g_MotionVectors[DTid.xy] * pc.velocityScale * pc.exposureTime;
    float velLen = length(velocity);

    if (velLen < 0.5) {
        // Use neighbor velocity (for background behind moving objects)
        velocity = tileMaxVel * pc.exposureTime;
        velLen = length(velocity);
    }

    // Clamp velocity
    if (velLen > pc.maxBlurRadius) {
        velocity *= pc.maxBlurRadius / velLen;
        velLen = pc.maxBlurRadius;
    }

    float centerDepth = g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0);
    float4 centerColor = g_SceneColor.SampleLevel(g_PointClamp, uv, 0);

    // Jitter start position for temporal stability
    float jitter = InterleavedGradientNoise(float2(DTid.xy) + float(pc.frameIndex) * 7.23) - 0.5;

    float4 colorSum = centerColor;
    float weightSum = 1.0;

    float2 stepDir = velocity * texelSize / float(pc.sampleCount);

    for (uint i = 1; i <= pc.sampleCount; ++i) {
        float t = (float(i) + jitter) / float(pc.sampleCount);

        // Sample in both directions along velocity
        float2 offset = stepDir * float(i);

        // Forward sample
        float2 sampleUV_fwd = uv + offset;
        if (sampleUV_fwd.x >= 0 && sampleUV_fwd.x <= 1 && sampleUV_fwd.y >= 0 && sampleUV_fwd.y <= 1) {
            float sampleDepth = g_DepthBuffer.SampleLevel(g_PointClamp, sampleUV_fwd, 0);
            float4 sampleColor = g_SceneColor.SampleLevel(g_LinearClamp, sampleUV_fwd, 0);

            // Depth rejection: don't blur background over foreground
            float depthDiff = abs(centerDepth - sampleDepth);
            float depthWeight = depthDiff < pc.depthThreshold ? 1.0 : 0.2;

            // Velocity coherence: check if sample has similar velocity
            float2 sampleVel = g_MotionVectors.SampleLevel(g_PointClamp, sampleUV_fwd, 0) * pc.velocityScale;
            float coherence = saturate(dot(normalize(velocity + 0.001), normalize(sampleVel + 0.001)));
            float velWeight = lerp(0.3, 1.0, coherence);

            float weight = depthWeight * velWeight;
            colorSum += sampleColor * weight;
            weightSum += weight;
        }

        // Backward sample
        float2 sampleUV_bwd = uv - offset;
        if (sampleUV_bwd.x >= 0 && sampleUV_bwd.x <= 1 && sampleUV_bwd.y >= 0 && sampleUV_bwd.y <= 1) {
            float sampleDepth = g_DepthBuffer.SampleLevel(g_PointClamp, sampleUV_bwd, 0);
            float4 sampleColor = g_SceneColor.SampleLevel(g_LinearClamp, sampleUV_bwd, 0);

            float depthDiff = abs(centerDepth - sampleDepth);
            float depthWeight = depthDiff < pc.depthThreshold ? 1.0 : 0.2;

            float2 sampleVel = g_MotionVectors.SampleLevel(g_PointClamp, sampleUV_bwd, 0) * pc.velocityScale;
            float coherence = saturate(dot(normalize(velocity + 0.001), normalize(sampleVel + 0.001)));
            float velWeight = lerp(0.3, 1.0, coherence);

            float weight = depthWeight * velWeight;
            colorSum += sampleColor * weight;
            weightSum += weight;
        }
    }

    g_OutputColor[DTid.xy] = colorSum / max(weightSum, 0.001);
}
