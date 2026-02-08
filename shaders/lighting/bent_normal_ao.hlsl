// ─── Screen-Space Bent Normal Cone AO ────────────────────────────────────
// Computes ambient occlusion with bent normal output for improved
// indirect lighting directionality. The bent normal represents the
// average unoccluded direction and is used to:
//   - Weight IBL sampling toward less occluded directions
//   - Improve diffuse GI accuracy in corners/crevices
//   - Provide specular occlusion via cone-based visibility
//
// Algorithm:
//   1. Sample hemisphere directions around the normal
//   2. Ray march in screen space for each direction
//   3. Compute horizon angle per direction (HBAO-style)
//   4. Accumulate AO and bent normal from unoccluded samples
//   5. Compute visibility cone angle from bent normal divergence
//
// References:
//   - "Practical Realtime Strategies for Accurate Indirect Occlusion" (Jimenez, SIGGRAPH 2016)
//   - "Multi-Scale Global Illumination in Quantum Break" (Silvennoinen, SIGGRAPH 2015)

#include "../common/math.hlsl"

Texture2D<float4> g_GBuffer1    : register(t0); // RGB: World normal, A: roughness
Texture2D<float>  g_DepthBuffer : register(t1);
Texture2D<float4> g_NoiseTexture: register(t2); // 4x4 rotation/jitter noise

RWTexture2D<float4> g_AOOutput     : register(u0); // R: AO, GBA: Bent normal (world space)
RWTexture2D<float>  g_ConeOutput   : register(u1); // Visibility cone half-angle

SamplerState g_PointClamp  : register(s0);
SamplerState g_PointWrap   : register(s1);

struct BentNormalAOConstants {
    float4x4 view;
    float4x4 proj;
    float4x4 invProj;
    float2   resolution;
    float2   invResolution;
    float    near;
    float    far;
    float    radius;          // AO radius in world units (default 1.5)
    float    bias;            // Depth bias to avoid self-occlusion (default 0.03)
    float    intensity;       // AO darkening strength (default 1.0)
    float    power;           // AO power curve (default 2.0)
    uint     sampleCount;     // Directions per pixel (default 8)
    uint     stepsPerSample;  // Depth samples per direction (default 4)
    uint     frameIndex;      // For temporal jitter
    float    coneTraceAngle;  // Max cone angle for specular occlusion (default PI/3)
};

[[vk::push_constant]] ConstantBuffer<BentNormalAOConstants> cb;

// ─── Utility Functions ───────────────────────────────────────────────────

float LinearizeDepth(float d) {
    return cb.near * cb.far / (cb.far - d * (cb.far - cb.near));
}

float3 ViewPosFromDepth(float2 uv, float depth) {
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;
    float4 viewPos = mul(cb.invProj, clipPos);
    return viewPos.xyz / viewPos.w;
}

float3 DecodeNormal(float3 encoded) {
    return normalize(encoded * 2.0 - 1.0);
}

// Interleaved gradient noise for per-pixel jitter
float InterleavedGradientNoise(float2 pos) {
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(pos, magic.xy)));
}

// Cosine-weighted sample directions on hemisphere
float3 SampleHemisphere(uint index, uint count, float rotation) {
    float goldenAngle = 2.399963; // PI * (3 - sqrt(5))
    float angle = rotation + float(index) * goldenAngle;
    float cosAngle = cos(angle);
    float sinAngle = sin(angle);

    // Fibonacci spiral distribution
    float t = float(index + 0.5) / float(count);
    float cosTheta = sqrt(1.0 - t);
    float sinTheta = sqrt(t);

    return float3(sinTheta * cosAngle, sinTheta * sinAngle, cosTheta);
}

