// ─── Depth of Field (Physically-Based Bokeh) ─────────────────────────────
// Two-pass circular bokeh DOF:
//   1. CoC (Circle of Confusion) computation from depth + camera params
//   2. Gather: sample neighborhood weighted by CoC for bokeh shape
//
// Uses a disk kernel with variable radius based on CoC.
// Separates near and far field to avoid bleeding artifacts.

#include "../common/math.hlsl"

struct DOFConstants {
    float4x4 invProj;
    uint2    screenSize;
    float    focusDistance;  // meters
    float    aperture;      // f-stop (e.g., 2.8)
    float    focalLength;   // mm (e.g., 50)
    float    sensorHeight;  // mm (e.g., 24 for full-frame)
    float    nearPlane;
    float    farPlane;
    float    maxCoC;        // Maximum CoC radius in pixels
    uint     sampleCount;   // Bokeh kernel samples
    uint     pad0;
    uint     pad1;
};

[[vk::push_constant]] ConstantBuffer<DOFConstants> pc;

Texture2D<float4>   g_SceneColor  : register(t0, space11);
Texture2D<float>    g_DepthBuffer : register(t1, space11);
RWTexture2D<float4> g_OutputColor : register(u0, space11);
RWTexture2D<float>  g_CoCBuffer   : register(u1, space11);

SamplerState g_LinearClamp : register(s0, space11);

// ─── Circle of Confusion ─────────────────────────────────────────────────

float LinearizeDepth(float rawDepth) {
    // Reverse-Z infinite far plane
    float z = pc.nearPlane / rawDepth;
    return z;
}

float ComputeCoC(float depth) {
    // Thin lens model:
    // CoC = |A * f * (S - D) / (D * (S - f))| * sensorToPixel
    // where A = aperture diameter, f = focal length, S = focus distance, D = scene depth
    
    float f = pc.focalLength * 0.001;       // Convert mm to meters
    float A = f / pc.aperture;               // Aperture diameter
    float S = pc.focusDistance;
    float D = max(depth, pc.nearPlane);
    
    float coc = A * f * (S - D) / (D * (S - f));
    
    // Convert from meters to pixels
    float sensorToPixel = float(pc.screenSize.y) / (pc.sensorHeight * 0.001);
    coc *= sensorToPixel;
    
    // Clamp and sign: positive = far field, negative = near field
    return clamp(coc, -pc.maxCoC, pc.maxCoC);
}

// ─── Pass 1: Compute CoC per pixel ───────────────────────────────────────

[numthreads(8, 8, 1)]
void CoCCS(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.screenSize.x || DTid.y >= pc.screenSize.y) return;
    
    float rawDepth = g_DepthBuffer[DTid.xy];
    float linearDepth = LinearizeDepth(rawDepth);
    float coc = ComputeCoC(linearDepth);
    
    g_CoCBuffer[DTid.xy] = coc;
}

// ─── Bokeh Kernel ────────────────────────────────────────────────────────
// Poisson disk samples for natural-looking bokeh

static const float2 g_PoissonDisk[32] = {
    float2(-0.613, 0.616), float2(0.170, -0.040), float2(-0.299, 0.070),
    float2(0.463, -0.284), float2(-0.421, 0.494), float2(0.816, -0.337),
    float2(-0.775, -0.140), float2(0.457, 0.466), float2(-0.162, -0.546),
    float2(0.156, 0.842), float2(-0.668, -0.521), float2(0.653, 0.170),
    float2(-0.052, 0.346), float2(0.340, -0.716), float2(-0.416, -0.262),
    float2(0.821, 0.528), float2(-0.900, 0.255), float2(0.111, -0.906),
    float2(-0.563, 0.842), float2(0.575, -0.577), float2(-0.193, 0.894),
    float2(0.930, -0.068), float2(-0.770, 0.618), float2(0.277, 0.277),
    float2(-0.335, -0.780), float2(0.698, 0.791), float2(-0.953, -0.352),
    float2(0.058, 0.577), float2(-0.474, 0.225), float2(0.452, -0.052),
    float2(-0.158, -0.284), float2(0.818, -0.700)
};

// ─── Pass 2: Bokeh Gather ────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void GatherCS(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.screenSize.x || DTid.y >= pc.screenSize.y) return;
    
    float2 texelSize = 1.0 / float2(pc.screenSize);
    float2 uv = (float2(DTid.xy) + 0.5) * texelSize;
    
    float centerCoC = g_CoCBuffer[DTid.xy];
    float absCoC = abs(centerCoC);
    
    // If CoC is tiny, skip blur
    if (absCoC < 0.5) {
        g_OutputColor[DTid.xy] = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);
        return;
    }
    
    float4 colorSum = float4(0, 0, 0, 0);
    float weightSum = 0;
    
    uint samples = min(pc.sampleCount, 32u);
    
    for (uint i = 0; i < samples; ++i) {
        float2 offset = g_PoissonDisk[i] * absCoC * texelSize;
        float2 sampleUV = uv + offset;
        
        // Clamp to screen bounds
        sampleUV = clamp(sampleUV, texelSize, 1.0 - texelSize);
        
        float4 sampleColor = g_SceneColor.SampleLevel(g_LinearClamp, sampleUV, 0);
        
        // Get CoC at sample position
        uint2 samplePixel = uint2(sampleUV * float2(pc.screenSize));
        samplePixel = clamp(samplePixel, uint2(0, 0), pc.screenSize - 1);
        float sampleCoC = g_CoCBuffer[samplePixel];
        float absSampleCoC = abs(sampleCoC);
        
        // Weight: sample contributes if its CoC is large enough to reach this pixel
        float sampleDist = length(g_PoissonDisk[i]) * absCoC;
        
        // Near field: always bleeds over (negative CoC)
        // Far field: only contributes if sample's CoC covers this pixel
        float weight = 1.0;
        
        if (sampleCoC > 0) {
            // Far field: weight by how much the sample's CoC covers this distance
            weight = saturate(absSampleCoC - sampleDist + 1.0);
        } else {
            // Near field: stronger bleeding
            weight = saturate(absSampleCoC + 1.0);
        }
        
        // Bokeh brightness weighting (brighter samples contribute more for anamorphic look)
        float lum = dot(sampleColor.rgb, float3(0.2126, 0.7152, 0.0722));
        float bokehWeight = lerp(1.0, lum * 2.0, 0.3); // Subtle highlight emphasis
        weight *= bokehWeight;
        
        colorSum += sampleColor * weight;
        weightSum += weight;
    }
    
    if (weightSum > 0.001) {
        colorSum /= weightSum;
    } else {
        colorSum = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);
    }
    
    g_OutputColor[DTid.xy] = colorSum;
}
