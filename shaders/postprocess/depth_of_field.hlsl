// ─── Depth of Field Compute Shader ───────────────────────────────────────
// Two-pass bokeh DOF: CoC calculation + hexagonal blur.
// Pass 1 (CSCalculateCoC): compute Circle of Confusion per pixel
// Pass 2 (CSBokehBlur): gather-based hexagonal bokeh blur

#define THREAD_GROUP_SIZE 8

// Constant buffer
cbuffer DOFConstants : register(b0) {
    float4x4 g_InvProj;
    float    g_FocusDistance;     // Distance to focal plane (world units)
    float    g_FocusRange;       // Range of sharp focus
    float    g_BokehRadius;      // Max blur radius in pixels
    float    g_ApertureSize;     // f-stop simulation (larger = more blur)
    float2   g_TexelSize;        // 1/width, 1/height
    float    g_NearPlane;
    float    g_FarPlane;
    int      g_SampleCount;      // Bokeh sample ring count
    int      g_UseBokehShape;    // 0 = circular, 1 = hexagonal
    float2   g_Pad;
};

// Resources
Texture2D<float4>   g_ColorInput    : register(t0);
Texture2D<float>    g_DepthInput    : register(t1);
RWTexture2D<float>  g_CoCOutput     : register(u0);
RWTexture2D<float4> g_BlurOutput    : register(u1);
SamplerState        g_LinearClamp   : register(s0);

// ─── Helper Functions ────────────────────────────────────────────────────

float LinearizeDepth(float ndcDepth) {
    // Reverse-Z: depth 0 = far, depth 1 = near
    float z = ndcDepth;
    return g_NearPlane * g_FarPlane / (g_FarPlane - z * (g_FarPlane - g_NearPlane));
}

float CalculateCoC(float linearDepth) {
    float dist = abs(linearDepth - g_FocusDistance);
    float coc = saturate((dist - g_FocusRange * 0.5) / max(g_FocusRange, 0.001));
    coc *= g_BokehRadius;

    // Sign: negative for near field, positive for far field
    if (linearDepth < g_FocusDistance) {
        return -coc;
    }
    return coc;
}

// Hexagonal blur kernel offsets (6-sided bokeh)
static const float2 g_HexOffsets[6] = {
    float2( 1.0,  0.0),
    float2( 0.5,  0.866),
    float2(-0.5,  0.866),
    float2(-1.0,  0.0),
    float2(-0.5, -0.866),
    float2( 0.5, -0.866),
};

// ─── Pass 1: Calculate Circle of Confusion ──────────────────────────────

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void CSCalculateCoC(uint3 dispatchId : SV_DispatchThreadID) {
    float2 uv = (float2(dispatchId.xy) + 0.5) * g_TexelSize;
    float depth = g_DepthInput.SampleLevel(g_LinearClamp, uv, 0);
    float linearDepth = LinearizeDepth(depth);
    float coc = CalculateCoC(linearDepth);

    g_CoCOutput[dispatchId.xy] = coc;
}

// ─── Pass 2: Bokeh Blur (Gather) ────────────────────────────────────────