// ─── Main AO + Bent Normal Pass ──────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSBentNormalAO(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float depth = g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0);
    if (depth >= 1.0) {
        g_AOOutput[DTid.xy] = float4(1.0, 0.0, 0.0, 1.0);
        g_ConeOutput[DTid.xy] = PI * 0.5;
        return;
    }

    float3 viewPos = ViewPosFromDepth(uv, depth);
    float3 worldNorm = DecodeNormal(g_GBuffer1.SampleLevel(g_PointClamp, uv, 0).rgb);

    // Transform normal to view space for screen-space operations
    float3 viewNorm = normalize(mul((float3x3)cb.view, worldNorm));

    // Build tangent frame in view space
    float3 up = abs(viewNorm.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, viewNorm));
    float3 bitangent = cross(viewNorm, tangent);

    // Per-pixel rotation from noise
    float rotation = InterleavedGradientNoise(float2(DTid.xy)) * 2.0 * PI;
    rotation += float(cb.frameIndex) * 1.618033988; // Golden ratio temporal offset

    float totalAO = 0.0;
    float3 bentNormal = float3(0, 0, 0);
    float totalWeight = 0.0;

    for (uint s = 0; s < cb.sampleCount; ++s) {
        // Generate sample direction in tangent space
        float3 localDir = SampleHemisphere(s, cb.sampleCount, rotation);

        // Transform to view space
        float3 sampleDir = tangent * localDir.x + bitangent * localDir.y + viewNorm * localDir.z;

        // March in screen space along this direction
        float maxHorizon = cb.bias; // Start with bias to avoid self-occlusion

        for (uint step = 1; step <= cb.stepsPerSample; ++step) {
            float t = (float(step) / float(cb.stepsPerSample)) * cb.radius;
            float3 samplePos = viewPos + sampleDir * t;

            // Project sample to screen
            float4 sampleClip = mul(cb.proj, float4(samplePos, 1.0));
            float2 sampleUV = (sampleClip.xy / sampleClip.w) * 0.5 + 0.5;
            sampleUV.y = 1.0 - sampleUV.y;

            if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) break;

            float sampleDepth = g_DepthBuffer.SampleLevel(g_PointClamp, sampleUV, 0);
            float3 sampleViewPos = ViewPosFromDepth(sampleUV, sampleDepth);

            // Compute horizon angle
            float3 horizonVec = sampleViewPos - viewPos;
            float horizonLen = length(horizonVec);
            if (horizonLen < 0.001) continue;

            float horizonAngle = dot(horizonVec / horizonLen, viewNorm);

            // Update max horizon (only if within radius)
            if (horizonLen < cb.radius) {
                float falloff = 1.0 - (horizonLen / cb.radius);
                falloff *= falloff;
                maxHorizon = max(maxHorizon, horizonAngle * falloff);
            }
        }

        // AO contribution: 1 - max horizon angle (normalized)
        float occlusion = saturate(maxHorizon);
        totalAO += occlusion;

        // Bent normal: weight by visibility (1 - occlusion)
        float visibility = 1.0 - occlusion;
        bentNormal += sampleDir * visibility;
        totalWeight += visibility;
    }

    // Normalize AO
    float ao = 1.0 - saturate((totalAO / float(cb.sampleCount)) * cb.intensity);
    ao = pow(ao, cb.power);

    // Normalize bent normal (in view space)
    float3 bentNormView;
    if (totalWeight > 0.001) {
        bentNormView = normalize(bentNormal / totalWeight);
    } else {
        bentNormView = viewNorm;
    }

    // Transform bent normal back to world space
    float3x3 invView3x3 = transpose((float3x3)cb.view); // Orthogonal, so transpose = inverse
    float3 bentNormWorld = normalize(mul(invView3x3, bentNormView));

    // Compute visibility cone half-angle from bent normal divergence
    // Larger divergence = wider cone = more occluded
    float bentDot = saturate(dot(bentNormView, viewNorm));
    float coneAngle = acos(bentDot);
    coneAngle = min(coneAngle, cb.coneTraceAngle);

    g_AOOutput[DTid.xy] = float4(ao, bentNormWorld);
    g_ConeOutput[DTid.xy] = coneAngle;
}

