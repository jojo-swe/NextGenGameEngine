// ─── Procedural Glitch / Data Corruption Shader ──────────────────────────
// Screen-space post-process that simulates digital signal corruption,
// VHS artifacts, and data glitch effects for stylized or horror aesthetics.
//
// Features:
//   - Block displacement (random rectangular shifts)
//   - Scanline noise and jitter
//   - Color channel separation (RGB split)
//   - Digital block corruption (mosaic artifacts)
//   - VHS tracking lines
//   - Bit-depth reduction / posterization
//   - Static / white noise overlay
//   - Horizontal tear lines
//   - Chromatic aberration (per-channel UV offset)
//   - Signal dropout (black bars)
//   - Configurable intensity, speed, and randomness
//
// References:
//   - "Glitch Art" (Rosa Menkman)
//   - "VHS Shader Effects" (Keijiro Takahashi)
//   - "Databending as Art" (Benjamin Berg)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor : register(t0);

SamplerState g_LinearClamp : register(s0);

RWTexture2D<float4> g_Output : register(u0);

struct GlitchConstants {
    float2   resolution;
    float2   invResolution;
    float    time;

    float    intensity;           // Overall glitch intensity 0-1 (default 0.5)
    float    speed;               // Animation speed multiplier (default 1.0)

    // Block displacement
    float    blockShiftStrength;  // Block horizontal shift (default 0.3)
    float    blockSize;           // Block height in pixels (default 16.0)
    float    blockFrequency;      // How often blocks glitch (default 0.3)

    // Channel separation
    float    rgbSplitAmount;      // RGB channel offset in pixels (default 3.0)
    float    rgbSplitAngle;       // Angle of RGB split (default 0.0)

    // Scanline noise
    float    scanlineJitter;      // Horizontal scanline jitter (default 0.2)
    float    scanlineNoise;       // Scanline noise intensity (default 0.1)

    // VHS artifacts
    float    vhsTracking;         // VHS tracking line intensity (default 0.0)
    float    vhsWobble;           // VHS horizontal wobble (default 0.0)

    // Digital corruption
    float    corruptionRate;      // Block corruption probability (default 0.05)
    float    posterize;           // Bit-depth reduction levels (default 0.0, disabled)

    // Static noise
    float    staticIntensity;     // White noise overlay (default 0.1)

