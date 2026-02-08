// ─── Real-Time Path Tracer ───────────────────────────────────────────────
// Supports two modes:
//   Production: 1 SPP + ReSTIR temporal/spatial resampling + neural denoiser
//   Reference:  Progressive accumulation, unlimited bounces, spectral option
//
// Architecture:
//   Ray Gen → Primary Hit → Material Eval → NEE + ReSTIR → Bounce/Terminate
//   → Denoiser → Temporal Accumulation → Upscaling

#include "../common/math.hlsl"
#include "../common/brdf.hlsl"

// ─── Acceleration Structure ──────────────────────────────────────────────
RaytracingAccelerationStructure g_Scene : register(t0, space4);

// ─── Output ──────────────────────────────────────────────────────────────
RWTexture2D<float4> g_OutputDiffuse   : register(u0, space4);
RWTexture2D<float4> g_OutputSpecular  : register(u1, space4);
RWTexture2D<float4> g_OutputDirect    : register(u2, space4);
RWTexture2D<float4> g_OutputIndirect  : register(u3, space4);

// ─── Constants ───────────────────────────────────────────────────────────
struct PathTracerConstants {
    float4x4 invViewProj;
    float4x4 prevViewProj;
    float4   cameraPos;
    float4   cameraRight;
    float4   cameraUp;
    float4   cameraForward;
    uint     frameIndex;
    uint     screenWidth;
    uint     screenHeight;
    uint     maxBounces;
    uint     samplesPerPixel;
    uint     enableReSTIR;
    float    russianRouletteStart; // Bounce index to start RR
    float    pad;
};

ConstantBuffer<PathTracerConstants> g_PTConstants : register(b0, space4);

// ─── Reservoir for ReSTIR ────────────────────────────────────────────────
struct Reservoir {
    float3 sampleRadiance;
    float  samplePdf;
    float3 sampleDirection;
    float  weightSum;
    uint   M;              // Number of candidates seen
    float  W;              // Unbiased contribution weight
};

Reservoir CreateReservoir() {
    Reservoir r;
    r.sampleRadiance = float3(0, 0, 0);
    r.samplePdf = 0;
    r.sampleDirection = float3(0, 0, 0);
    r.weightSum = 0;
    r.M = 0;
    r.W = 0;
    return r;
}

bool UpdateReservoir(inout Reservoir r, float3 radiance, float pdf, float3 dir, float weight, inout uint seed) {
    r.weightSum += weight;
    r.M += 1;
    if (RandomFloat(seed) * r.weightSum < weight) {
        r.sampleRadiance = radiance;
        r.samplePdf = pdf;
        r.sampleDirection = dir;
        return true;
    }
    return false;
}

void FinalizeReservoir(inout Reservoir r) {
    if (r.weightSum > 0 && r.samplePdf > 0) {
        r.W = r.weightSum / (max((float)r.M, 1.0) * r.samplePdf);
    } else {
        r.W = 0;
    }
}

// ─── Ray Payload ─────────────────────────────────────────────────────────
struct RayPayload {
    float3 radiance;
    float3 throughput;
    float3 hitPosition;
    float3 hitNormal;
    float  hitDistance;
    uint   seed;
    bool   hit;
    uint   bounceIndex;
};

// ─── Generate Camera Ray ─────────────────────────────────────────────────
RayDesc GenerateCameraRay(uint2 pixelCoord, uint sampleIndex) {
    uint seed = PCGHash(pixelCoord.x + pixelCoord.y * g_PTConstants.screenWidth +
                        g_PTConstants.frameIndex * g_PTConstants.screenWidth * g_PTConstants.screenHeight +
                        sampleIndex * 1337);

    // Sub-pixel jitter for anti-aliasing
    float2 jitter = RandomFloat2(seed) - 0.5;
    float2 uv = (float2(pixelCoord) + 0.5 + jitter) / float2(g_PTConstants.screenWidth, g_PTConstants.screenHeight);
    uv = uv * 2.0 - 1.0;
    uv.y = -uv.y; // Vulkan NDC

    float4 clipPos = float4(uv, 0.0, 1.0);
    float4 worldPos = mul(g_PTConstants.invViewProj, clipPos);
    worldPos.xyz /= worldPos.w;

    RayDesc ray;
    ray.Origin = g_PTConstants.cameraPos.xyz;
    ray.Direction = normalize(worldPos.xyz - ray.Origin);
    ray.TMin = 0.001;
    ray.TMax = 100000.0;

    return ray;
}

// ─── Next Event Estimation (Direct Lighting) ─────────────────────────────
// Sample a light source and trace a shadow ray for direct illumination.
// In production mode, this uses ReSTIR for efficient many-light sampling.
float3 NextEventEstimation(float3 hitPos, float3 hitNormal, float3 V,
                            float3 albedo, float metallic, float roughness,
                            inout uint seed) {
    // Placeholder: single directional sun light
    float3 lightDir = normalize(float3(0.5, 0.8, 0.3));
    float3 lightColor = float3(5.0, 4.8, 4.5);

    float NdotL = max(dot(hitNormal, lightDir), 0.0);
    if (NdotL <= 0.0) return float3(0, 0, 0);

    // Shadow ray
    RayDesc shadowRay;
    shadowRay.Origin = hitPos + hitNormal * 0.001;
    shadowRay.Direction = lightDir;
    shadowRay.TMin = 0.001;
    shadowRay.TMax = 100000.0;

    // TODO: Trace shadow ray against TLAS
    // For now, assume no occlusion
    float visibility = 1.0;

    // Evaluate BRDF
    BRDFInput brdfIn;
    brdfIn.N = hitNormal;
    brdfIn.V = V;
    brdfIn.L = lightDir;
    brdfIn.albedo = albedo;
    brdfIn.metallic = metallic;
    brdfIn.roughness = roughness;

    float3 brdf = EvaluatePBR(brdfIn);
    return brdf * lightColor * visibility;
}

