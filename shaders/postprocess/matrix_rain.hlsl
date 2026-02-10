// ─── Procedural Matrix / Digital Rain Effect Shader ──────────────────────
// Screen-space post-process for "The Matrix"-style digital rain with
// cascading glyphs, column-based animation, and depth-aware overlay.
//
// Features:
//   - Column-based cascading glyph streams
//   - Procedural glyph rendering (pseudo-katakana grid)
//   - Per-column random speed, brightness, and glyph cycling
//   - Head glow (bright white/green leading character)
//   - Tail fade (exponential decay down the column)
//   - Depth-aware: rain only on surfaces or full-screen overlay
//   - Color tint (classic green, configurable)
//   - Glyph cycling animation (characters change over time)
//   - Bloom-compatible HDR output
//
// References:
//   - "The Matrix" digital rain (Warner Bros, 1999)
//   - "Matrix Code NFI Font" recreation studies
//   - "Shadertoy Matrix Rain" (various authors)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor : register(t0);
Texture2D<float>  g_SceneDepth : register(t1);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct MatrixRainConstants {
    float2 resolution;
    float2 invResolution;
    float  time;
    float  columnWidth;       // Pixels per column (default 12.0)
    float  glyphHeight;       // Pixels per glyph row (default 16.0)
    float  speedMin;          // Min column fall speed (default 0.5)
    float  speedMax;          // Max column fall speed (default 2.0)
    float  headBrightness;    // Leading char intensity (default 5.0)
    float  tailLength;        // Number of glyphs in trail (default 20.0)
    float  fadeExponent;      // Tail decay exponent (default 2.5)
    float3 rainColor;         // Tint (default: 0.0, 1.0, 0.3)
    float  glyphCycleSpeed;   // How fast glyphs change (default 8.0)
    float  opacity;           // Blend opacity (default 0.8)
    float  depthMask;         // 0=full screen, 1=surfaces only (default 0.0)
    float  bloomIntensity;    // HDR bloom on head glyph (default 3.0)
    u32    glyphSetSize;      // Number of unique glyphs (default 48)
    float  columnDensity;     // Fraction of active columns (default 0.7)
    float  flickerAmount;     // Random brightness flicker (default 0.2)
    float  pad0;
};

[[vk::push_constant]] ConstantBuffer<MatrixRainConstants> cb;

// ─── Hash Functions ──────────────────────────────────────────────────────

