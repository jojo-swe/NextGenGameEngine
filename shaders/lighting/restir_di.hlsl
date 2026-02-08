// ─── ReSTIR Direct Illumination (RTXDI) ──────────────────────────────────
// Reservoir-based Spatiotemporal Importance Resampling for direct lighting.
// Enables 1-ray-per-pixel quality equivalent to thousands of shadow rays.
//
// Algorithm:
//   1. Generate N candidate light samples per pixel
//   2. Evaluate target PDF (unshadowed contribution × visibility estimate)
//   3. Store best sample in reservoir with correct MIS weight
//   4. Temporal resampling: combine with previous frame's reservoir
//   5. Spatial resampling: share reservoirs with neighbors
//   6. Final: trace 1 shadow ray for the selected light sample

#include "../common/math.hlsl"
#include "../common/brdf.hlsl"

// ─── Light Data ──────────────────────────────────────────────────────────

struct LightInfo {
    float3 position;    // Point/spot lights
    float  radius;      // Light radius (for area lights)
    float3 color;       // Light color × intensity
    float  pad0;
    float3 direction;   // Spot/directional lights
    float  cosAngle;    // Spot light cone angle
    uint   type;        // 0=point, 1=spot, 2=directional, 3=area, 4=emissive_tri
    float3 normal;      // Area light normal
    float  area;        // Area light surface area
    uint   emissiveTriIndex; // For emissive triangle lights
    uint   pad1;
    uint   pad2;
    uint   pad3;
};

// ─── Reservoir ───────────────────────────────────────────────────────────

struct DIReservoir {
    uint   lightIndex;       // Selected light sample
    float3 samplePosition;   // Position on light surface
    float  targetPdf;        // p_hat(y) for selected sample
    float  weightSum;        // Running sum of weights (W_sum)
    uint   M;                // Number of candidates processed
    float  W;                // Unbiased contribution weight = W_sum / (M × p_hat)
};

DIReservoir CreateDIReservoir() {
    DIReservoir r;
    r.lightIndex = 0xFFFFFFFF;
    r.samplePosition = float3(0, 0, 0);
    r.targetPdf = 0;
    r.weightSum = 0;
    r.M = 0;
    r.W = 0;
    return r;
}

// ─── Buffers ─────────────────────────────────────────────────────────────

StructuredBuffer<LightInfo>    g_Lights          : register(t0, space6);
RWStructuredBuffer<DIReservoir> g_CurrentReservoirs : register(u0, space6);
StructuredBuffer<DIReservoir>  g_PrevReservoirs   : register(t1, space6);

Texture2D<float4> g_GBufferAlbedo   : register(t2, space6);
Texture2D<float2> g_GBufferNormal   : register(t3, space6);
Texture2D<float4> g_GBufferMaterial : register(t4, space6);
Texture2D<float>  g_Depth           : register(t5, space6);
Texture2D<float2> g_MotionVectors   : register(t6, space6);

RWTexture2D<float4> g_OutputDirect  : register(u1, space6);

RaytracingAccelerationStructure g_Scene : register(t7, space6);

struct ReSTIRConstants {
    float4x4 viewProj;
    float4x4 invViewProj;
    float4x4 prevViewProj;
    float4   cameraPos;
    uint     lightCount;
    uint     candidateCount;   // N candidates per pixel (e.g., 32)
    uint     spatialRadius;    // Pixel radius for spatial resampling (e.g., 30)
    uint     spatialSamples;   // Number of spatial neighbors (e.g., 5)
    uint     frameIndex;
    uint     screenWidth;
    uint     screenHeight;
    uint     enableTemporal;
    uint     enableSpatial;
    float    normalThreshold;  // Reject spatial if normals differ too much
    float    depthThreshold;   // Reject spatial if depth differs too much
    float    pad;
};

ConstantBuffer<ReSTIRConstants> g_ReSTIR : register(b0, space6);

