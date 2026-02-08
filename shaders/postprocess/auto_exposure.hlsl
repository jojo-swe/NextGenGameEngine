// ─── Auto-Exposure (Adaptive Luminance) ──────────────────────────────────
// Computes scene average luminance and adapts exposure over time.
// Uses a luminance histogram for robust metering (avoids outliers).
//
// Pipeline:
//   1. Build luminance histogram from HDR scene color
//   2. Compute average luminance from histogram (trimmed mean)
//   3. Smooth adaptation over time (eye adaptation)

#include "../common/math.hlsl"

struct ExposureConstants {
    uint2  screenSize;
    float  minLogLuminance;   // e.g., -10.0
    float  maxLogLuminance;   // e.g., 2.0
    float  logLuminanceRange; // max - min
    float  adaptationSpeed;   // Speed of adaptation (1.0 = instant, 0.05 = slow)
    float  deltaTime;
    float  lowPercentile;     // Trim bottom (e.g., 0.1 = ignore darkest 10%)
    float  highPercentile;    // Trim top (e.g., 0.9 = ignore brightest 10%)
    float  exposureCompensation; // EV offset
    float  minExposure;       // Minimum exposure value
    float  maxExposure;       // Maximum exposure value
};

[[vk::push_constant]] ConstantBuffer<ExposureConstants> pc;

Texture2D<float4>       g_HDRColor       : register(t0, space30);
RWByteAddressBuffer     g_Histogram      : register(u0, space30); // 256 bins
RWByteAddressBuffer     g_ExposureBuffer : register(u1, space30); // [0] = current exposure (float)

#define NUM_BINS 256

// ─── Pass 1: Build Histogram ─────────────────────────────────────────────

groupshared uint gs_Histogram[NUM_BINS];

[numthreads(16, 16, 1)]
void CSBuildHistogram(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex) {
    // Clear shared histogram
    if (GI < NUM_BINS) gs_Histogram[GI] = 0;
    GroupMemoryBarrierWithGroupSync();

    if (DTid.x < pc.screenSize.x && DTid.y < pc.screenSize.y) {
        float3 color = g_HDRColor[DTid.xy].rgb;
        float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));

        if (luminance > 0.001) {
            float logLum = clamp(log2(luminance), pc.minLogLuminance, pc.maxLogLuminance);
            float normalized = (logLum - pc.minLogLuminance) / pc.logLuminanceRange;
            uint bin = uint(normalized * float(NUM_BINS - 1));
            bin = clamp(bin, 0, NUM_BINS - 1);
            InterlockedAdd(gs_Histogram[bin], 1);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Merge shared histogram to global
    if (GI < NUM_BINS) {
        g_Histogram.InterlockedAdd(GI * 4, gs_Histogram[GI]);
    }
}

// ─── Pass 2: Compute Average Luminance + Exposure ────────────────────────

groupshared uint gs_Bins[NUM_BINS];

[numthreads(256, 1, 1)]
void CSComputeExposure(uint GI : SV_GroupIndex) {
    // Load histogram to shared memory
    gs_Bins[GI] = g_Histogram.Load(GI * 4);
    GroupMemoryBarrierWithGroupSync();

    if (GI != 0) return; // Single thread computes final result

    // Total pixel count
    uint totalPixels = 0;
    for (uint i = 0; i < NUM_BINS; ++i) {
        totalPixels += gs_Bins[i];
    }

    if (totalPixels == 0) return;

    // Trimmed mean: exclude bottom/top percentiles
    uint lowCount = uint(float(totalPixels) * pc.lowPercentile);
    uint highCount = uint(float(totalPixels) * pc.highPercentile);

    uint cumulative = 0;
    float weightedSum = 0;
    uint includedPixels = 0;

    for (uint i = 0; i < NUM_BINS; ++i) {
        uint binCount = gs_Bins[i];
        uint prevCumulative = cumulative;
        cumulative += binCount;

        // Skip pixels in the low percentile
        if (cumulative <= lowCount) continue;
        // Stop at high percentile
        if (prevCumulative >= highCount) break;

        // Clamp bin contribution to percentile range
        uint effectiveStart = max(prevCumulative, lowCount);
        uint effectiveEnd = min(cumulative, highCount);
        uint effectiveCount = effectiveEnd - effectiveStart;

        float binLogLum = pc.minLogLuminance + (float(i) + 0.5) / float(NUM_BINS) * pc.logLuminanceRange;
        weightedSum += binLogLum * float(effectiveCount);
        includedPixels += effectiveCount;
    }

    float avgLogLuminance = (includedPixels > 0) ? (weightedSum / float(includedPixels)) : 0.0;
    float avgLuminance = exp2(avgLogLuminance);

    // Compute target exposure: exposure = 1 / (avgLuminance * K)
    // K is a calibration constant (typically 12.5 for average grey = 18%)
    float targetExposure = 1.0 / (avgLuminance * 12.5);
    targetExposure *= exp2(pc.exposureCompensation);
    targetExposure = clamp(targetExposure, pc.minExposure, pc.maxExposure);

    // Eye adaptation: smooth transition
    float currentExposure = asfloat(g_ExposureBuffer.Load(0));
    if (currentExposure <= 0.0) currentExposure = targetExposure; // First frame

    float adaptedExposure = currentExposure + (targetExposure - currentExposure) *
                             (1.0 - exp(-pc.deltaTime * pc.adaptationSpeed));
    adaptedExposure = clamp(adaptedExposure, pc.minExposure, pc.maxExposure);

    g_ExposureBuffer.Store(0, asuint(adaptedExposure));

    // Clear histogram for next frame
    for (uint j = 0; j < NUM_BINS; ++j) {
        g_Histogram.Store(j * 4, 0);
    }
}