    // Signal dropout
    float    dropoutRate;         // Black bar probability (default 0.02)
    float    dropoutHeight;       // Dropout bar height (default 4.0)

    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<GlitchConstants> cb;

// ─── Noise Helpers ───────────────────────────────────────────────────────

float Hash11(float p) {
    p = frac(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return frac(p);
}

float Hash21(float2 p) {
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float TemporalHash(float seed) {
    return Hash11(seed + floor(cb.time * cb.speed * 8.0));
}

float TemporalHash2(float seed) {
    return Hash11(seed + floor(cb.time * cb.speed * 24.0));
}

// ─── Block Displacement ──────────────────────────────────────────────────

float2 BlockDisplacement(float2 uv) {
    if (cb.blockShiftStrength <= 0.0 || cb.intensity <= 0.0) return uv;

    float blockY = floor(uv.y * cb.resolution.y / cb.blockSize);
    float trigger = TemporalHash(blockY * 7.31);

    if (trigger > cb.blockFrequency) return uv;

    float shift = (TemporalHash(blockY * 13.17) - 0.5) * 2.0;
    shift *= cb.blockShiftStrength * cb.intensity;

    return float2(uv.x + shift, uv.y);
}

// ─── Scanline Jitter ─────────────────────────────────────────────────────

float2 ScanlineJitter(float2 uv) {
    if (cb.scanlineJitter <= 0.0) return uv;

    float line = floor(uv.y * cb.resolution.y);
    float jitter = (TemporalHash2(line * 3.71) - 0.5) * 2.0;
    jitter *= cb.scanlineJitter * cb.intensity * cb.invResolution.x * 20.0;

    return float2(uv.x + jitter, uv.y);
}

// ─── VHS Wobble + Tracking ───────────────────────────────────────────────

float2 VHSDistort(float2 uv) {
    float2 result = uv;

    // Horizontal wobble
    if (cb.vhsWobble > 0.0) {
        float wobble = sin(uv.y * 50.0 + cb.time * cb.speed * 3.0) * cb.vhsWobble * 0.005;
        wobble += sin(uv.y * 130.0 + cb.time * cb.speed * 7.0) * cb.vhsWobble * 0.002;
        result.x += wobble * cb.intensity;
    }

    return result;
}

float VHSTracking(float2 uv) {
    if (cb.vhsTracking <= 0.0) return 0.0;

    // Scrolling tracking line
    float trackY = frac(cb.time * cb.speed * 0.1);
    float dist = abs(uv.y - trackY);
    float tracking = smoothstep(0.06, 0.0, dist);
    tracking *= cb.vhsTracking * cb.intensity;

    return tracking;
}

// ─── RGB Channel Separation ──────────────────────────────────────────────

float3 RGBSplit(float2 uv) {
    if (cb.rgbSplitAmount <= 0.0) return g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;

    float angle = cb.rgbSplitAngle;
    float2 dir = float2(cos(angle), sin(angle));
    float2 offset = dir * cb.rgbSplitAmount * cb.invResolution * cb.intensity;

    float r = g_SceneColor.SampleLevel(g_LinearClamp, uv + offset, 0).r;
    float g = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).g;
    float b = g_SceneColor.SampleLevel(g_LinearClamp, uv - offset, 0).b;

    return float3(r, g, b);
}

// ─── Digital Block Corruption ────────────────────────────────────────────

float3 BlockCorruption(float3 color, float2 uv) {
    if (cb.corruptionRate <= 0.0) return color;

    float2 blockCoord = floor(uv * cb.resolution / cb.blockSize);
    float trigger = TemporalHash(dot(blockCoord, float2(17.31, 43.77)));

    if (trigger > cb.corruptionRate * cb.intensity) return color;

    // Corrupt: shift color, wrong block, or solid color
    float corruptType = TemporalHash(dot(blockCoord, float2(7.13, 29.53)));

    if (corruptType < 0.33) {
        // Solid random color block
        return float3(
            TemporalHash(blockCoord.x * 5.0),
            TemporalHash(blockCoord.y * 11.0),
            TemporalHash(dot(blockCoord, float2(3.0, 7.0)))
        );
    } else if (corruptType < 0.66) {
        // Invert colors
        return 1.0 - color;
    } else {
        // Channel swap
        return color.brg;
    }
}

// ─── Scanline Noise ──────────────────────────────────────────────────────

float3 ApplyScanlineNoise(float3 color, float2 uv) {
    if (cb.scanlineNoise <= 0.0) return color;

    float line = uv.y * cb.resolution.y;
    float scanline = sin(line * 3.14159) * 0.5 + 0.5;
    scanline = lerp(1.0, scanline, cb.scanlineNoise * cb.intensity * 0.5);

    return color * scanline;
}

// ─── Static Noise ────────────────────────────────────────────────────────

float3 ApplyStaticNoise(float3 color, float2 uv) {
    if (cb.staticIntensity <= 0.0) return color;

    float noise = Hash21(uv * cb.resolution + frac(cb.time * 1000.0));
    noise = (noise - 0.5) * cb.staticIntensity * cb.intensity;

    return color + noise;
}

// ─── Signal Dropout ──────────────────────────────────────────────────────

float3 ApplyDropout(float3 color, float2 uv) {
    if (cb.dropoutRate <= 0.0) return color;

    float lineGroup = floor(uv.y * cb.resolution.y / cb.dropoutHeight);
    float trigger = TemporalHash(lineGroup * 23.17);

    if (trigger < cb.dropoutRate * cb.intensity) {
        return float3(0, 0, 0);
    }

    return color;
}

// ─── Posterize (Bit Depth Reduction) ─────────────────────────────────────

float3 ApplyPosterize(float3 color) {
    if (cb.posterize <= 0.0) return color;

    float levels = max(cb.posterize, 2.0);
    return floor(color * levels) / (levels - 1.0);
}

// ─── Horizontal Tear Lines ───────────────────────────────────────────────

float2 TearLine(float2 uv) {
    float tearY = frac(TemporalHash(42.0) + cb.time * cb.speed * 0.3);
    float dist = abs(uv.y - tearY);

    if (dist < 0.005 * cb.intensity) {
        float shift = (TemporalHash(tearY * 100.0) - 0.5) * 0.1 * cb.intensity;
        return float2(uv.x + shift, uv.y);
    }

    return uv;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    // ── UV distortion pipeline ───────────────────────────────────────
    uv = BlockDisplacement(uv);
    uv = ScanlineJitter(uv);
    uv = VHSDistort(uv);
    uv = TearLine(uv);

    // Clamp UV after distortion
    uv = saturate(uv);

    // ── Color fetch with RGB split ───────────────────────────────────
    float3 color = RGBSplit(uv);

    // ── Color corruption pipeline ────────────────────────────────────
    color = BlockCorruption(color, uv);
    color = ApplyScanlineNoise(color, uv);
    color = ApplyStaticNoise(color, uv);
    color = ApplyDropout(color, uv);
    color = ApplyPosterize(color);

    // ── VHS tracking overlay ─────────────────────────────────────────
    float tracking = VHSTracking(uv);
    color = lerp(color, float3(1, 1, 1), tracking);

    color = saturate(color);

    g_Output[DTid.xy] = float4(color, 1.0);
}