// ─── Target PDF ──────────────────────────────────────────────────────────
// Evaluates how "good" a light sample is for a given surface point.
// Higher = brighter contribution. Used as resampling weight.

float EvaluateTargetPdf(float3 hitPos, float3 hitNormal, float3 V,
                         float3 albedo, float metallic, float roughness,
                         LightInfo light, float3 samplePos) {
    float3 L = samplePos - hitPos;
    float dist = length(L);
    L /= dist;

    float NdotL = max(dot(hitNormal, L), 0.0);
    if (NdotL <= 0.0) return 0.0;

    // Unshadowed BRDF contribution
    BRDFInput brdfIn;
    brdfIn.N = hitNormal;
    brdfIn.V = V;
    brdfIn.L = L;
    brdfIn.albedo = albedo;
    brdfIn.metallic = metallic;
    brdfIn.roughness = roughness;

    float3 brdf = EvaluatePBR(brdfIn);
    float3 lightContrib = light.color / (dist * dist + EPSILON);

    // Luminance of unshadowed contribution
    return Luminance(brdf * lightContrib * NdotL);
}

// ─── Light Sampling ──────────────────────────────────────────────────────

float3 SampleLightPosition(LightInfo light, inout uint seed) {
    switch (light.type) {
        case 0: // Point light
            return light.position;
        case 1: // Spot light
            return light.position;
        case 2: // Directional
            return float3(0, 0, 0); // Use direction instead
        case 3: { // Area light — sample disk
            float2 xi = RandomFloat2(seed);
            float r = sqrt(xi.x) * light.radius;
            float theta = TWO_PI * xi.y;
            float3 tangent = normalize(cross(light.normal, float3(0, 1, 0)));
            float3 bitangent = cross(light.normal, tangent);
            return light.position + tangent * (r * cos(theta)) + bitangent * (r * sin(theta));
        }
        default:
            return light.position;
    }
}

float LightSamplingPdf(LightInfo light) {
    // Uniform random selection from light pool
    return 1.0 / max((float)g_ReSTIR.lightCount, 1.0);
}

// ─── Pass 1: Initial Candidate Generation ────────────────────────────────

[numthreads(8, 8, 1)]
void InitialCandidatesCS(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= g_ReSTIR.screenWidth || DTid.y >= g_ReSTIR.screenHeight) return;

    uint pixelIdx = DTid.y * g_ReSTIR.screenWidth + DTid.x;
    uint seed = PCGHash(pixelIdx + g_ReSTIR.frameIndex * 12345);

    float2 uv = (float2(DTid.xy) + 0.5) / float2(g_ReSTIR.screenWidth, g_ReSTIR.screenHeight);

    // Reconstruct surface from G-buffer
    float depth = g_Depth[DTid.xy];
    if (depth >= 1.0) {
        g_CurrentReservoirs[pixelIdx] = CreateDIReservoir();
        return;
    }

    float3 worldPos = ReconstructWorldPos(uv, depth, g_ReSTIR.invViewProj);
    float3 normal = OctDecode(g_GBufferNormal[DTid.xy]);
    float3 albedo = g_GBufferAlbedo[DTid.xy].rgb;
    float metallic = g_GBufferMaterial[DTid.xy].r;
    float roughness = g_GBufferMaterial[DTid.xy].g;
    float3 V = normalize(g_ReSTIR.cameraPos.xyz - worldPos);

    DIReservoir reservoir = CreateDIReservoir();

    // Generate N candidate light samples
    for (uint i = 0; i < g_ReSTIR.candidateCount; ++i) {
        // Randomly select a light
        uint lightIdx = (uint)(RandomFloat(seed) * g_ReSTIR.lightCount);
        lightIdx = min(lightIdx, g_ReSTIR.lightCount - 1);

        LightInfo light = g_Lights[lightIdx];
        float3 samplePos = SampleLightPosition(light, seed);
        float sourcePdf = LightSamplingPdf(light);

        // Evaluate target PDF
        float pHat = EvaluateTargetPdf(worldPos, normal, V, albedo, metallic, roughness, light, samplePos);

        // RIS weight = p_hat / p_source
        float weight = pHat / max(sourcePdf, EPSILON);

        // Update reservoir
        reservoir.weightSum += weight;
        reservoir.M += 1;
        if (RandomFloat(seed) * reservoir.weightSum < weight) {
            reservoir.lightIndex = lightIdx;
            reservoir.samplePosition = samplePos;
            reservoir.targetPdf = pHat;
        }
    }

    // Finalize: W = weightSum / (M × p_hat)
    if (reservoir.targetPdf > 0) {
        reservoir.W = reservoir.weightSum / (max((float)reservoir.M, 1.0) * reservoir.targetPdf);
    }

    g_CurrentReservoirs[pixelIdx] = reservoir;
}

