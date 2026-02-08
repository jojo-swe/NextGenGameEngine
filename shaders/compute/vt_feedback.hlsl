// ─── Virtual Texture Feedback Shader ──────────────────────────────────────
// Writes per-tile mip level requests to a feedback buffer during rendering.
// The CPU reads this buffer back to determine which texture pages to stream.
//
// Each screen tile (e.g. 16×16 pixels) writes the finest mip level needed
// by any pixel in that tile. The feedback is conservative — it requests
// the finest mip even if only one pixel needs it.

#include "../common/math.hlsl"

struct VTFeedbackConstants {
    float4x4 viewProj;
    uint2    screenSize;
    uint2    tileSize;       // e.g., 16×16
    uint     maxFeedbackEntries;
    float    mipBias;        // Additional mip bias (positive = coarser)
    uint     pad0;
    uint     pad1;
};

[[vk::push_constant]] ConstantBuffer<VTFeedbackConstants> pc;

// ─── Inputs ──────────────────────────────────────────────────────────────

Texture2D<float>  g_DepthBuffer    : register(t0, space26);
Texture2D<uint>   g_MaterialIdTex  : register(t1, space26); // Per-pixel material/texture ID

struct MaterialInfo {
    uint textureId;
    uint baseWidth;
    uint baseHeight;
    uint mipCount;
    float tilingU;
    float tilingV;
    uint pad0;
    uint pad1;
};

StructuredBuffer<MaterialInfo> g_Materials : register(t2, space26);

// ─── Output ──────────────────────────────────────────────────────────────

struct FeedbackEntry {
    uint textureId;
    uint requestedMip;
};

RWStructuredBuffer<FeedbackEntry> g_FeedbackBuffer : register(u0, space26);
RWByteAddressBuffer               g_FeedbackCount  : register(u1, space26);

// ─── Mip Level Computation ───────────────────────────────────────────────
// Estimates the required mip level based on the UV derivatives.
// For screen-space tiles, we estimate based on depth-based footprint.

float ComputeRequiredMip(float depth, float texWidth, float texHeight,
                          float tilingU, float tilingV) {
    // Approximate screen-space texel density from depth
    // Closer objects need finer mips, farther objects need coarser
    float screenFootprint = depth * 2.0; // Simplified; real impl uses projection

    // Texels per screen pixel
    float texelsPerPixelU = texWidth * tilingU / float(pc.screenSize.x);
    float texelsPerPixelV = texHeight * tilingV / float(pc.screenSize.y);
    float texelsPerPixel = max(texelsPerPixelU, texelsPerPixelV) / max(screenFootprint, 0.001);

    // Mip level = log2(texels per pixel)
    float mip = log2(max(texelsPerPixel, 1.0)) + pc.mipBias;
    return max(mip, 0.0);
}

// ─── Main Feedback Kernel ────────────────────────────────────────────────
// One thread per screen tile. Scans all pixels in the tile to find
// the finest required mip level.

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    uint2 tileCount = (pc.screenSize + pc.tileSize - 1) / pc.tileSize;
    if (DTid.x >= tileCount.x || DTid.y >= tileCount.y) return;

    uint2 tileOrigin = DTid.xy * pc.tileSize;

    // Track per-texture finest mip request in this tile
    // Simple approach: one feedback entry per tile per unique texture
    uint bestTextureId = 0xFFFFFFFF;
    float finestMip = 999.0;

    for (uint ty = 0; ty < pc.tileSize.y; ++ty) {
        for (uint tx = 0; tx < pc.tileSize.x; ++tx) {
            uint2 pixel = tileOrigin + uint2(tx, ty);
            if (pixel.x >= pc.screenSize.x || pixel.y >= pc.screenSize.y) continue;

            float depth = g_DepthBuffer[pixel];
            if (depth <= 0.0) continue;

            uint materialId = g_MaterialIdTex[pixel];
            if (materialId == 0xFFFFFFFF) continue;

            MaterialInfo mat = g_Materials[materialId];
            if (mat.textureId == 0xFFFFFFFF) continue;

            float requiredMip = ComputeRequiredMip(depth,
                float(mat.baseWidth), float(mat.baseHeight),
                mat.tilingU, mat.tilingV);

            if (requiredMip < finestMip || bestTextureId == 0xFFFFFFFF) {
                finestMip = requiredMip;
                bestTextureId = mat.textureId;
            }
        }
    }

    if (bestTextureId == 0xFFFFFFFF) return;

    // Write feedback entry
    uint entryIdx;
    g_FeedbackCount.InterlockedAdd(0, 1, entryIdx);
    if (entryIdx >= pc.maxFeedbackEntries) return;

    FeedbackEntry entry;
    entry.textureId = bestTextureId;
    entry.requestedMip = uint(finestMip);
    g_FeedbackBuffer[entryIdx] = entry;
}

// ─── Reset Counter ───────────────────────────────────────────────────────

[numthreads(1, 1, 1)]
void CSResetFeedback(uint3 DTid : SV_DispatchThreadID) {
    g_FeedbackCount.Store(0, 0);
}