// ─── Path Tracing Loop ───────────────────────────────────────────────────
float3 TracePath(RayDesc ray, inout uint seed) {
    float3 radiance = float3(0, 0, 0);
    float3 throughput = float3(1, 1, 1);

    for (uint bounce = 0; bounce < g_PTConstants.maxBounces; ++bounce) {
        // TODO: TraceRay against TLAS
        // For now, simulate a hit on the ground plane at y=0
        float t = -ray.Origin.y / ray.Direction.y;
        bool hitGround = t > 0.001 && ray.Direction.y < 0;

        if (!hitGround) {
            // Sky contribution
            float3 skyColor = float3(0.5, 0.7, 1.0) * 0.5;
            float sunDot = max(dot(ray.Direction, normalize(float3(0.5, 0.8, 0.3))), 0.0);
            skyColor += float3(10, 8, 6) * pow(sunDot, 128.0);
            radiance += throughput * skyColor;
            break;
        }

        float3 hitPos = ray.Origin + ray.Direction * t;
        float3 hitNormal = float3(0, 1, 0);
        float3 V = -ray.Direction;

        // Material (checkerboard for testing)
        float checker = step(0.5, frac(hitPos.x * 0.5)) * step(0.5, frac(hitPos.z * 0.5));
        checker += step(0.5, 1.0 - frac(hitPos.x * 0.5)) * step(0.5, 1.0 - frac(hitPos.z * 0.5));
        float3 albedo = lerp(float3(0.1, 0.1, 0.1), float3(0.9, 0.9, 0.9), checker);
        float metallic = 0.0;
        float roughness = 0.5;

        // Direct lighting (NEE)
        radiance += throughput * NextEventEstimation(hitPos, hitNormal, V, albedo, metallic, roughness, seed);

        // Russian roulette
        if (bounce >= (uint)g_PTConstants.russianRouletteStart) {
            float survivalProb = max(max(throughput.x, throughput.y), throughput.z);
            survivalProb = clamp(survivalProb, 0.05, 0.95);
            if (RandomFloat(seed) > survivalProb) break;
            throughput /= survivalProb;
        }

        // Sample next bounce direction (cosine-weighted hemisphere)
        float2 xi = RandomFloat2(seed);
        float3 localDir = SampleCosineHemisphere(xi);
        float3x3 TBN = BuildTBN(hitNormal);
        float3 bounceDir = normalize(mul(localDir, TBN));

        // Update throughput: diffuse BRDF * cosTheta / pdf
        // For cosine-weighted sampling: pdf = cos(theta)/pi, BRDF = albedo/pi
        // So throughput *= albedo (the pi terms cancel)
        throughput *= albedo * (1.0 - metallic);

        // Next ray
        ray.Origin = hitPos + hitNormal * 0.001;
        ray.Direction = bounceDir;
    }

    return radiance;
}

// ─── Ray Generation Shader ───────────────────────────────────────────────
[shader("raygeneration")]
void RayGeneration() {
    uint2 pixelCoord = DispatchRaysIndex().xy;
    uint seed = PCGHash(pixelCoord.x + pixelCoord.y * g_PTConstants.screenWidth +
                        g_PTConstants.frameIndex * 77777);

    float3 totalRadiance = float3(0, 0, 0);

    for (uint s = 0; s < g_PTConstants.samplesPerPixel; ++s) {
        RayDesc ray = GenerateCameraRay(pixelCoord, s);
        totalRadiance += TracePath(ray, seed);
    }

    totalRadiance /= (float)g_PTConstants.samplesPerPixel;

    // Write separated channels for denoiser
    g_OutputDirect[pixelCoord]  = float4(totalRadiance, 1.0);
    g_OutputIndirect[pixelCoord] = float4(0, 0, 0, 1); // TODO: separate direct/indirect
}

// ─── Closest Hit Shader ──────────────────────────────────────────────────
[shader("closesthit")]
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attribs) {
    payload.hit = true;
    payload.hitDistance = RayTCurrent();

    // TODO: Reconstruct position, normal, UVs from vertex buffers
    // using InstanceID, PrimitiveIndex, and barycentrics
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y,
                                  attribs.barycentrics.x, attribs.barycentrics.y);
    payload.hitPosition = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    payload.hitNormal = float3(0, 1, 0); // Placeholder
}

// ─── Miss Shader ─────────────────────────────────────────────────────────
[shader("miss")]
void Miss(inout RayPayload payload) {
    payload.hit = false;
    // Sky radiance
    float3 dir = WorldRayDirection();
    float3 skyColor = float3(0.5, 0.7, 1.0) * 0.5;
    float sunDot = max(dot(dir, normalize(float3(0.5, 0.8, 0.3))), 0.0);
    skyColor += float3(10, 8, 6) * pow(sunDot, 128.0);
    payload.radiance = skyColor;
}