float3 SampleWithWeight(float2 uv, float cocRadius, float sampleCoC) {
    // Prevent background bleeding into foreground
    float weight = (sampleCoC >= cocRadius) ? 1.0 : saturate(sampleCoC / max(cocRadius, 0.001));
    float3 color = g_ColorInput.SampleLevel(g_LinearClamp, uv, 0).rgb;
    return color * weight;
}

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void CSBokehBlur(uint3 dispatchId : SV_DispatchThreadID) {
    float2 centerUV = (float2(dispatchId.xy) + 0.5) * g_TexelSize;
    float centerCoC = g_CoCOutput[dispatchId.xy];
    float absCoc = abs(centerCoC);

    // No blur needed for sharp pixels
    if (absCoc < 0.5) {
        g_BlurOutput[dispatchId.xy] = g_ColorInput.SampleLevel(g_LinearClamp, centerUV, 0);
        return;
    }

    float3 colorSum = float3(0, 0, 0);
    float weightSum = 0;

    // Center sample
    float3 centerColor = g_ColorInput.SampleLevel(g_LinearClamp, centerUV, 0).rgb;
    colorSum += centerColor;
    weightSum += 1.0;

    // Concentric rings
    int rings = g_SampleCount;
    for (int ring = 1; ring <= rings; ring++) {
        float ringRadius = (float(ring) / float(rings)) * absCoc;
        int samplesInRing = ring * 6; // Hexagonal ring

        for (int s = 0; s < samplesInRing; s++) {
            float angle;
            float2 offset;

            if (g_UseBokehShape == 1) {
                // Hexagonal: interpolate between hex vertices
                float t = float(s) / float(samplesInRing);
                int edge = int(t * 6.0) % 6;
                int nextEdge = (edge + 1) % 6;
                float edgeT = frac(t * 6.0);
                offset = lerp(g_HexOffsets[edge], g_HexOffsets[nextEdge], edgeT);
            } else {
                // Circular
                angle = float(s) * (6.283185 / float(samplesInRing));
                offset = float2(cos(angle), sin(angle));
            }

            float2 sampleUV = centerUV + offset * ringRadius * g_TexelSize;

            // Bounds check
            if (sampleUV.x < 0 || sampleUV.x > 1 || sampleUV.y < 0 || sampleUV.y > 1)
                continue;

            float sampleCoC = abs(g_CoCOutput.SampleLevel(g_LinearClamp, sampleUV, 0));

            // Gather weight: ring area normalization
            float ringWeight = 1.0 / max(float(samplesInRing), 1.0);

            // Prevent background bleeding
            float cocWeight = saturate(sampleCoC / max(absCoc, 0.001));

            float3 sampleColor = g_ColorInput.SampleLevel(g_LinearClamp, sampleUV, 0).rgb;

            // Bokeh highlight boost: weight bright samples more
            float luminance = dot(sampleColor, float3(0.2126, 0.7152, 0.0722));
            float highlightWeight = 1.0 + saturate(luminance - 0.8) * 4.0;

            float finalWeight = ringWeight * cocWeight * highlightWeight;
            colorSum += sampleColor * finalWeight;
            weightSum += finalWeight;
        }
    }

    float3 result = colorSum / max(weightSum, 0.001);

    // Alpha stores CoC for potential later use (e.g., alpha compositing)
    g_BlurOutput[dispatchId.xy] = float4(result, saturate(absCoc / g_BokehRadius));
}

// ─── Pass 3: Near/Far Composite ─────────────────────────────────────────
// Blends near-field DOF on top of the far-field result

Texture2D<float4> g_NearFieldBlur : register(t2);
Texture2D<float4> g_FarFieldBlur  : register(t3);
RWTexture2D<float4> g_CompositeOutput : register(u2);

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void CSComposite(uint3 dispatchId : SV_DispatchThreadID) {
    float2 uv = (float2(dispatchId.xy) + 0.5) * g_TexelSize;
    float coc = g_CoCOutput[dispatchId.xy];

    float4 sharpColor = g_ColorInput.SampleLevel(g_LinearClamp, uv, 0);
    float4 farBlur = g_FarFieldBlur.SampleLevel(g_LinearClamp, uv, 0);
    float4 nearBlur = g_NearFieldBlur.SampleLevel(g_LinearClamp, uv, 0);

    // Blend factor based on CoC
    float farFactor = saturate(coc / g_BokehRadius);
    float nearFactor = saturate(-coc / g_BokehRadius);

    // Composite: sharp → far blend → near overlay
    float3 result = lerp(sharpColor.rgb, farBlur.rgb, farFactor);
    result = lerp(result, nearBlur.rgb, nearFactor);

    g_CompositeOutput[dispatchId.xy] = float4(result, 1.0);
}
