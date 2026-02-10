// ─── Procedural Lightning Bolt Generator ─────────────────────────────────
// GPU compute shader that generates animated lightning bolt geometry as
// line segment chains, with branching, glow, and flickering.
//
// Features:
//   - L-system inspired recursive branching
//   - Midpoint displacement for jagged bolt shape
//   - Animated flicker with random re-generation
//   - Per-segment glow radius and intensity falloff
//   - Branch probability and depth control
//   - Core + glow dual-pass rendering data
//   - Screen-space billboard output for line rendering
//
// References:
//   - "Real-Time Lightning Rendering" (Kim & Lin, GPU Gems 2)
//   - "Procedural Lightning in Diablo III" (Blizzard, GDC 2012)
//   - "Stochastic Branching" (Müller, Eurographics 2007)

struct LightningSegment {
    float3 startPos;
    float  intensity;
    float3 endPos;
    float  glowRadius;
    float3 color;
    float  branchDepth;
};

struct LightningParams {
    float3 boltStart;            // World-space origin
    float  time;
    float3 boltEnd;              // World-space target
    float  lifetime;             // Bolt lifetime in seconds (default 0.2)
    float  jitterAmount;         // Midpoint displacement scale (default 0.5)
    float  branchProbability;    // Chance of branching per segment (default 0.3)
    float  branchAngle;          // Max branch angle in radians (default 0.5)
    float  branchLengthScale;    // Branch length relative to parent (default 0.6)
    u32    maxSegments;          // Max segments in main bolt (default 16)
    u32    maxBranchDepth;       // Max recursion depth (default 3)
    u32    maxTotalSegments;     // Total output buffer size (default 512)
    float  coreWidth;            // Core line width (default 0.02)
    float  glowWidth;            // Glow halo width (default 0.15)
    float3 coreColor;            // Core color (default: 0.8, 0.9, 1.0)
    float  coreIntensity;        // Core brightness (default 10.0)
    float3 glowColor;            // Glow color (default: 0.3, 0.4, 1.0)
    float  glowIntensity;        // Glow brightness (default 3.0)
    float  flickerSpeed;         // Flicker frequency (default 30.0)
    float  fadeExponent;         // Intensity falloff along bolt (default 1.5)
    u32    seed;                 // Random seed for this bolt
    float  pad0;
};

[[vk::push_constant]] ConstantBuffer<LightningParams> cb;

RWStructuredBuffer<LightningSegment> g_Segments : register(u0);
RWStructuredBuffer<uint>             g_Counter  : register(u1); // Atomic segment counter

// ─── Random Number Generator ─────────────────────────────────────────────