// ─── Pass 2: Temporal Resampling ─────────────────────────────────────────

[numthreads(8, 8, 1)]
void TemporalResamplingCS(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= g_ReSTIR.screenWidth || DTid.y >= g_ReSTIR.screenHeight) return;
    if (!g_ReSTIR.enableTemporal) return;

    uint pixelIdx = DTid.y * g_ReSTIR.screenWidth + DTid.x;
    uint seed = PCGHash(pixelIdx + g_ReSTIR.frameIndex * 54321);

    float2 uv = (float2(DTid.xy) + 0.5) / float2(g_ReSTIR.screenWidth, g_ReSTIR.screenHeight);
    float2 motion = g_MotionVectors[DTid.xy];
    float2 prevUV = uv - motion;

    DIReservoir current = g_CurrentReservoirs[pixelIdx];

    // Check if previous pixel is valid
    if (all(prevUV >= 0.0) && all(prevUV <= 1.0)) {
        uint2 prevCoord = uint2(prevUV * float2(g_ReSTIR.screenWidth, g_ReSTIR.screenHeight));
        uint prevIdx = prevCoord.y * g_ReSTIR.screenWidth + prevCoord.x;

        DIReservoir prev = g_PrevReservoirs[prevIdx];

        // Cap history length to prevent bias accumulation
        prev.M = min(prev.M, 20 * current.M);

        // Combine: treat previous reservoir as a single candidate
        float weight = prev.targetPdf * prev.W * prev.M;
        current.weightSum += weight;
        current.M += prev.M;

        if (RandomFloat(seed) * current.weightSum < weight) {
            current.lightIndex = prev.lightIndex;
            current.samplePosition = prev.samplePosition;
            current.targetPdf = prev.targetPdf;
        }

        if (current.targetPdf > 0) {
            current.W = current.weightSum / (max((float)current.M, 1.0) * current.targetPdf);
        }
    }

    g_CurrentReservoirs[pixelIdx] = current;
}

// ─── Pass 3: Spatial Resampling ──────────────────────────────────────────