// ─── Specular Occlusion from Bent Normal Cone ────────────────────────────
// Estimates how much of the specular lobe is occluded based on the
// bent normal cone and reflection direction.

Texture2D<float4> g_AOInput    : register(t3);
Texture2D<float>  g_ConeInput  : register(t4);

RWTexture2D<float> g_SpecOccOutput : register(u2);

[numthreads(8, 8, 1)]
void CSSpecularOcclusion(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float4 aoData = g_AOInput.SampleLevel(g_PointClamp, uv, 0);
    float ao = aoData.r;
    float3 bentNormal = aoData.gba;
    float coneAngle = g_ConeInput.SampleLevel(g_PointClamp, uv, 0);

    float3 worldNorm = DecodeNormal(g_GBuffer1.SampleLevel(g_PointClamp, uv, 0).rgb);
    float roughness = g_GBuffer1.SampleLevel(g_PointClamp, uv, 0).a;
    float depth = g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0);

    if (depth >= 1.0) {
        g_SpecOccOutput[DTid.xy] = 1.0;
        return;
    }

    float3 worldPos = ViewPosFromDepth(uv, depth); // Simplified; would use invViewProj
    float3 viewDir = normalize(-worldPos); // View space
    float3 reflectDir = reflect(-viewDir, worldNorm);

    // Angle between reflection direction and bent normal
    float reflectBentAngle = acos(saturate(dot(reflectDir, bentNormal)));

    // Specular lobe half-angle (approximation from roughness)
    float specConeAngle = roughness * roughness * PI * 0.5;

    // Intersection of specular cone with visibility cone
    // If reflection falls within the visibility cone, full visibility
    // Otherwise, gradually occlude
    float occlusionAngle = max(reflectBentAngle - specConeAngle, 0.0);
    float specOcclusion = saturate(1.0 - occlusionAngle / max(coneAngle, 0.001));

    // Modulate with diffuse AO for consistency
    specOcclusion = min(specOcclusion, lerp(1.0, ao, 0.5));

    g_SpecOccOutput[DTid.xy] = specOcclusion;
}

// ─── Spatial Denoise (Edge-Preserving Blur) ──────────────────────────────

Texture2D<float4> g_DenoiseInput : register(t5);
RWTexture2D<float4> g_DenoiseOutput : register(u3);

[numthreads(8, 8, 1)]
void CSDenoise(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float4 center = g_DenoiseInput.SampleLevel(g_PointClamp, uv, 0);
    float centerDepth = LinearizeDepth(g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0));
    float3 centerNorm = DecodeNormal(g_GBuffer1.SampleLevel(g_PointClamp, uv, 0).rgb);

    float4 totalColor = float4(0, 0, 0, 0);
    float totalWeight = 0.0;

    const int RADIUS = 4;
    for (int y = -RADIUS; y <= RADIUS; ++y) {
        for (int x = -RADIUS; x <= RADIUS; ++x) {
            float2 sampleUV = uv + float2(x, y) * cb.invResolution;
            if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) continue;

            float4 sampleColor = g_DenoiseInput.SampleLevel(g_PointClamp, sampleUV, 0);
            float sampleDepth = LinearizeDepth(g_DepthBuffer.SampleLevel(g_PointClamp, sampleUV, 0));
            float3 sampleNorm = DecodeNormal(g_GBuffer1.SampleLevel(g_PointClamp, sampleUV, 0).rgb);

            float spatialW = exp(-float(x * x + y * y) / 8.0);
            float depthW = exp(-abs(centerDepth - sampleDepth) / max(centerDepth * 0.05, 0.001));
            float normalW = pow(max(dot(centerNorm, sampleNorm), 0.0), 32.0);

            float weight = spatialW * depthW * normalW;
            totalColor += sampleColor * weight;
            totalWeight += weight;
        }
    }

    g_DenoiseOutput[DTid.xy] = totalWeight > 0.0 ? totalColor / totalWeight : center;
}
