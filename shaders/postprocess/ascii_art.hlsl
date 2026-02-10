// ─── Procedural ASCII / Text Art Rendering Shader ────────────────────────
// Screen-space post-process that converts the scene into ASCII character
// art by mapping luminance to character glyphs rendered procedurally.
//
// Features:
//   - Procedural character rendering (no font texture required)
//   - Luminance-to-character mapping (space -> . : - = + * # @ █)
//   - Configurable cell size (character resolution)
//   - Color mode: monochrome green, original color, or custom palette
//   - Background fill (black or scene-tinted)
//   - Edge-enhanced character selection
//   - Configurable character set density
//   - Animated cursor blink effect
//   - Depth-aware detail (more detail up close)
//
// References:
//   - "ASCII Rendering in Dwarf Fortress" (Bay 12 Games)
//   - "Text Mode Demo Effects" (Pouet, 2003)
//   - "Procedural Glyph Rendering" (Shadertoy community)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor : register(t0);
Texture2D<float>  g_SceneDepth : register(t1);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct ASCIIArtConstants {
    float2   resolution;
    float2   invResolution;
    float    time;
    u32      colorMode;            // 0=monochrome green, 1=original color, 2=amber, 3=white
    float    cellSize;             // Character cell size in pixels (default 8.0)
    float    charBrightness;       // Character brightness (default 1.0)
    float    bgBrightness;         // Background brightness (default 0.05)
    float    edgeWeight;           // Edge detection influence on char selection (default 0.3)
    float    depthDetailScale;     // More detail for closer objects (default 0.0)
    float    cursorBlinkSpeed;     // Animated cursor blink (default 0.0, disabled)
    float3   monoColor;            // Monochrome tint (default: 0.2, 1.0, 0.3)
    float    contrast;             // Luminance contrast (default 1.2)
    float    pad0;
    float    pad1;
};

[[vk::push_constant]] ConstantBuffer<ASCIIArtConstants> cb;

// ─── Procedural Character Bitmaps ────────────────────────────────────────
// Each character is encoded as a 5x7 bitmap packed into a u32+u8.
// We use simplified 4x5 bitmaps packed into 20 bits of a u32.

// Characters ordered by visual density (dark to bright):
// Space, ., :, -, =, +, *, #, @, █
// Index 0 = darkest (space), 9 = brightest (block)

float GetCharPixel(u32 charIndex, float2 localUV) {
    // 4x5 character bitmaps (20 bits each)
    // Bit layout: row0[3:0], row1[3:0], row2[3:0], row3[3:0], row4[3:0]

    // Simplified procedural approach: use math patterns instead of bitmaps
    int2 p = int2(localUV * float2(4.0, 5.0));
    p = clamp(p, int2(0, 0), int2(3, 4));

    float pixel = 0.0;

    if (charIndex == 0) {
        // Space
        pixel = 0.0;
    } else if (charIndex == 1) {
        // . (dot at bottom center)
        pixel = (p.x == 1 || p.x == 2) && p.y == 4 ? 1.0 : 0.0;
    } else if (charIndex == 2) {
        // : (two dots)
        pixel = ((p.x == 1 || p.x == 2) && (p.y == 1 || p.y == 3)) ? 1.0 : 0.0;
    } else if (charIndex == 3) {
        // - (horizontal line)
        pixel = (p.y == 2) ? 1.0 : 0.0;
    } else if (charIndex == 4) {
        // = (double horizontal)
        pixel = (p.y == 1 || p.y == 3) ? 1.0 : 0.0;
    } else if (charIndex == 5) {
        // + (cross)
        pixel = (p.x == 1 || p.x == 2) && p.y == 2 ? 1.0 :
                (p.y == 1 || p.y == 2 || p.y == 3) && (p.x == 1 || p.x == 2) ? 0.5 : 0.0;
        if ((p.y == 2) || ((p.x == 1 || p.x == 2) && (p.y >= 1 && p.y <= 3))) pixel = 1.0;
    } else if (charIndex == 6) {
        // * (star pattern)
        pixel = (p.y == 0 || p.y == 4) && (p.x == 1 || p.x == 2) ? 1.0 :
                (p.y == 2) ? 1.0 :
                (p.y == 1 || p.y == 3) && (p.x == 0 || p.x == 3) ? 1.0 : 0.0;
    } else if (charIndex == 7) {
        // # (hash/grid)
        pixel = ((p.x == 1 || p.x == 3) || (p.y == 1 || p.y == 3)) ? 1.0 : 0.0;
    } else if (charIndex == 8) {
        // @ (filled circle-ish)
        float2 center = float2(1.5, 2.0);
        float dist = length(float2(p) - center);
        pixel = dist < 2.0 ? 1.0 : 0.0;
    } else {
        // █ (full block)
        pixel = 1.0;
    }

    return pixel;
}