[numthreads(8, 8, 1)]
void SpatialResamplingCS(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= g_ReSTIR.screenWidth || DTid.y >= g_ReSTIR.screenHeight) return;
    if (!g_ReSTIR.enableSpatial) return;

    uint pixelIdx = DTid.y * g_ReSTIR.screenWidth + DTid.x;
    uint seed = PCGHash(pixelIdx + g_ReSTIR.frameIndex * 77777);

    float2 uv = (float2(DTid.xy) + 0.5) / float2(g_ReSTIR.screenWidth, g_ReSTIR.screenHeight);
    float3 normal = OctDecode(g_GBufferNormal[DTid.xy]);
    float depth = g_Depth[DTid.xy];

    DIReservoir current = g_CurrentReservoirs[pixelIdx];

    for (uint i = 0; i < g_ReSTIR.spatialSamples; ++i) {
        // Random neighbor within radius
        float angle = RandomFloat(seed) * TWO_PI;
        float r = sqrt(RandomFloat(seed)) * (float)g_ReSTIR.spatialRadius;
        int2 neighborCoord = int2(DTid.xy) + int2(r * cos(angle), r * sin(angle));

        if (neighborCoord.x < 0 || neighborCoord.x >= (int)g_ReSTIR.screenWidth ||
            neighborCoord.y < 0 || neighborCoord.y >= (int)g_ReSTIR.screenHeight) continue;

        uint neighborIdx = neighborCoord.y * g_ReSTIR.screenWidth + neighborCoord.x;

        // Reject if normals or depths differ too much
        float3 neighborNormal = OctDecode(g_GBufferNormal[neighborCoord]);
        float neighborDepth = g_Depth[neighborCoord];

        if (dot(normal, neighborNormal) < g_ReSTIR.normalThreshold) continue;
        if (abs(depth - neighborDepth) > g_ReSTIR.depthThreshold) continue;

        DIReservoir neighbor = g_CurrentReservoirs[neighborIdx];

        float weight = neighbor.targetPdf * neighbor.W * neighbor.M;
        current.weightSum += weight;
        current.M += neighbor.M;

        if (RandomFloat(seed) * current.weightSum < weight) {
            current.lightIndex = neighbor.lightIndex;
            current.samplePosition = neighbor.samplePosition;
            current.targetPdf = neighbor.targetPdf;
        }
    }

    if (current.targetPdf > 0) {
        current.W = current.weightSum / (max((float)current.M, 1.0) * current.targetPdf);
    }

    g_CurrentReservoirs[pixelIdx] = current;
}

// ─── Pass 4: Final Shading (Trace Shadow Ray) ───────────────────────────

[numthreads(8, 8, 1)]
void FinalShadingCS(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= g_ReSTIR.screenWidth || DTid.y >= g_ReSTIR.screenHeight) return;

    uint pixelIdx = DTid.y * g_ReSTIR.screenWidth + DTid.x;
    float2 uv = (float2(DTid.xy) + 0.5) / float2(g_ReSTIR.screenWidth, g_ReSTIR.screenHeight);

    float depth = g_Depth[DTid.xy];
    if (depth >= 1.0) {
        g_OutputDirect[DTid.xy] = float4(0, 0, 0, 1);
        return;
    }

    DIReservoir reservoir = g_CurrentReservoirs[pixelIdx];
    if (reservoir.lightIndex == 0xFFFFFFFF || reservoir.targetPdf <= 0) {
        g_OutputDirect[DTid.xy] = float4(0, 0, 0, 1);
        return;
    }

    float3 worldPos = ReconstructWorldPos(uv, depth, g_ReSTIR.invViewProj);
    float3 normal = OctDecode(g_GBufferNormal[DTid.xy]);
    float3 albedo = g_GBufferAlbedo[DTid.xy].rgb;
    float metallic = g_GBufferMaterial[DTid.xy].r;
    float roughness = g_GBufferMaterial[DTid.xy].g;
    float3 V = normalize(g_ReSTIR.cameraPos.xyz - worldPos);

    LightInfo light = g_Lights[reservoir.lightIndex];
    float3 samplePos = reservoir.samplePosition;

    // Trace 1 shadow ray to the selected light
    float3 L = samplePos - worldPos;
    float dist = length(L);
    L /= dist;

    // TODO: actual ray trace
    float visibility = 1.0;

    // Evaluate final contribution
    BRDFInput brdfIn;
    brdfIn.N = normal;
    brdfIn.V = V;
    brdfIn.L = L;
    brdfIn.albedo = albedo;
    brdfIn.metallic = metallic;
    brdfIn.roughness = roughness;

    float3 brdf = EvaluatePBR(brdfIn);
    float NdotL = max(dot(normal, L), 0.0);
    float3 lightContrib = light.color / (dist * dist + EPSILON);

    float3 directLight = brdf * lightContrib * NdotL * visibility * reservoir.W;

    g_OutputDirect[DTid.xy] = float4(directLight, 1.0);
}