float Hash11(float p) {
    p = frac(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return frac(p);
}

float Hash21(float2 p) {
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

// ─── Procedural Glyph Rendering ─────────────────────────────────────────
// Renders a pseudo-glyph using a procedural pattern based on a glyph ID.
// Each glyph is a 5x7 pixel pattern encoded procedurally.

float RenderGlyph(float2 localUV, u32 glyphId) {
    // Map UV to a 5x7 grid
    int2 cell = int2(localUV * float2(5, 7));
    if (cell.x < 0 || cell.x >= 5 || cell.y < 0 || cell.y >= 7) return 0.0;

    // Generate pseudo-random pattern for this glyph
    float seed = float(glyphId) * 7.31;
    float cellHash = Hash21(float2(float(cell.x) + seed, float(cell.y) + seed * 3.7));

    // Create structured patterns (not pure noise)
    // Different glyph IDs produce different stroke patterns
    float pattern = 0.0;

    // Vertical strokes
    float vStroke = step(0.6, Hash11(seed + float(cell.x) * 13.7));
    if (vStroke > 0.5 && cell.y >= 1 && cell.y <= 5) pattern = 1.0;

    // Horizontal strokes
    float hStroke = step(0.5, Hash11(seed + float(cell.y) * 17.3));
    if (hStroke > 0.5 && cell.x >= 1 && cell.x <= 3) pattern = max(pattern, 0.8);

    // Diagonal elements
    if (abs(cell.x - cell.y) <= 1 && Hash11(seed * 2.1) > 0.5) pattern = max(pattern, 0.6);

    // Dots and accents
    if (cellHash > 0.7) pattern = max(pattern, 0.4);

    return pattern;
}

// ─── Column Properties ───────────────────────────────────────────────────

struct ColumnInfo {
    float speed;
    float offset;       // Random start offset
    float brightness;
    bool  active;
};

ColumnInfo GetColumnInfo(u32 columnIndex) {
    ColumnInfo info;
    float colSeed = float(columnIndex);

    info.speed = lerp(cb.speedMin, cb.speedMax, Hash11(colSeed * 0.7123));
    info.offset = Hash11(colSeed * 1.3117) * 100.0;
    info.brightness = 0.5 + 0.5 * Hash11(colSeed * 2.5731);
    info.active = Hash11(colSeed * 3.9173) < cb.columnDensity;

    return info;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;
    float2 pixelPos = float2(DTid.xy);

    float4 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);
    float depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);

    // Depth mask: optionally only show rain on surfaces
    float depthMask = 1.0;
    if (cb.depthMask > 0.0) {
        depthMask = depth < 1.0 ? 1.0 : 0.0;
        depthMask = lerp(1.0, depthMask, cb.depthMask);
    }

    // Determine which column and row this pixel belongs to
    u32 columnIndex = u32(pixelPos.x / cb.columnWidth);
    float rowPos = pixelPos.y / cb.glyphHeight;

    // Get column properties
    ColumnInfo col = GetColumnInfo(columnIndex);

    float rainValue = 0.0;
    float headValue = 0.0;

    if (col.active) {
        // Column scroll position (wraps around screen height)
        float totalRows = cb.resolution.y / cb.glyphHeight;
        float scrollPos = cb.time * col.speed + col.offset;
        float headRow = fmod(scrollPos, totalRows + cb.tailLength);

        // Distance from head (in rows)
        float distFromHead = headRow - rowPos;

        // Handle wrapping
        if (distFromHead < -totalRows * 0.5) distFromHead += totalRows + cb.tailLength;

        // Check if pixel is in the active trail
        if (distFromHead >= 0.0 && distFromHead <= cb.tailLength) {
            // Tail fade: exponential decay
            float tailT = distFromHead / cb.tailLength;
            float fade = pow(1.0 - tailT, cb.fadeExponent);

            // Determine which glyph to show
            u32 rowIndex = u32(rowPos);
            float glyphTime = cb.time * cb.glyphCycleSpeed;
            u32 glyphId = u32(Hash21(float2(float(columnIndex), float(rowIndex))) *
                              float(cb.glyphSetSize) + glyphTime) % cb.glyphSetSize;

            // Glyph cycling: characters near the head change faster
            if (distFromHead < 3.0) {
                glyphId = u32(Hash21(float2(float(columnIndex), glyphTime * 2.0)) *
                              float(cb.glyphSetSize)) % cb.glyphSetSize;
            }

            // Local UV within the glyph cell
            float2 cellUV;
            cellUV.x = frac(pixelPos.x / cb.columnWidth);
            cellUV.y = frac(pixelPos.y / cb.glyphHeight);

            // Render glyph
            float glyph = RenderGlyph(cellUV, glyphId);

            // Brightness with flicker
            float flicker = 1.0 - cb.flickerAmount * (0.5 + 0.5 *
                            sin(cb.time * 30.0 + float(columnIndex) * 7.0 + float(rowIndex) * 3.0));

            rainValue = glyph * fade * col.brightness * flicker;

            // Head glow (first 1-2 characters)
            if (distFromHead < 1.5) {
                float headFade = 1.0 - distFromHead / 1.5;
                headValue = glyph * headFade * cb.headBrightness;
            }
        }
    }

    // Color the rain
    float3 rainCol = cb.rainColor * rainValue;

    // Head is brighter white-green
    float3 headCol = lerp(cb.rainColor, float3(1, 1, 1), 0.5) * headValue * cb.bloomIntensity;

    // Composite
    float totalRain = saturate(rainValue + headValue * 0.3);
    float3 finalColor = lerp(sceneColor.rgb,
                              sceneColor.rgb * 0.3 + rainCol + headCol,
                              totalRain * cb.opacity * depthMask);

    // Add subtle glow around bright glyphs
    finalColor += headCol * 0.2 * depthMask;

    g_Output[DTid.xy] = float4(finalColor, 1.0);
}