// ─── Luminance to Character Index ────────────────────────────────────────

u32 LuminanceToChar(float lum) {
    lum = saturate(lum);
    u32 idx = u32(lum * 9.0 + 0.5);
    return min(idx, 9u);
}

// ─── Simple Edge Detection ───────────────────────────────────────────────

float EdgeDetect(float2 uv) {
    float2 texel = cb.invResolution;

    float c = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb, float3(0.333, 0.333, 0.333));
    float l = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(-texel.x, 0), 0).rgb, float3(0.333, 0.333, 0.333));
    float r = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(texel.x, 0), 0).rgb, float3(0.333, 0.333, 0.333));
    float t = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(0, -texel.y), 0).rgb, float3(0.333, 0.333, 0.333));
    float b = dot(g_SceneColor.SampleLevel(g_LinearClamp, uv + float2(0, texel.y), 0).rgb, float3(0.333, 0.333, 0.333));

    return abs(4.0 * c - l - r - t - b);
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    // ── Compute cell coordinates ─────────────────────────────────────
    float cellSize = cb.cellSize;

    // Depth-based detail
    if (cb.depthDetailScale > 0.0) {
        float depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
        cellSize = max(4.0, cellSize * (1.0 + depth * cb.depthDetailScale));
    }

    float2 cellUV = floor(float2(DTid.xy) / cellSize) * cellSize;
    float2 cellCenter = (cellUV + cellSize * 0.5) * cb.invResolution;
    float2 localUV = frac(float2(DTid.xy) / cellSize);

    // ── Sample scene at cell center ──────────────────────────────────
    float4 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, cellCenter, 0);
    float luminance = dot(sceneColor.rgb, float3(0.2126, 0.7152, 0.0722));

    // Contrast adjustment
    luminance = saturate((luminance - 0.5) * cb.contrast + 0.5);

    // Edge enhancement
    if (cb.edgeWeight > 0.0) {
        float edge = EdgeDetect(cellCenter);
        luminance = saturate(luminance + edge * cb.edgeWeight);
    }

    // ── Select character ─────────────────────────────────────────────
    u32 charIdx = LuminanceToChar(luminance);

    // ── Render character pixel ───────────────────────────────────────
    float charPixel = GetCharPixel(charIdx, localUV);

    // ── Apply color mode ─────────────────────────────────────────────
    float3 charColor;

    if (cb.colorMode == 0) {
        // Monochrome (green terminal)
        charColor = cb.monoColor;
    } else if (cb.colorMode == 1) {
        // Original scene color
        charColor = sceneColor.rgb;
    } else if (cb.colorMode == 2) {
        // Amber terminal
        charColor = float3(1.0, 0.7, 0.2);
    } else {
        // White
        charColor = float3(1.0, 1.0, 1.0);
    }

    // ── Compose output ───────────────────────────────────────────────
    float3 bgColor = charColor * cb.bgBrightness;
    float3 fgColor = charColor * cb.charBrightness;

    float3 result = lerp(bgColor, fgColor, charPixel);

    // ── Cursor blink effect ──────────────────────────────────────────
    if (cb.cursorBlinkSpeed > 0.0) {
        float2 cursorCell = floor(float2(cb.resolution) * 0.5 / cellSize);
        float2 thisCell = floor(float2(DTid.xy) / cellSize);

        if (all(thisCell == cursorCell)) {
            float blink = step(0.5, frac(cb.time * cb.cursorBlinkSpeed));
            if (blink > 0.5 && localUV.y > 0.8) {
                result = fgColor;
            }
        }
    }

    g_Output[DTid.xy] = float4(result, 1.0);
}