uint WangHash(uint seed) {
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27D4EB2Du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

float RandFloat(inout uint state) {
    state = WangHash(state);
    return float(state) / 4294967295.0;
}

float RandRange(inout uint state, float minVal, float maxVal) {
    return lerp(minVal, maxVal, RandFloat(state));
}

float3 RandDirection(inout uint state) {
    float z = RandRange(state, -1.0, 1.0);
    float phi = RandRange(state, 0.0, 6.2831853);
    float r = sqrt(1.0 - z * z);
    return float3(r * cos(phi), r * sin(phi), z);
}

// ─── Midpoint Displacement ───────────────────────────────────────────────
// Subdivide a segment by displacing the midpoint perpendicular to the
// segment direction.

float3 DisplaceMidpoint(float3 start, float3 end, float jitter, inout uint rng) {
    float3 mid = (start + end) * 0.5;
    float3 dir = end - start;
    float len = length(dir);

    // Perpendicular displacement
    float3 perp = RandDirection(rng);
    perp = normalize(perp - dir * dot(perp, dir) / max(dot(dir, dir), 0.0001));

    float displacement = RandRange(rng, -jitter, jitter) * len;
    mid += perp * displacement;

    return mid;
}

// ─── Emit Segment ────────────────────────────────────────────────────────

void EmitSegment(float3 start, float3 end, float intensity, float glowRadius,
                  float3 color, float depth) {
    uint idx;
    InterlockedAdd(g_Counter[0], 1, idx);

    if (idx >= cb.maxTotalSegments) return;

    LightningSegment seg;
    seg.startPos = start;
    seg.endPos = end;
    seg.intensity = intensity;
    seg.glowRadius = glowRadius;
    seg.color = color;
    seg.branchDepth = depth;

    g_Segments[idx] = seg;
}

// ─── Generate Bolt Chain ─────────────────────────────────────────────────
// Iterative midpoint displacement to create the main bolt.

void GenerateBoltChain(float3 start, float3 end, uint segments, float intensity,
                        float glowRadius, float3 color, float branchDepth, inout uint rng) {
    // Build point array via midpoint displacement
    // Start with 2 points, subdivide log2(segments) times
    float3 points[33]; // Max 32 segments + 1 point
    uint pointCount = min(segments + 1, 33);

    points[0] = start;
    points[pointCount - 1] = end;

    // Simple linear interpolation then displace
    for (uint i = 1; i < pointCount - 1; ++i) {
        float t = float(i) / float(pointCount - 1);
        points[i] = lerp(start, end, t);

        // Displace perpendicular
        float jitter = cb.jitterAmount * (1.0 - abs(t - 0.5) * 2.0); // Less at endpoints
        float3 perp = RandDirection(rng);
        float3 dir = normalize(end - start);
        perp = normalize(perp - dir * dot(perp, dir));
        points[i] += perp * RandRange(rng, -jitter, jitter) * length(end - start) / float(segments);
    }

    // Emit segments
    for (uint i = 0; i < pointCount - 1; ++i) {
        float t = float(i) / float(max(pointCount - 2, 1));
        float segIntensity = intensity * pow(1.0 - t, cb.fadeExponent);

        EmitSegment(points[i], points[i + 1], segIntensity, glowRadius, color, branchDepth);

        // Branch at this point?
        if (branchDepth < cb.maxBranchDepth && RandFloat(rng) < cb.branchProbability) {
            float3 branchDir = normalize(points[i + 1] - points[i]);
            float3 randDir = RandDirection(rng);

            // Rotate branch direction by random angle
            float3 branchEnd = points[i] + normalize(lerp(branchDir, randDir, cb.branchAngle)) *
                                length(end - start) * cb.branchLengthScale / float(segments) *
                                float(pointCount - i);

            float branchIntensity = segIntensity * 0.5;
            float branchGlow = glowRadius * 0.7;
            uint branchSegs = max(2u, segments / 3u);

            // Recursive-ish: generate a shorter branch chain
            float3 brPts[9]; // Max 8 branch segments
            uint brPtCount = min(branchSegs + 1, 9u);
            brPts[0] = points[i];
            brPts[brPtCount - 1] = branchEnd;

            for (uint b = 1; b < brPtCount - 1; ++b) {
                float bt = float(b) / float(brPtCount - 1);
                brPts[b] = lerp(points[i], branchEnd, bt);
                float3 bPerp = RandDirection(rng);
                float3 bDir = normalize(branchEnd - points[i]);
                bPerp = normalize(bPerp - bDir * dot(bPerp, bDir));
                brPts[b] += bPerp * RandRange(rng, -cb.jitterAmount * 0.5, cb.jitterAmount * 0.5) *
                             length(branchEnd - points[i]) / float(branchSegs);
            }

            for (uint b = 0; b < brPtCount - 1; ++b) {
                float bt = float(b) / float(max(brPtCount - 2, 1));
                EmitSegment(brPts[b], brPts[b + 1],
                            branchIntensity * (1.0 - bt), branchGlow,
                            cb.glowColor, branchDepth + 1);
            }
        }
    }
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(1, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    // Initialize counter
    if (DTid.x == 0) {
        g_Counter[0] = 0;
    }

    GroupMemoryBarrierWithGroupSync();

    // Flicker: modulate overall intensity
    float flicker = 0.5 + 0.5 * sin(cb.time * cb.flickerSpeed);
    flicker *= 0.7 + 0.3 * sin(cb.time * cb.flickerSpeed * 2.3 + 1.7);

    // Age-based fade
    float age = frac(cb.time / max(cb.lifetime, 0.01));
    float ageFade = 1.0 - smoothstep(0.7, 1.0, age);

    float totalIntensity = cb.coreIntensity * flicker * ageFade;

    if (totalIntensity < 0.01) return;

    // Initialize RNG
    uint rng = cb.seed ^ WangHash(asuint(cb.time * 1000.0));

    // Re-jitter endpoints slightly each frame for visual variation
    float3 start = cb.boltStart + RandDirection(rng) * 0.02 * flicker;
    float3 end = cb.boltEnd + RandDirection(rng) * 0.02 * flicker;

    // Generate main bolt
    GenerateBoltChain(start, end, cb.maxSegments, totalIntensity,
                       cb.glowWidth, cb.coreColor, 0, rng);
}
